//
//  CompatibleTranslationProvider.swift
//  TransFrame
//
//  OpenAI-compatible custom translation provider
//

import Foundation
import os.log

/// OpenAI-compatible translation provider for custom endpoints
actor CompatibleTranslationProvider: TranslationProvider, TranslationPromptConfigurable, TranslationPromptContextProviding {
    // MARK: - Properties

    nonisolated let id: String
    nonisolated let name: String
    nonisolated let configHash: Int

    private let config: TranslationEngineConfig
    private let compatibleConfig: CompatibleConfig
    private let promptConfigID: String?
    private let keychain: KeychainService
    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "TransFrame",
        category: "CompatibleTranslationProvider"
    )

    // MARK: - Configuration

    struct CompatibleConfig: Codable, Equatable, Sendable, Identifiable {
        var id: UUID
        var displayName: String
        var baseURL: String
        var modelName: String
        var hasAPIKey: Bool

        init(
            id: UUID = UUID(),
            displayName: String,
            baseURL: String,
            modelName: String,
            hasAPIKey: Bool = true
        ) {
            self.id = id
            self.displayName = displayName
            self.baseURL = baseURL
            self.modelName = modelName
            self.hasAPIKey = hasAPIKey
        }

        static var `default`: CompatibleConfig {
            CompatibleConfig(
                displayName: "Custom",
                baseURL: "http://localhost:8000/v1",
                modelName: "default",
                hasAPIKey: false
            )
        }

        var keychainId: String {
            return "custom:\(id.uuidString)"
        }

        var configHash: Int {
            var hasher = Hasher()
            hasher.combine(baseURL)
            hasher.combine(modelName)
            hasher.combine(hasAPIKey)
            return hasher.finalize()
        }
    }

    // MARK: - Initialization

    init(config: TranslationEngineConfig, keychain: KeychainService) async throws {
        self.config = config
        self.keychain = keychain

        let savedConfigs: [CompatibleConfig] = await MainActor.run {
            AppSettings.shared.compatibleProviderConfigs
        }

        // Parse compatible config from customName or fallback to AppSettings authoritative store
        let resolvedCompatibleConfig: CompatibleConfig
        if let customName = config.customName,
           let jsonData = customName.data(using: .utf8),
           let parsed = try? JSONDecoder().decode(CompatibleConfig.self, from: jsonData),
           let latest = savedConfigs.first(where: { $0.id == parsed.id }) {
            resolvedCompatibleConfig = latest
        } else if let customName = config.customName,
                  let jsonData = customName.data(using: .utf8),
                  let parsed = try? JSONDecoder().decode(CompatibleConfig.self, from: jsonData) {
            resolvedCompatibleConfig = parsed
        } else if let firstSaved = savedConfigs.first {
            resolvedCompatibleConfig = firstSaved
        } else {
            resolvedCompatibleConfig = .default
        }

        let resolvedPromptConfigID = savedConfigs.contains(where: { $0.id == resolvedCompatibleConfig.id })
            ? resolvedCompatibleConfig.id.uuidString
            : nil
        self.compatibleConfig = resolvedCompatibleConfig
        self.promptConfigID = resolvedPromptConfigID

        self.id = "custom"
        self.name = self.compatibleConfig.displayName
        self.configHash = self.compatibleConfig.configHash
    }

    init(
        config: TranslationEngineConfig,
        compatibleConfig: CompatibleConfig,
        keychain: KeychainService
    ) async throws {
        self.config = config
        self.compatibleConfig = compatibleConfig
        self.promptConfigID = compatibleConfig.id.uuidString
        self.keychain = keychain
        self.id = compatibleConfig.keychainId
        self.name = compatibleConfig.displayName
        self.configHash = compatibleConfig.configHash
    }

    // MARK: - TranslationProvider Protocol

    var isAvailable: Bool {
        get async {
            if compatibleConfig.hasAPIKey {
                let keychainId = compatibleConfig.keychainId
                return await keychain.hasCredentials(forCompatibleId: keychainId)
            }
            return !compatibleConfig.baseURL.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
        }
    }

    func translate(
        text: String,
        from sourceLanguage: String?,
        to targetLanguage: String
    ) async throws -> TranslationResult {
        try await translate(
            text: text,
            from: sourceLanguage,
            to: targetLanguage,
            promptTemplate: nil
        )
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

    func translate(
        texts: [String],
        from sourceLanguage: String?,
        to targetLanguage: String,
        promptTemplate: String?
    ) async throws -> [TranslationResult] {
        guard !texts.isEmpty else { return [] }

        // Combine texts for efficiency
        let combinedText = texts.joined(separator: "\n---\n")
        let combinedResult = try await translate(
            text: combinedText,
            from: sourceLanguage,
            to: targetLanguage,
            promptTemplate: promptTemplate
        )

        let translatedTexts = combinedResult.translatedText.components(separatedBy: "\n---\n")

        if translatedTexts.count == texts.count {
            return zip(texts, translatedTexts).map { source, translated in
                TranslationResult(
                    sourceText: source,
                    translatedText: translated.trimmingCharacters(in: .whitespacesAndNewlines),
                    sourceLanguage: combinedResult.sourceLanguage,
                    targetLanguage: combinedResult.targetLanguage
                )
            }
        }

        // Split failed - translate individually to ensure correct mapping
        logger.warning("Batch split failed, falling back to individual translations")
        var results: [TranslationResult] = []
        results.reserveCapacity(texts.count)
        for text in texts {
            let result = try await translate(
                text: text,
                from: sourceLanguage,
                to: targetLanguage,
                promptTemplate: promptTemplate
            )
            results.append(result)
        }
        return results
    }

    func verifyConnection() async throws {
        let baseURL = compatibleConfig.baseURL.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        guard let url = URL(string: baseURL) else {
            throw TranslationProviderError.invalidConfiguration("Invalid base URL")
        }
        
        let apiURL = url.resolvingLocalhost.appendingPathComponent("models")
        
        var request = URLRequest(url: apiURL)
        request.httpMethod = "GET"
        request.timeoutInterval = 10.0
        
        if compatibleConfig.hasAPIKey {
            let keychainId = compatibleConfig.keychainId
            if let credentials = try await keychain.getCredentials(forCompatibleId: keychainId) {
                // Security check
                if let host = url.resolvingLocalhost.host, !Self.isLocalhost(host) && url.resolvingLocalhost.scheme != "https" {
                    throw TranslationProviderError.invalidConfiguration(
                        "Refusing to send API key over insecure connection (HTTP). Use HTTPS or a localhost URL."
                    )
                }
                request.setValue("Bearer \(credentials.apiKey)", forHTTPHeaderField: "Authorization")
            }
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
            logger.info("Sending verifyConnection request to \(apiURL.absoluteString) with headers: \(safeHeaders)")
        }
        
        let (_, response) = try await URLSession.shared.data(for: request)
        guard let httpResponse = response as? HTTPURLResponse else {
            throw TranslationProviderError.connectionFailed("Invalid response")
        }
        
        guard httpResponse.statusCode == 200 else {
            logger.error("Connection verify failed: status \(httpResponse.statusCode)")
            throw TranslationProviderError.connectionFailed("Connection verify failed: HTTP \(httpResponse.statusCode)")
        }
    }

    func compatiblePromptIdentifier() async -> String? {
        promptConfigID
    }

    // MARK: - Private Methods

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

        return """
            Translate the following text from \(source) to \(target).
            Provide ONLY the translated text without any explanations or additional text.

            Text to translate:
            \(text)
            """
    }

    private func callAPI(
        prompt: String,
        credentials: StoredCredentials?
    ) async throws -> String {
        let baseURL = compatibleConfig.baseURL.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        guard let url = URL(string: baseURL) else {
            throw TranslationProviderError.invalidConfiguration("Invalid base URL")
        }

        // Build OpenAI-compatible endpoint: baseURL/chat/completions
        let apiURL = url.resolvingLocalhost.appendingPathComponent("chat/completions")

        var request = URLRequest(url: apiURL)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.timeoutInterval = config.options?.timeout ?? 60

        // Add authorization if API key is configured
        if let apiKey = credentials?.apiKey {
            request.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
        }

        // Build OpenAI-compatible request body
        let body: [String: Any] = [
            "model": compatibleConfig.modelName,
            "messages": [
                ["role": "user", "content": prompt]
            ],
            "stream": false,
            "temperature": config.options?.temperature ?? 0.3,
            "max_tokens": config.options?.maxTokens ?? 2048
        ]

        request.httpBody = try JSONSerialization.data(withJSONObject: body)

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
            logger.info("Sending request to \(apiURL.absoluteString) with headers: \(safeHeaders)")
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

        return try parseResponse(data)
    }

    private func parseResponse(_ data: Data) throws -> String {
        guard let rawString = String(data: data, encoding: .utf8) else {
            throw TranslationProviderError.translationFailed("Unable to decode data to UTF-8 string")
        }

        let trimmedResponse = rawString.trimmingCharacters(in: .whitespacesAndNewlines)

        // Check if response is Server-Sent Events (SSE) stream format
        if trimmedResponse.hasPrefix("data:") || trimmedResponse.contains("\ndata:") {
            return try parseSSEStream(trimmedResponse)
        }

        let jsonObject: Any
        do {
            jsonObject = try JSONSerialization.jsonObject(with: data)
        } catch {
            logger.error("JSON serialization failed: \(error.localizedDescription). Response size: \(data.count) bytes")
            throw error
        }

        guard let json = jsonObject as? [String: Any] else {
            logger.error("Response JSON is not a dictionary object")
            throw TranslationProviderError.translationFailed("Response JSON is not a dictionary")
        }

        // Handle error responses from OpenAI compatible APIs
        if let errorObj = json["error"] as? [String: Any],
           let errorMessage = errorObj["message"] as? String {
            logger.error("API returned error: \(errorMessage)")
            throw TranslationProviderError.translationFailed("API error: \(errorMessage)")
        }

        guard let choices = json["choices"] as? [[String: Any]],
              let firstChoice = choices.first,
              let message = firstChoice["message"] as? [String: Any],
              let content = message["content"] as? String else {
            logger.error("Unexpected JSON response structure (missing choices or content)")
            throw TranslationProviderError.translationFailed("Unexpected JSON response structure")
        }

        return content.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private func parseSSEStream(_ streamText: String) throws -> String {
        var resultText = ""
        let lines = streamText.components(separatedBy: .newlines)

        for line in lines {
            let trimmedLine = line.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !trimmedLine.isEmpty else { continue }

            // Skip stream end marker
            if trimmedLine == "data: [DONE]" {
                continue
            }

            if trimmedLine.hasPrefix("data:") {
                let jsonText = trimmedLine.dropFirst(5).trimmingCharacters(in: .whitespaces)
                guard !jsonText.isEmpty else { continue }

                guard let jsonData = jsonText.data(using: .utf8),
                      let json = try? JSONSerialization.jsonObject(with: jsonData) as? [String: Any] else {
                    continue
                }

                if let choices = json["choices"] as? [[String: Any]],
                   let firstChoice = choices.first {
                    if let delta = firstChoice["delta"] as? [String: Any],
                       let content = delta["content"] as? String {
                        resultText += content
                    } else if let text = firstChoice["text"] as? String {
                        resultText += text
                    }
                }
            }
        }

        guard !resultText.isEmpty else {
            logger.error("Failed to extract any text from SSE stream (length: \(streamText.count))")
            throw TranslationProviderError.translationFailed("Empty text from SSE stream")
        }

        return resultText.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private func translate(
        text: String,
        from sourceLanguage: String?,
        to targetLanguage: String,
        promptTemplate: String?
    ) async throws -> TranslationResult {
        guard !text.isEmpty else {
            throw TranslationProviderError.emptyInput
        }

        let keychainId = compatibleConfig.keychainId
        let credentials: StoredCredentials?
        if compatibleConfig.hasAPIKey {
            guard let creds = try await keychain.getCredentials(forCompatibleId: keychainId) else {
                throw TranslationProviderError.invalidConfiguration("API key required but not found for \(compatibleConfig.displayName)")
            }
            credentials = creds
        } else {
            credentials = nil
        }

        let prompt = buildPrompt(
            text: text,
            sourceLanguage: sourceLanguage,
            targetLanguage: targetLanguage,
            promptTemplate: promptTemplate
        )

        let start = Date()
        let translatedText = try await callAPI(prompt: prompt, credentials: credentials)
        let latency = Date().timeIntervalSince(start)

        logger.info("Custom translation completed in \(latency)s")

        return TranslationResult(
            sourceText: text,
            translatedText: translatedText,
            sourceLanguage: sourceLanguage ?? "Auto",
            targetLanguage: targetLanguage
        )
    }

    private nonisolated static func isLocalhost(_ host: String) -> Bool {
        let lowered = host.lowercased()
        return lowered == "localhost"
            || lowered == "127.0.0.1"
            || lowered == "::1"
            || lowered == "0.0.0.0"
            || lowered.hasSuffix(".local")
            || lowered.contains(".local:")
    }
}
