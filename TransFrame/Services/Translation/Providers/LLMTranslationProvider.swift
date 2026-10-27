//
//  LLMTranslationProvider.swift
//  TransFrame
//
//  LLM-based translation provider for OpenAI, Claude, and Ollama
//

import Foundation
import os.log

/// LLM-based translation provider supporting OpenAI, Claude, and Ollama
actor LLMTranslationProvider: TranslationProvider, TranslationPromptConfigurable {
    // MARK: - Properties

    nonisolated let id: String
    nonisolated let name: String
    let engineType: TranslationEngineType
    let config: TranslationEngineConfig

    private let keychain: KeychainService
    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "TransFrame",
        category: "LLMTranslationProvider"
    )

    // MARK: - Initialization

    init(
        type: TranslationEngineType,
        config: TranslationEngineConfig,
        keychain: KeychainService
    ) async throws {
        self.engineType = type
        self.id = type.rawValue
        self.config = config
        self.keychain = keychain

        switch type {
        case .openai:
            self.name = "OpenAI Translation"
        case .claude:
            self.name = "Claude Translation"
        case .gemini:
            self.name = "Gemini Translation"
        case .ollama:
            self.name = "Ollama Translation"
        default:
            throw TranslationProviderError.invalidConfiguration("Invalid LLM type: \(type.rawValue)")
        }
    }

    // MARK: - TranslationProvider Protocol

    var isAvailable: Bool {
        get async {
            // Ollama doesn't need API key
            if engineType == .ollama {
                return true
            }
            // Check for API key in keychain
            return await keychain.hasCredentials(for: engineType)
        }
    }

    func translate(
        text: String,
        from sourceLanguage: String?,
        to targetLanguage: String
    ) async throws -> TranslationResult {
        let results = try await translate(
            texts: [text],
            from: sourceLanguage,
            to: targetLanguage,
            promptTemplate: nil
        )

        guard let result = results.first else {
            throw TranslationProviderError.translationFailed("No translation returned")
        }
        return result
    }

    func translate(
        texts: [String],
        from sourceLanguage: String?,
        to targetLanguage: String,
        promptTemplate: String?
    ) async throws -> [TranslationResult] {
        guard !texts.isEmpty else { return [] }

        let batches = LLMTranslationBatching.split(
            texts: texts, maxOutputTokens: config.options?.maxTokens ?? 2048, promptTemplate: promptTemplate
        )
        guard batches.count == 1, batches[0] == texts else {
            return try await translateBatches(
                batches, from: sourceLanguage, to: targetLanguage, promptTemplate: promptTemplate
            )
        }
        return try await translateSingleBatch(texts, from: sourceLanguage, to: targetLanguage, promptTemplate: promptTemplate)
    }

    func translate(
        texts: [String],
        from sourceLanguage: String?,
        to targetLanguage: String
    ) async throws -> [TranslationResult] {
        try await translate(
            texts: texts,
            from: sourceLanguage,
            to: targetLanguage,
            promptTemplate: nil
        )
    }

    func verifyConnection() async throws {
        let credentials = try await getCredentials()
        let baseURL = try getBaseURL()
        
        // For OpenAI and Ollama, we can perform a models GET check (non-billing)
        if engineType == .openai || engineType == .ollama {
            let endpoint: URL
            if engineType == .ollama {
                // Use the same OpenAI-compatible path family as translation to
                // avoid "test passes but translation 404" (or vice versa).
                endpoint = Self.appendingAPISuffix(baseURL, suffix: "v1/models")
            } else {
                endpoint = Self.appendingAPISuffix(baseURL, suffix: "models")
            }
            
            var request = URLRequest(url: endpoint)
            request.httpMethod = "GET"
            request.timeoutInterval = 10.0
            
            if let apiKey = credentials?.apiKey {
                // Security check
                if let host = baseURL.host, !Self.isLocalhost(host) && baseURL.scheme != "https" {
                    throw TranslationProviderError.invalidConfiguration(
                        "Refusing to send API key over insecure connection (HTTP). Use HTTPS or a localhost URL."
                    )
                }
                request.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
            }
            
            if let headers = request.allHTTPHeaderFields {
                var safeHeaders: [String: String] = [:]
                for (key, value) in headers {
                    let lowerKey = key.lowercased()
                    if lowerKey == "authorization" {
                        if value.lowercased().hasPrefix("bearer ") {
                            safeHeaders[key] = "Bearer " + String(value.dropFirst(7).prefix(4)) + "..."
                        } else {
                            safeHeaders[key] = String(value.prefix(4)) + "..."
                        }
                    } else if lowerKey == "x-api-key" || lowerKey.contains("key") {
                        safeHeaders[key] = String(value.prefix(4)) + "..."
                    } else {
                        safeHeaders[key] = value
                    }
                }
                logger.info("Sending verifyConnection request to \(endpoint.absoluteString) with headers: \(safeHeaders)")
            }
            
            let (_, response) = try await URLSession.shared.data(for: request)
            guard let httpResponse = response as? HTTPURLResponse else {
                throw TranslationProviderError.connectionFailed("Invalid response")
            }
            guard httpResponse.statusCode == 200 else {
                logger.error("Connection verify failed: status \(httpResponse.statusCode)")
                throw TranslationProviderError.connectionFailed("Connection verify failed: HTTP \(httpResponse.statusCode)")
            }
        } else {
            // Claude, Gemini, etc. fallback to translation of "1"
            _ = try await translate(text: "1", from: "en", to: "zh")
        }
    }

    // MARK: - Custom Prompt

    // MARK: - Private Methods

    private func translateBatches(
        _ batches: [[String]], from sourceLanguage: String?, to targetLanguage: String, promptTemplate: String?
    ) async throws -> [TranslationResult] {
        var results: [TranslationResult] = []
        for batch in batches {
            results.append(contentsOf: try await translateSingleBatch(
                batch, from: sourceLanguage, to: targetLanguage, promptTemplate: promptTemplate
            ))
        }
        return results
    }

    private func translateSingleBatch(
        _ texts: [String], from sourceLanguage: String?, to targetLanguage: String, promptTemplate: String?
    ) async throws -> [TranslationResult] {
        let credentials = try await getCredentials()
        let combinedText = texts.joined(separator: LLMTranslationBatching.separator)
        let prompt = buildPrompt(text: combinedText, sourceLanguage: sourceLanguage, targetLanguage: targetLanguage, promptTemplate: promptTemplate)
        let translatedText = try await callLLMAPI(prompt: prompt, credentials: credentials)
        let translatedTexts = translatedText.components(separatedBy: LLMTranslationBatching.separator)
        if translatedTexts.count == texts.count {
            return zip(texts, translatedTexts).map {
                TranslationResult(sourceText: $0, translatedText: $1.trimmingCharacters(in: .whitespacesAndNewlines), sourceLanguage: sourceLanguage ?? "Auto", targetLanguage: targetLanguage)
            }
        }
        guard texts.count > 1 else {
            return [TranslationResult(sourceText: combinedText, translatedText: translatedText, sourceLanguage: sourceLanguage ?? "Auto", targetLanguage: targetLanguage)]
        }
        logger.warning("Batch split failed, retrying only unmatched individual texts")
        return try await translateBatches(texts.map { [$0] }, from: sourceLanguage, to: targetLanguage, promptTemplate: promptTemplate)
    }

    private func getCredentials() async throws -> StoredCredentials? {
        guard engineType.requiresAPIKey else { return nil }
        guard let credentials = try await keychain.getCredentials(for: engineType) else {
            throw TranslationProviderError.invalidConfiguration("API key required for \(engineType.rawValue)")
        }
        return credentials
    }

    private func buildPrompt(
        text: String,
        sourceLanguage: String?,
        targetLanguage: String,
        promptTemplate: String?
    ) -> String {
        let source = TranslationLanguage.promptDisplayName(for: sourceLanguage)
        let target = TranslationLanguage.promptDisplayName(for: targetLanguage)

        if let template = promptTemplate {
            return template
                .replacingOccurrences(of: "{source_language}", with: source)
                .replacingOccurrences(of: "{target_language}", with: target)
                .replacingOccurrences(of: "{text}", with: text)
        }

        // Default prompt — include separator preservation instruction when batch separators present
        let separatorHint = text.contains("\n---\n")
            ? "Preserve the literal '---' separators exactly where they appear in the input. Do not add or remove any separators."
            : ""
        return """
            Translate the following text from \(source) to \(target).
            Provide ONLY the translated text without any explanations, notes, or formatting.
            \(separatorHint)

            Text to translate:
            \(text)
            """
    }

    private func callLLMAPI(
        prompt: String,
        credentials: StoredCredentials?
    ) async throws -> String {
        let baseURL = try getBaseURL()
        let modelName = getModelName()

        // Build endpoint and headers based on engine type
        let endpoint: URL
        var headers: [String: String] = ["Content-Type": "application/json"]

        switch engineType {
        case .claude:
            // Claude uses /v1/messages endpoint
            endpoint = Self.appendingAPISuffix(baseURL, suffix: "v1/messages")
            if let apiKey = credentials?.apiKey {
                // Security: reject non-HTTPS endpoints (except localhost) when sending API keys
                if let host = baseURL.host, !Self.isLocalhost(host) && baseURL.scheme != "https" {
                    throw TranslationProviderError.invalidConfiguration(
                        "Refusing to send API key over insecure connection (HTTP). Use HTTPS or a localhost URL."
                    )
                }
                headers["x-api-key"] = apiKey
                headers["anthropic-version"] = "2023-06-01"
            }
        case .gemini:
            // Gemini OpenAI-compatible endpoint: /v1beta/openai/chat/completions
            endpoint = Self.appendingAPISuffix(baseURL, suffix: "v1beta/openai/chat/completions")
            if let apiKey = credentials?.apiKey {
                if let host = baseURL.host, !Self.isLocalhost(host) && baseURL.scheme != "https" {
                    throw TranslationProviderError.invalidConfiguration(
                        "Refusing to send API key over insecure connection (HTTP). Use HTTPS or a localhost URL."
                    )
                }
                headers["Authorization"] = "Bearer \(apiKey)"
            }
        case .ollama:
            // Ollama OpenAI-compatible endpoint: /v1/chat/completions
            endpoint = Self.appendingAPISuffix(baseURL, suffix: "v1/chat/completions")
        default:
            // OpenAI and others use /chat/completions (base typically ends with /v1)
            endpoint = Self.appendingAPISuffix(baseURL, suffix: "chat/completions")
            if let apiKey = credentials?.apiKey {
                if let host = baseURL.host, !Self.isLocalhost(host) && baseURL.scheme != "https" {
                    throw TranslationProviderError.invalidConfiguration(
                        "Refusing to send API key over insecure connection (HTTP). Use HTTPS or a localhost URL."
                    )
                }
                headers["Authorization"] = "Bearer \(apiKey)"
            }
        }

        var request = URLRequest(url: endpoint)
        request.httpMethod = "POST"
        request.timeoutInterval = config.options?.timeout ?? 30
        for (key, value) in headers {
            request.setValue(value, forHTTPHeaderField: key)
        }

        // Build request body based on engine type
        let body: [String: Any]
        switch engineType {
        case .claude:
            // Claude API format
            body = [
                "model": modelName,
                "max_tokens": config.options?.maxTokens ?? 2048,
                "messages": [
                    ["role": "user", "content": prompt]
                ]
            ]
        default:
            // OpenAI/Gemini/Ollama format
            body = [
                "model": modelName,
                "messages": [
                    ["role": "user", "content": prompt]
                ],
                "stream": false,
                "temperature": config.options?.temperature ?? 0.3,
                "max_tokens": config.options?.maxTokens ?? 2048
            ]
        }

        request.httpBody = try JSONSerialization.data(withJSONObject: body)

        // Execute request
        if let headers = request.allHTTPHeaderFields {
            var safeHeaders: [String: String] = [:]
            for (key, value) in headers {
                let lowerKey = key.lowercased()
                if lowerKey == "authorization" {
                    if value.lowercased().hasPrefix("bearer ") {
                        safeHeaders[key] = "Bearer " + String(value.dropFirst(7).prefix(4)) + "..."
                    } else {
                        safeHeaders[key] = String(value.prefix(4)) + "..."
                    }
                } else if lowerKey == "x-api-key" || lowerKey.contains("key") {
                    safeHeaders[key] = String(value.prefix(4)) + "..."
                } else {
                    safeHeaders[key] = value
                }
            }
            logger.info("Sending request to \(endpoint.absoluteString) with headers: \(safeHeaders)")
        }
        let (data, response) = try await URLSession.shared.data(for: request)

        guard let httpResponse = response as? HTTPURLResponse else {
            throw TranslationProviderError.connectionFailed("Invalid response")
        }

        guard httpResponse.statusCode == 200 else {
            // Log status code only to avoid exposing user text in logs
            logger.error("API error status=\(httpResponse.statusCode)")

            if httpResponse.statusCode == 401 {
                throw TranslationProviderError.invalidConfiguration("Invalid API key")
            } else if httpResponse.statusCode == 429 {
                throw TranslationProviderError.rateLimited(retryAfter: nil)
            }

            throw TranslationProviderError.translationFailed("API error: \(httpResponse.statusCode)")
        }

        // Parse response based on engine type
        return try parseResponse(data, for: engineType)
    }

    private func parseResponse(_ data: Data, for engineType: TranslationEngineType) throws -> String {
        try LLMTranslationResponseParser.extractText(from: data, for: engineType)
    }


    private func getBaseURL() throws -> URL {
        if let customURL = config.options?.baseURL {
            guard let url = URL(string: customURL) else {
                throw TranslationProviderError.invalidConfiguration("Invalid custom baseURL: \(customURL)")
            }
            return url.resolvingLocalhost
        }

        if let defaultURL = engineType.defaultBaseURL,
           let url = URL(string: defaultURL) {
            return url.resolvingLocalhost
        }

        guard let url = URL(string: "https://api.openai.com/v1") else {
            throw TranslationProviderError.invalidConfiguration("Failed to create API URL")
        }
        return url.resolvingLocalhost
    }

    private func getModelName() -> String {
        return config.options?.modelName ?? engineType.defaultModelName ?? "gpt-4o-mini"
    }

    /// Appends an API path suffix to a base URL without duplicating version segments.
    /// If the base URL already ends with a matching prefix of the suffix, only the
    /// remainder is appended. Handles trailing slashes and `URL.appendingPathComponent`
    /// percent-encoding quirks.
    private nonisolated static func appendingAPISuffix(_ base: URL, suffix: String) -> URL {
        var urlString = base.absoluteString
        while urlString.hasSuffix("/") {
            urlString = String(urlString.dropLast())
        }
        guard var url = URL(string: urlString) else { return base }
        let baseParts = url.pathComponents.filter { $0 != "/" && !$0.isEmpty }
        let suffixParts = suffix.split(separator: "/").map(String.init)
        // Find the longest overlapping prefix of suffix already present at end of base
        for overlap in stride(from: min(baseParts.count, suffixParts.count), through: 1, by: -1) {
            let baseTail = Array(baseParts.suffix(overlap)).map { $0.lowercased() }
            let suffixHead = Array(suffixParts.prefix(overlap)).map { $0.lowercased() }
            if baseTail == suffixHead {
                // Append only the non-overlapping remainder
                let remainder = suffixParts.dropFirst(overlap)
                for part in remainder {
                    url = url.appendingPathComponent(part)
                }
                return url
            }
        }
        // No overlap: append all parts
        for part in suffixParts {
            url = url.appendingPathComponent(part)
        }
        return url
    }

    /// Check if a hostname refers to localhost (safe for HTTP)
    private nonisolated static func isLocalhost(_ host: String) -> Bool {
        let lowered = host.lowercased()
        return lowered == "localhost"
            || lowered == "127.0.0.1"
            || lowered == "::1"
            || lowered == "0.0.0.0"
            || lowered.hasSuffix(".local")
    }
}
