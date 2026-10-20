import Foundation
import os.log

actor MTranServerEngine: TranslationProvider {
    // MARK: - TranslationProvider Properties

    nonisolated let id = "mtranserver"
    nonisolated let name = "MTransServer"

    var isAvailable: Bool {
        get async { await checkConnection() }
    }

    nonisolated var configuration: Configuration { .default }

    // MARK: - Properties

    static let shared = MTranServerEngine()
    private let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "TransFrame", category: "MTranServerEngine")

    // MARK: - Configuration

    /// MTranServer configuration options
    struct Configuration: Sendable {
        /// Server address (e.g., "localhost" or "192.168.1.100")
        var serverAddress: String

        /// Server port (default 8989)
        var serverPort: Int

        /// Request timeout in seconds
        var timeout: TimeInterval

        /// Whether to automatically detect source language
        var autoDetectSourceLanguage: Bool

        /// Default configuration from UserDefaults
        static var `default`: Configuration {
            // Read directly from UserDefaults to avoid MainActor isolation issues
            let defaults = UserDefaults.standard
            let prefix = "TransFrame."
            let host = defaults.string(forKey: prefix + "mtranServerHost") ?? "localhost"
            let port = defaults.object(forKey: prefix + "mtranServerPort") as? Int ?? 8989
            return Configuration(
                serverAddress: host,
                serverPort: port,
                timeout: 10.0,
                autoDetectSourceLanguage: true
            )
        }
    }

    // MARK: - Initialization

    private init() {}

    // MARK: - Public API

    /// Translates text using MTranServer.
    /// - Parameters:
    ///   - text: The text to translate
    ///   - sourceLanguage: Source language code (e.g., "en", "zh")
    ///   - targetLanguage: Target language code (e.g., "en", "zh")
    ///   - config: Translation configuration (uses default if not specified)
    /// - Returns: TranslationResult containing translated text
    /// - Throws: MTranServerError if translation fails
    func translate(
        _ text: String,
        from sourceLanguage: String? = nil,
        to targetLanguage: String,
        config: Configuration = .default
    ) async throws -> TranslationResult {
        logger.info("Starting translation: \(text.count) chars to \(targetLanguage)")
        logger.info("Config: \(config.serverAddress):\(config.serverPort)")

        // Reset cache to ensure we check with current settings
        MTranServerChecker.resetCache()
        guard await MTranServerChecker.checkAvailability() else {
            logger.error("MTranServer not available")
            throw MTranServerError.notAvailable
        }

        // Validate input
        guard !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            throw MTranServerError.emptyInput
        }

        let effectiveSourceLanguage = resolveSourceLanguage(
            sourceLanguage,
            autoDetect: config.autoDetectSourceLanguage
        )

        // Build request
        let url = try buildURL(config: config)
        logger.info("Translation URL: \(url.absoluteString)")
        let normalizedTarget = normalizeLanguageCode(targetLanguage)
        let jsonData = try buildRequestBody(text: text, from: effectiveSourceLanguage, to: normalizedTarget)

        // Perform request with timeout
        do {
            let result = try await performTranslationRequest(
                url: url,
                jsonData: jsonData,
                timeout: config.timeout
            )
            logger.info("Translation successful: \(result.translatedText.count) chars")
            return TranslationResult(
                sourceText: text,
                translatedText: result.translatedText,
                sourceLanguage: result.detectedLanguage ?? effectiveSourceLanguage,
                targetLanguage: targetLanguage
            )
        } catch {
            logger.error("Translation failed: \(error.localizedDescription)")
            throw error
        }
    }

    /// Translates text with automatic language detection.
    /// - Parameters:
    ///   - text: The text to translate
    ///   - targetLanguage: Target language code
    /// - Returns: TranslationResult containing translated text
    /// - Throws: MTranServerError if translation fails
    func translate(_ text: String, to targetLanguage: String) async throws -> TranslationResult {
        try await translate(text, from: nil, to: targetLanguage, config: .default)
    }

    // MARK: - TranslationProvider Protocol

    func translate(
        text: String,
        from sourceLanguage: String?,
        to targetLanguage: String
    ) async throws -> TranslationResult {
        try await translate(text, from: sourceLanguage, to: targetLanguage, config: .default)
    }

    func translate(
        texts: [String],
        from sourceLanguage: String?,
        to targetLanguage: String
    ) async throws -> [TranslationResult] {
        guard !texts.isEmpty else { return [] }

        var results: [TranslationResult] = []
        results.reserveCapacity(texts.count)

        for text in texts {
            let result = try await translate(
                text,
                from: sourceLanguage,
                to: targetLanguage,
                config: .default
            )
            results.append(result)
        }

        return results
    }

    func verifyConnection() async throws {
        MTranServerChecker.resetCache()
        guard await MTranServerChecker.checkAvailability() else {
            throw TranslationProviderError.connectionFailed("MTranServer is not reachable")
        }
    }

    // MARK: - Private Methods

    /// Resolves the effective source language for translation
    private func resolveSourceLanguage(_ source: String?, autoDetect: Bool) -> String {
        if let source = source, !source.isEmpty {
            return normalizeLanguageCode(source)
        }
        return "auto"
    }

    /// Normalizes language codes for MTranServer compatibility
    /// MTranServer expects simple codes like "zh", "en" rather than "zh-Hans", "en-US"
    private func normalizeLanguageCode(_ code: String) -> String {
        let lowercased = code.lowercased()
        // Map common variants to simple codes
        switch lowercased {
        case "zh-hans", "zh-cn", "zh_hans", "zh_cn":
            return "zh"
        case "zh-hant", "zh-tw", "zh_hant", "zh_tw":
            return "zh-TW"
        case "en-us", "en-gb", "en_us", "en_gb":
            return "en"
        case "ja-jp", "ja_jp":
            return "ja"
        case "ko-kr", "ko_kr":
            return "ko"
        default:
            // Extract base language code (e.g., "en-US" -> "en")
            if let hyphenIndex = lowercased.firstIndex(of: "-") {
                return String(lowercased[..<hyphenIndex])
            }
            if let underscoreIndex = lowercased.firstIndex(of: "_") {
                return String(lowercased[..<underscoreIndex])
            }
            return lowercased
        }
    }

    /// Builds the URL for MTranServer API endpoint
    private func buildURL(config: Configuration) throws -> URL {
        // Normalize localhost to 127.0.0.1 to avoid IPv6 resolution issues
        let host = config.serverAddress == "localhost" ? "127.0.0.1" : config.serverAddress
        let urlString = "http://\(host):\(config.serverPort)/translate"
        guard let url = URL(string: urlString) else {
            throw MTranServerError.invalidURL
        }
        return url
    }

    /// Builds the JSON request body for translation
    private func buildRequestBody(text: String, from: String, to: String) throws -> Data {
        let requestBody: [String: Any] = [
            "text": text,
            "from": from,
            "to": to
        ]

        guard let jsonData = try? JSONSerialization.data(withJSONObject: requestBody) else {
            throw MTranServerError.invalidRequest
        }
        return jsonData
    }

    /// Performs the translation request with timeout handling
    private func performTranslationRequest(
        url: URL,
        jsonData: Data,
        timeout: TimeInterval
    ) async throws -> TranslationResponse {
        try await withThrowingTaskGroup(of: Result<TranslationResponse, any Error>.self) { group in
            // Translation task
            group.addTask { [jsonData, url] in
                await self.executeTranslationRequest(url: url, jsonData: jsonData)
            }

            // Timeout task
            _ = group.addTaskUnlessCancelled { [timeout] in
                try await Task.sleep(nanoseconds: UInt64(timeout * 1_000_000_000))
                return .failure(MTranServerError.timeout)
            }

            // Wait for first completed task
            guard let result = try await group.next() else {
                throw MTranServerError.timeout
            }
            group.cancelAll()
            return try result.get()
        }
    }

    /// Executes the HTTP request to MTranServer
    private func executeTranslationRequest(
        url: URL,
        jsonData: Data
    ) async -> Result<TranslationResponse, any Error> {
        do {
            var request = URLRequest(url: url)
            request.httpMethod = "POST"
            request.setValue("application/json", forHTTPHeaderField: "Content-Type")
            request.httpBody = jsonData
            
            // Debug: log request body
            if let jsonString = String(data: jsonData, encoding: .utf8) {
                logger.debug("Request body: \(jsonString)")
            }

            let (data, response) = try await URLSession.shared.data(for: request)
            
            // Debug: log response
            if let responseString = String(data: data, encoding: .utf8) {
                logger.debug("Response body: \(responseString)")
            }

            guard let httpResponse = response as? HTTPURLResponse else {
                logger.error("Invalid response type")
                return .failure(MTranServerError.invalidResponse)
            }
            
            logger.debug("Response status code: \(httpResponse.statusCode)")

            guard httpResponse.statusCode == 200 else {
                if httpResponse.statusCode == 503 {
                    return .failure(MTranServerError.serviceUnavailable)
                }
                return .failure(MTranServerError.httpError(statusCode: httpResponse.statusCode))
            }

            let decoded = try JSONDecoder().decode(TranslationResponse.self, from: data)
            return .success(decoded)
        } catch let error as MTranServerError {
            return .failure(error)
        } catch {
            logger.error("Request failed: \(error)")
            return .failure(MTranServerError.requestFailed(underlying: error))
        }
    }
}

