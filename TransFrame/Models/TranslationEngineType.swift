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
            return "https://api.deepl.com/v2/translate"
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
final class MTranServerChecker: @unchecked Sendable {
    static let shared = MTranServerChecker()

    private struct EndpointKey: Hashable, Sendable {
        let host: String
        let port: Int
    }

    private struct CacheEntry {
        let isAvailable: Bool
        let checkedAt: TimeInterval
    }

    private struct InFlightProbe {
        let token: UInt64
        let task: Task<Bool?, Never>
    }

    private let lock = NSLock()
    private let session: URLSession
    private let cacheTTL: TimeInterval
    private let now: @Sendable () -> TimeInterval
    private let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "TransFrame", category: "MTranServerChecker")
    private var cachedResults: [EndpointKey: CacheEntry] = [:]
    private var inFlightProbes: [EndpointKey: InFlightProbe] = [:]
    private var nextToken: UInt64 = 0

    init(
        session: URLSession = .shared,
        cacheTTL: TimeInterval = 5,
        now: @escaping @Sendable () -> TimeInterval = { ProcessInfo.processInfo.systemUptime }
    ) {
        self.session = session
        self.cacheTTL = cacheTTL
        self.now = now
    }

    /// Check if MTranServer is available (synchronous property, never performs network I/O).
    /// Cached-only snapshot: returns false whenever no fresh (TTL-valid) probe
    /// exists; use `checkAvailability(...)` for an authoritative answer.
    static var isAvailable: Bool { shared.isAvailable }

    var isAvailable: Bool { cachedAvailability(for: .default) }

    func cachedAvailability(
        for configuration: MTranServerEngine.Configuration = .default
    ) -> Bool {
        guard let key = endpointKey(for: configuration) else { return false }
        let currentTime = now()
        return lock.withLock {
            guard let entry = cachedResults[key],
                  currentTime - entry.checkedAt < cacheTTL else { return false }
            return entry.isAvailable
        }
    }

    /// Check if MTranServer is available asynchronously without blocking calling threads
    static func checkAvailability(
        configuration: MTranServerEngine.Configuration = .default,
        forceRefresh: Bool = false
    ) async throws -> Bool {
        try await shared.checkAvailability(configuration: configuration, forceRefresh: forceRefresh)
    }

    func checkAvailability(
        configuration: MTranServerEngine.Configuration = .default,
        forceRefresh: Bool = false
    ) async throws -> Bool {
        try Task.checkCancellation()
        guard let key = endpointKey(for: configuration) else { return false }
        let currentTime = now()
        let probe: InFlightProbe? = lock.withLock {
            if !forceRefresh,
               let entry = cachedResults[key],
               currentTime - entry.checkedAt < cacheTTL {
                return nil
            }
            if let existing = inFlightProbes[key] { return existing }
            nextToken &+= 1
            let token = nextToken
            let task = Task { [session, logger] in
                await Self.probeEndpoint(configuration: configuration, session: session, logger: logger)
            }
            let created = InFlightProbe(token: token, task: task)
            inFlightProbes[key] = created
            return created
        }
        guard let probe else { return cachedAvailability(for: configuration) }
        let result = try await withTaskCancellationHandler {
            try Task.checkCancellation()
            return await probe.task.value
        } onCancel: {
            probe.task.cancel()
        }
        try Task.checkCancellation()
        lock.withLock {
            guard inFlightProbes[key]?.token == probe.token else { return }
            inFlightProbes.removeValue(forKey: key)
            if let result {
                cachedResults[key] = CacheEntry(isAvailable: result, checkedAt: now())
            }
        }
        return result ?? false
    }

    static func resetCache() { shared.resetCache() }

    func resetCache() {
        let probes = lock.withLock {
            nextToken &+= 1
            cachedResults.removeAll()
            defer { inFlightProbes.removeAll() }
            return Array(inFlightProbes.values)
        }
        probes.forEach { $0.task.cancel() }
    }

    private func endpointKey(
        for configuration: MTranServerEngine.Configuration
    ) -> EndpointKey? {
        guard let host = configuration.endpointHost else { return nil }
        return EndpointKey(host: host.lowercased(), port: configuration.serverPort)
    }

    private static func probeEndpoint(
        configuration: MTranServerEngine.Configuration,
        session: URLSession,
        logger: Logger
    ) async -> Bool? {
        let endpoints = ["/health", "/", "/translate"]
        for endpoint in endpoints {
            if Task.isCancelled { return nil }
            guard let url = configuration.endpointURL(path: endpoint) else { return false }
            var request = URLRequest(url: url)
            request.timeoutInterval = min(configuration.timeout, 2.0)
            request.httpMethod = "GET"

            do {
                let (_, response) = try await session.data(for: request)
                guard let statusCode = (response as? HTTPURLResponse)?.statusCode else { return false }
                if (200..<300).contains(statusCode) {
                    logger.info("MTranServer available via \(endpoint)")
                    return true
                }
                if endpoint != "/translate" {
                    // 404/405/501: endpoint missing. 401/403/429: a gateway in
                    // front may only guard some paths while /translate works.
                    // Other statuses (5xx server errors) fail fast — the
                    // server itself is unhealthy and /translate won't be well.
                    if [401, 403, 404, 405, 429, 501].contains(statusCode) {
                        continue
                    }
                }
                return false
            } catch is CancellationError {
                return nil
            } catch let error as URLError where error.code == .cancelled {
                return nil
            } catch {
                logger.debug("MTranServer check \(endpoint) failed: \(error.localizedDescription)")
                return false
            }
        }
        return false
    }
}
