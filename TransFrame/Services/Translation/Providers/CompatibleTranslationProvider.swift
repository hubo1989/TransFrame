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
    nonisolated let configurationSnapshot: ConfigurationSnapshot

    private let config: TranslationEngineConfig
    private let compatibleConfig: CompatibleConfig
    private let promptConfigID: String?
    private let keychain: KeychainService
    private let session: URLSession
    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "TransFrame",
        category: "CompatibleTranslationProvider"
    )

    // MARK: - Configuration

    struct ConfigurationSnapshot: Equatable, Sendable {
        let engineConfig: TranslationEngineConfig
        let compatibleConfig: CompatibleConfig
        let promptConfigID: String?
    }

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
                id: UUID(uuidString: "00000000-0000-0000-0000-000000000000")!,
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
            hasher.combine(id)
            hasher.combine(displayName)
            hasher.combine(baseURL)
            hasher.combine(modelName)
            hasher.combine(hasAPIKey)
            return hasher.finalize()
        }
    }

    static func resolveConfigurationSnapshot(
        from config: TranslationEngineConfig,
        savedConfigs: [CompatibleConfig]
    ) -> ConfigurationSnapshot {
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

        return ConfigurationSnapshot(
            engineConfig: config,
            compatibleConfig: resolvedCompatibleConfig,
            promptConfigID: resolvedPromptConfigID
        )
    }

    // MARK: - Initialization

    init(config: TranslationEngineConfig, keychain: KeychainService) async throws {
        let savedConfigs: [CompatibleConfig] = await MainActor.run {
            AppSettings.shared.compatibleProviderConfigs
        }
        let snapshot = Self.resolveConfigurationSnapshot(from: config, savedConfigs: savedConfigs)
        self.init(snapshot: snapshot, keychain: keychain, providerID: "custom")
    }

    init(
        config: TranslationEngineConfig,
        compatibleConfig: CompatibleConfig,
        keychain: KeychainService,
        session: URLSession = .shared
    ) async throws {
        let snapshot = ConfigurationSnapshot(
            engineConfig: config,
            compatibleConfig: compatibleConfig,
            promptConfigID: compatibleConfig.id.uuidString
        )
        self.init(snapshot: snapshot, keychain: keychain, providerID: compatibleConfig.keychainId, session: session)
    }

    init(
        snapshot: ConfigurationSnapshot,
        keychain: KeychainService,
        providerID: String,
        session: URLSession = .shared
    ) {
        self.config = snapshot.engineConfig
        self.compatibleConfig = snapshot.compatibleConfig
        self.promptConfigID = snapshot.promptConfigID
        self.keychain = keychain
        self.session = session
        self.id = providerID
        self.name = snapshot.compatibleConfig.displayName
        self.configHash = snapshot.compatibleConfig.configHash
        self.configurationSnapshot = snapshot
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

        let batches = LLMTranslationBatching.split(
            texts: texts, maxOutputTokens: config.options?.maxTokens ?? 2048, promptTemplate: promptTemplate
        )
        guard batches.count == 1, batches[0] == texts else {
            var results: [TranslationResult] = []
            for batch in batches {
                results.append(contentsOf: try await translateSingleBatch(
                    batch, from: sourceLanguage, to: targetLanguage, promptTemplate: promptTemplate
                ))
            }
            return results
        }
        return try await translateSingleBatch(texts, from: sourceLanguage, to: targetLanguage, promptTemplate: promptTemplate)
    }

    private func translateSingleBatch(
        _ texts: [String], from sourceLanguage: String?, to targetLanguage: String, promptTemplate: String?
    ) async throws -> [TranslationResult] {
        let combinedText = texts.joined(separator: LLMTranslationBatching.separator)
        let combinedResult = try await translate(
            text: combinedText,
            from: sourceLanguage,
            to: targetLanguage,
            promptTemplate: promptTemplate
        )

        let translatedTexts = combinedResult.translatedText.components(separatedBy: LLMTranslationBatching.separator)

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
        guard texts.count > 1 else {
            return [combinedResult]
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
        let (data, response) = try await session.data(for: request)

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
        try LLMTranslationResponseParser.extractText(from: data, for: .custom)
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