// MARK: - Translation Response

/// MTranServer API response structure
private struct TranslationResponse: Codable, Sendable {
    let translatedText: String
    let detectedLanguage: String?

    enum CodingKeys: String, CodingKey {
        case translatedText = "translated_text"
        case detectedLanguage = "detected_language"
        case result = "result"
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)

        if let text = try? container.decode(String.self, forKey: .translatedText) {
            translatedText = text
        } else if let text = try? container.decode(String.self, forKey: .result) {
            translatedText = text
        } else {
            throw DecodingError.keyNotFound(
                CodingKeys.translatedText,
                DecodingError.Context(
                    codingPath: decoder.codingPath,
                    debugDescription: "Neither 'translated_text' nor 'result' field found"
                )
            )
        }

        detectedLanguage = try? container.decode(String.self, forKey: .detectedLanguage)
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(translatedText, forKey: .translatedText)
        try? container.encode(detectedLanguage, forKey: .detectedLanguage)
    }
}

// MARK: - MTranServer Errors

/// Errors that can occur during MTranServer operations
enum MTranServerError: LocalizedError, Sendable {
    /// MTranServer is not available
    case notAvailable

    /// Translation operation is already in progress
    case operationInProgress

    /// The input text is empty
    case emptyInput

    /// Invalid URL constructed
    case invalidURL

