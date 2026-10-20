import Foundation
import os.log

/// Translation engine types supported by the application
enum TranslationEngineType: String, CaseIterable, Sendable, Codable, Identifiable {
    // MARK: - Built-in Engines

    /// macOS native Translation API (local, default)
    case apple = "apple"

    /// MTranServer (optional, external)
    case mtranServer = "mtran"

    // MARK: - LLM Translation Engines

    /// OpenAI GPT translation
    case openai = "openai"

    /// Anthropic Claude translation
    case claude = "claude"

    /// Google Gemini translation
    case gemini = "gemini"

    /// Ollama local LLM translation
    case ollama = "ollama"

    // MARK: - Cloud Service Providers

    /// Google Cloud Translation API
    case google = "google"

    /// DeepL Translation API
    case deepl = "deepl"

    /// Baidu Translation API
    case baidu = "baidu"

    // MARK: - Custom/Compatible

    /// Custom OpenAI-compatible endpoint
    case custom = "custom"

    var id: String { rawValue }

    /// Localized display name
    var localizedName: String {
        switch self {
        case .apple:
            return NSLocalizedString("translation.engine.apple", comment: "Apple Translation (Local)")
        case .mtranServer:
            return NSLocalizedString("translation.engine.mtran", comment: "MTranServer")
        case .openai:
            return NSLocalizedString("translation.engine.openai", comment: "OpenAI")
        case .claude:
            return NSLocalizedString("translation.engine.claude", comment: "Claude")
        case .gemini:
            return NSLocalizedString("translation.engine.gemini", comment: "Gemini")
        case .ollama:
            return NSLocalizedString("translation.engine.ollama", comment: "Ollama")
        case .google:
            return NSLocalizedString("translation.engine.google", comment: "Google Translate")
        case .deepl:
            return NSLocalizedString("translation.engine.deepl", comment: "DeepL")
        case .baidu:
            return NSLocalizedString("translation.engine.baidu", comment: "Baidu Translate")
        case .custom:
            return NSLocalizedString("translation.engine.custom", comment: "Custom")
        }
    }

    /// Description of the engine
    var engineDescription: String {
        switch self {
        case .apple:
            return NSLocalizedString(
                "translation.engine.apple.description",
                comment: "Built-in macOS translation, no setup required"
            )
        case .mtranServer:
            return NSLocalizedString(
                "translation.engine.mtran.description",
                comment: "Self-hosted translation server"
            )
        case .openai:
            return NSLocalizedString(
                "translation.engine.openai.description",
                comment: "GPT-4 translation via OpenAI API"
            )
        case .claude:
            return NSLocalizedString(
                "translation.engine.claude.description",
                comment: "Claude translation via Anthropic API"
            )
        case .gemini:
            return NSLocalizedString(
                "translation.engine.gemini.description",
                comment: "Gemini translation via Google AI API"
            )
        case .ollama:
            return NSLocalizedString(
                "translation.engine.ollama.description",
                comment: "Local LLM translation via Ollama"
            )
        case .google:
            return NSLocalizedString(
                "translation.engine.google.description",
                comment: "Google Cloud Translation API"
            )
        case .deepl:
            return NSLocalizedString(
                "translation.engine.deepl.description",
                comment: "High-quality translation via DeepL API"
            )
        case .baidu:
            return NSLocalizedString(
                "translation.engine.baidu.description",
                comment: "Baidu Translation API"
            )
        case .custom:
            return NSLocalizedString(
                "translation.engine.custom.description",
                comment: "Custom OpenAI-compatible endpoint"
            )
        }
    }

    /// Whether this engine is available (local engines are always available)
    var isAvailable: Bool {
        switch self {
        case .apple:
            return true
        case .mtranServer:
            // MTranServer requires external setup
            return MTranServerChecker.isAvailable
        default:
            // Other engines require configuration
            return true
        }
    }

    /// Whether this engine requires an API key
    var requiresAPIKey: Bool {
        switch self {
        case .apple, .mtranServer, .ollama:
            return false
        case .openai, .claude, .gemini, .google, .deepl, .custom:
            return true
        case .baidu:
            return true // Baidu requires both appID and secretKey
        }
    }

    /// Whether this engine requires an App ID (Baidu specific)
    var requiresAppID: Bool {
        switch self {
        case .baidu:
            return true
        default:
            return false
        }
    }

    /// Engine category for grouping
    var category: EngineCategory {
        switch self {
        case .apple, .mtranServer:
            return .builtIn
        case .openai, .claude, .gemini, .ollama:
            return .llm
        case .google, .deepl, .baidu:
            return .cloudService
        case .custom:
            return .compatible
        }
    }

    /// Default base URL for this engine (if applicable)
    var defaultBaseURL: String? {
        switch self {
        case .openai:
            return "https://api.openai.com/v1"
        case .claude:
            return "https://api.anthropic.com/v1"
        case .gemini:
            return "https://generativelanguage.googleapis.com/v1beta"
        case .ollama:
            return "http://localhost:11434"
        case .google:
            return "https://translation.googleapis.com/language/translate/v2"
        case .deepl:
            return "https://api.deepl.com/v2"
        case .baidu:
            return "https://fanyi-api.baidu.com/api/trans/vip/translate"
        default:
            return nil
        }
    }

