import Darwin
import Foundation
import os.log

actor MTranServerEngine: TranslationProvider {
    // MARK: - TranslationProvider Properties

    nonisolated let id = "mtranserver"
    nonisolated let name = "MTransServer"

    var isAvailable: Bool {
        configuration.isValid
    }

    nonisolated var configuration: Configuration { configuredSnapshot ?? .default }

    // MARK: - Properties

    static let shared = MTranServerEngine()
    private let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "TransFrame", category: "MTranServerEngine")

    // MARK: - Configuration

    /// MTranServer configuration options
    struct Configuration: Sendable, Equatable {
        /// Server address (e.g., "localhost" or "192.168.1.100")
        var serverAddress: String

        /// Server port (default 8989)
        var serverPort: Int

        /// Request timeout in seconds
        var timeout: TimeInterval

        /// Whether to automatically detect source language
        var autoDetectSourceLanguage: Bool

        init(
            serverAddress: String,
            serverPort: Int,
            timeout: TimeInterval,
            autoDetectSourceLanguage: Bool
        ) {
            self.serverAddress = serverAddress
            self.serverPort = serverPort
            self.timeout = timeout
            self.autoDetectSourceLanguage = autoDetectSourceLanguage
        }

        init?(
            serverURL: String,
            timeout: TimeInterval = 10.0,
            autoDetectSourceLanguage: Bool = true
        ) {
            let trimmed = serverURL.trimmingCharacters(in: .whitespacesAndNewlines)
            let endpoint = trimmed.contains("://") ? trimmed : "http://\(trimmed)"
            guard let components = URLComponents(string: endpoint),
                  let host = components.percentEncodedHost,
                  components.path.isEmpty || components.path == "/",
                  components.query == nil, components.fragment == nil,
                  components.user == nil, components.password == nil else { return nil }
            self.init(
                serverAddress: host,
                serverPort: components.port ?? 8989,
                timeout: timeout,
                autoDetectSourceLanguage: autoDetectSourceLanguage
            )
            guard isValid else { return nil }
        }

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

        var endpointHost: String? {
            let host = serverAddress.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !host.isEmpty, (1...65_535).contains(serverPort),
                  timeout.isFinite, timeout > 0 else { return nil }
            if host.hasPrefix("[") || host.hasSuffix("]") {
                guard host.hasPrefix("["), host.hasSuffix("]") else { return nil }
                let rawAddress = String(host.dropFirst().dropLast())
                var parsed = in6_addr()
                guard inet_pton(AF_INET6, rawAddress, &parsed) == 1 else { return nil }
                return host
            }
            if host == "localhost" { return "127.0.0.1" }
            // percentEncodedHost throws an uncatchable NSInvalidArgumentException
            // on illegal characters (e.g. legacy UserDefaults hosts with spaces
            // or non-ASCII input); restrict to hostname/IPv4 syntax.
            guard host.allSatisfy(Self.isHostnameSafeCharacter) else { return nil }
            return host
        }

        private static func isHostnameSafeCharacter(_ character: Character) -> Bool {
            guard let ascii = character.asciiValue else { return false }
            switch ascii {
            case 0x30...0x39, 0x41...0x5A, 0x61...0x7A: return true // 0-9 A-Z a-z
            case 0x2E, 0x2D, 0x5F: return true                     // . - _
            default: return false
            }
        }

        var isValid: Bool {
            guard let url = endpointURL(path: "/translate"),
                  url.host != nil, url.port == serverPort else { return false }
            return true
        }

        func endpointURL(path: String) -> URL? {
            guard let host = endpointHost else { return nil }
            var components = URLComponents()
            components.scheme = "http"
            components.percentEncodedHost = host
            components.port = serverPort
            components.path = path
            return components.url
        }
    }

    // MARK: - Initialization

    private let configuredSnapshot: Configuration?
    private let session: URLSession
    private let checker: MTranServerChecker

    init(
        configuration: Configuration? = nil,
        session: URLSession = .shared,
        checker: MTranServerChecker = .shared
    ) {
        self.configuredSnapshot = configuration
        self.session = session
        self.checker = checker
    }

    static func provider(
        config: TranslationEngineConfig,
        session: URLSession = .shared,
        checker: MTranServerChecker = .shared
    ) -> MTranServerEngine {
        let baseURL = config.options?.baseURL
        let timeout = config.options?.timeout ?? 10.0
        guard let baseURL, !baseURL.isEmpty,
              let parsed = Configuration(serverURL: baseURL, timeout: timeout) else {
            return MTranServerEngine(configuration: .default, session: session, checker: checker)
        }
        return MTranServerEngine(configuration: parsed, session: session, checker: checker)
    }

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
        config: Configuration? = nil
    ) async throws -> TranslationResult {
        let config = config ?? configuration
        logger.info("Starting translation: \(text.count) chars to \(targetLanguage)")
        logger.info("Config: \(config.serverAddress):\(config.serverPort)")

        // Validate input before making a request. Health state never gates translation.
        try Task.checkCancellation()
        guard config.isValid else { throw MTranServerError.invalidURL }

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
        try await translate(text, from: nil, to: targetLanguage, config: configuration)
    }

    // MARK: - TranslationProvider Protocol

    func translate(
        text: String,
        from sourceLanguage: String?,
        to targetLanguage: String
    ) async throws -> TranslationResult {
        try await translate(text, from: sourceLanguage, to: targetLanguage, config: configuration)
    }

    func translate(
        texts: [String],
        from sourceLanguage: String?,
        to targetLanguage: String
    ) async throws -> [TranslationResult] {
        guard !texts.isEmpty else { return [] }

        var results: [TranslationResult] = []
        results.reserveCapacity(texts.count)

        let config = configuration
        for text in texts {
            let result = try await translate(
                text,
                from: sourceLanguage,
                to: targetLanguage,
                config: config
            )
            results.append(result)
        }

        return results
    }

    func verifyConnection() async throws {
        try await verifyConnection(config: configuration)
    }

    func verifyConnection(config: Configuration) async throws {
        guard config.isValid else { throw MTranServerError.invalidURL }
        try Task.checkCancellation()
        let isAvailable = try await checker.checkAvailability(
            configuration: config,
            forceRefresh: true
        )
        guard isAvailable else {
            throw TranslationProviderError.connectionFailed("MTranServer is not reachable")
        }
    }

    func checkConnection(config: Configuration) async throws -> Bool {
        guard config.isValid else { throw MTranServerError.invalidURL }
        try Task.checkCancellation()
        return try await checker.checkAvailability(configuration: config, forceRefresh: true)
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
        guard let url = config.endpointURL(path: "/translate") else {
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
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.timeoutInterval = timeout
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = jsonData

        do {
            let (data, response) = try await session.data(for: request)
            guard let httpResponse = response as? HTTPURLResponse else {
                throw MTranServerError.invalidResponse
            }
            guard httpResponse.statusCode == 200 else {
                if httpResponse.statusCode == 503 {
                    throw MTranServerError.serviceUnavailable
                }
                throw MTranServerError.httpError(statusCode: httpResponse.statusCode)
            }
            return try JSONDecoder().decode(TranslationResponse.self, from: data)
        } catch is CancellationError {
            throw CancellationError()
        } catch let error as URLError where error.code == .cancelled {
            throw CancellationError()
        } catch let error as URLError where error.code == .timedOut {
            throw MTranServerError.timeout
        } catch let error as MTranServerError {
            throw error
        } catch {
            logger.error("Request failed: \(error)")
            throw MTranServerError.requestFailed(underlying: error)
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