    /// Invalid request format
    case invalidRequest

    /// Invalid response from server
    case invalidResponse

    /// Request timeout
    case timeout

    /// Service unavailable (HTTP 503)
    case serviceUnavailable

    /// HTTP error with status code
    case httpError(statusCode: Int)

    /// Request failed with underlying error
    case requestFailed(underlying: any Error)

    var errorDescription: String? {
        switch self {
        case .notAvailable:
            return NSLocalizedString("error.mtran.not.available", comment: "")
        case .operationInProgress:
            return NSLocalizedString("error.translation.in.progress", comment: "")
        case .emptyInput:
            return NSLocalizedString("error.translation.empty.input", comment: "")
        case .invalidURL:
            return NSLocalizedString("error.mtran.invalid.url", comment: "")
        case .invalidRequest:
            return NSLocalizedString("error.mtran.invalid.request", comment: "")
        case .invalidResponse:
            return NSLocalizedString(
                "error.mtran.invalid.response",
                comment: ""
            )
        case .timeout:
            return NSLocalizedString("error.translation.timeout", comment: "")
        case .serviceUnavailable:
            return NSLocalizedString(
                "error.mtran.service.unavailable",
                comment: ""
            )
        case .httpError(let code):
            return String(
                format: NSLocalizedString("error.mtran.http.error", comment: ""),
                code
            )
        case .requestFailed:
            return NSLocalizedString("error.translation.failed", comment: "")
        }
    }

    var recoverySuggestion: String? {
        switch self {
        case .notAvailable:
            return NSLocalizedString(
                "error.mtran.not.available.recovery",
                comment: ""
            )
        case .operationInProgress:
            return NSLocalizedString("error.translation.in.progress.recovery", comment: "")
        case .emptyInput:
            return NSLocalizedString("error.translation.empty.input.recovery", comment: "")
        case .invalidURL:
            return NSLocalizedString("error.mtran.invalid.url.recovery", comment: "")
        case .invalidRequest:
            return NSLocalizedString("error.mtran.invalid.request.recovery", comment: "")
        case .invalidResponse:
            return NSLocalizedString(
                "error.mtran.invalid.response.recovery",
                comment: ""
            )
        case .timeout:
            return NSLocalizedString("error.translation.timeout.recovery", comment: "")
        case .serviceUnavailable:
            return NSLocalizedString(
                "error.mtran.service.unavailable.recovery",
                comment: ""
            )
        case .httpError:
            return NSLocalizedString("error.mtran.http.error.recovery", comment: "")
        case .requestFailed:
            return NSLocalizedString("error.translation.failed.recovery", comment: "")
        }
    }
}