    /// Default model name for LLM engines
    var defaultModelName: String? {
        switch self {
        case .openai:
            return "gpt-4o-mini"
        case .claude:
            return "claude-sonnet-4-20250514"
        case .gemini:
            return "gemini-2.0-flash"
        case .ollama:
            return "llama3"
        default:
            return nil
        }
    }

    /// URL to get API key for this engine
    var apiKeyURL: URL? {
        switch self {
        case .openai:
            return URL(string: "https://platform.openai.com/api-keys")
        case .claude:
            return URL(string: "https://console.anthropic.com/settings/keys")
        case .gemini:
            return URL(string: "https://aistudio.google.com/apikey")
        case .google:
            return URL(string: "https://console.cloud.google.com/apis/credentials")
        case .deepl:
            return URL(string: "https://www.deepl.com/pro-api?cta=header-pro-api")
        case .baidu:
            return URL(string: "https://fanyi-api.baidu.com/api/trans/product/desktop?req=developer")
        default:
            return nil
        }
    }
}

/// Engine category for grouping in UI
enum EngineCategory: String, CaseIterable, Sendable, Codable {
    case builtIn
    case llm
    case cloudService
    case compatible

    var localizedName: String {
        switch self {
        case .builtIn:
            return NSLocalizedString("engine.category.builtin", comment: "Built-in")
        case .llm:
            return NSLocalizedString("engine.category.llm", comment: "LLM Translation")
        case .cloudService:
            return NSLocalizedString("engine.category.cloud", comment: "Cloud Services")
        case .compatible:
            return NSLocalizedString("engine.category.compatible", comment: "Compatible")
        }
    }
}

// MARK: - MTranServer Availability Checker

/// Helper to check if MTranServer is available on the system
enum MTranServerChecker {
    private static let lock = NSLock()
    nonisolated(unsafe) private static var _isAvailable: Bool?

    /// Check if MTranServer is available (synchronous property)
    static var isAvailable: Bool {
        if let cached = lock.withLock({ _isAvailable }) {
            return cached
        }

        let result = checkMTranServer()
        lock.withLock { _isAvailable = result }
        return result
    }

    /// Check if MTranServer is available asynchronously without blocking calling threads
    static func checkAvailability() async -> Bool {
        if let cached = lock.withLock({ _isAvailable }) {
            return cached
        }

        let result = await checkMTranServerAsync()
        lock.withLock { _isAvailable = result }
        return result
    }

    private static func resolvedHostAndPort() -> (host: String, port: Int) {
        let defaults = UserDefaults.standard
        let prefix = "TransFrame."
        var host = defaults.string(forKey: prefix + "mtranServerHost") ?? "localhost"
        let port = defaults.object(forKey: prefix + "mtranServerPort") as? Int ?? 8989
        if host == "localhost" {
            host = "127.0.0.1"
        }
        return (host, port)
    }

    private static func checkMTranServerAsync() async -> Bool {
        let (host, port) = resolvedHostAndPort()
        let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "TransFrame", category: "MTranServerChecker")
        logger.info("Checking MTranServer async at \(host):\(port)")

        let endpoints = ["/health", "/", "/translate"]
        for endpoint in endpoints {
            var components = URLComponents()
            components.scheme = "http"
            components.host = host
            components.port = port
            components.path = endpoint

            guard let url = components.url else { continue }
            var request = URLRequest(url: url)
            request.timeoutInterval = 2.0
            request.httpMethod = "GET"

            do {
                let (_, response) = try await URLSession.shared.data(for: request)
                let statusCode = (response as? HTTPURLResponse)?.statusCode ?? -1
                if statusCode > 0 {
                    logger.info("MTranServer available via \(endpoint)")
                    return true
                }
            } catch {
                logger.debug("MTranServer check \(endpoint) failed: \(error.localizedDescription)")
            }
        }

        logger.info("MTranServer final availability: false")
        return false
    }

    private final class ResultBox: @unchecked Sendable {
        var value: Bool = false
    }

    private static func checkMTranServer() -> Bool {
        let (host, port) = resolvedHostAndPort()
        let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "TransFrame", category: "MTranServerChecker")
        logger.info("Checking MTranServer at \(host):\(port)")

        // Try multiple endpoints for health check
        let endpoints = ["/health", "/", "/translate"]
        var isAvailable = false

        for endpoint in endpoints {
            var components = URLComponents()
            components.scheme = "http"
            components.host = host
            components.port = port
            components.path = endpoint

            guard let url = components.url else { continue }

            var request = URLRequest(url: url)
            request.timeoutInterval = 2.0
            request.httpMethod = "GET"

            let semaphore = DispatchSemaphore(value: 0)
            let resultBox = ResultBox()

            let task = URLSession.shared.dataTask(with: request) { _, response, error in
                if let error = error {
                    logger.debug("MTranServer check \(endpoint) failed: \(error.localizedDescription)")
                }
                let statusCode = (response as? HTTPURLResponse)?.statusCode ?? -1
                logger.debug("MTranServer check \(endpoint): status \(statusCode)")
                resultBox.value = statusCode > 0
                semaphore.signal()
            }

            task.resume()
            _ = semaphore.wait(timeout: .now() + 2.5)

            if resultBox.value {
                isAvailable = true
                logger.info("MTranServer available via \(endpoint)")
                break
            }
        }

        logger.info("MTranServer final availability: \(isAvailable)")
        return isAvailable
    }

    /// Reset the cached availability check
    static func resetCache() {
        lock.withLock {
            _isAvailable = nil
        }
    }
}
