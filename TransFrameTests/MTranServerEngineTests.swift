import Foundation
import XCTest
@testable import TransFrame

@MainActor
final class MTranServerEngineTests: XCTestCase {
    private var session: URLSession!
    private var checker: MTranServerChecker!

    override func setUp() async throws {
        try await super.setUp()
        MTranTestURLProtocol.reset()
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [MTranTestURLProtocol.Handler.self]
        session = URLSession(configuration: configuration)
        checker = MTranServerChecker(session: session)
    }

    override func tearDown() async throws {
        session.invalidateAndCancel()
        MTranTestURLProtocol.reset()
        try await super.tearDown()
    }

    func testSingleTranslationSendsOnlyOnePOST() async throws {
        let result = try await makeEngine().translate("hello", from: "en", to: "zh")

        XCTAssertEqual(result.translatedText, "你好")
        XCTAssertEqual(MTranTestURLProtocol.requests.map(\.httpMethod), ["POST"])
        XCTAssertEqual(MTranTestURLProtocol.requests.first?.url?.host, "mtran.test")
    }

    func testBatchSendsOnePOSTPerSegmentWithoutHealthChecks() async throws {
        let results = try await makeEngine().translate(texts: ["one", "two", "three"], from: "en", to: "zh")

        XCTAssertEqual(results.count, 3)
        XCTAssertEqual(MTranTestURLProtocol.requests.map(\.httpMethod), ["POST", "POST", "POST"])
    }

    func testProviderAvailabilityOnlyValidatesConfiguration() async {
        let available = await makeEngine().isAvailable
        let invalid = MTranServerEngine(
            configuration: makeConfig(port: 0), session: session, checker: checker
        )

        let invalidAvailability = await invalid.isAvailable
        XCTAssertTrue(available)
        XCTAssertFalse(invalidAvailability)
        XCTAssertTrue(MTranTestURLProtocol.requests.isEmpty)
    }

    func testInvalidTranslationConfigurationDoesNotRequestOrCrash() async {
        let engine = MTranServerEngine(
            configuration: makeConfig(timeout: -1), session: session, checker: checker
        )

        await assertThrows(MTranServerError.invalidURL) {
            _ = try await engine.translate("hello", from: "en", to: "zh")
        }
        XCTAssertTrue(MTranTestURLProtocol.requests.isEmpty)
    }

    func testSynchronousAvailabilityDoesNotPerformNetworkRequests() {
        XCTAssertFalse(checker.isAvailable)
        XCTAssertTrue(MTranTestURLProtocol.requests.isEmpty)
    }

    func testCachedNegativeHealthCheckDoesNotBlockRecoveredTranslation() async throws {
        MTranTestURLProtocol.defaultStatusCode = 500
        let available = try await checker.checkAvailability(configuration: makeConfig())
        XCTAssertFalse(available)

        MTranTestURLProtocol.defaultStatusCode = 200
        let result = try await makeEngine().translate("hello", from: "en", to: "zh")

        XCTAssertEqual(result.translatedText, "你好")
        XCTAssertEqual(MTranTestURLProtocol.requests.map(\.httpMethod), ["GET", "POST"])
    }

    func testHealthHTTP500IsUnavailableWithoutFallback() async throws {
        MTranTestURLProtocol.defaultStatusCode = 500

        let available = try await checker.checkAvailability(configuration: makeConfig())
        XCTAssertFalse(available)
        XCTAssertEqual(MTranTestURLProtocol.requests.map(\.url?.path), ["/health"])
    }

    /// 网关只拦截 /health 与 /（401），/translate 正常时仍应判可用。
    func testHealthUnauthorizedFallsThroughToTranslate() async throws {
        MTranTestURLProtocol.statusCodes = ["/health": 401, "/": 403]

        let available = try await checker.checkAvailability(configuration: makeConfig())
        XCTAssertTrue(available)
        XCTAssertEqual(MTranTestURLProtocol.requests.map(\.url?.path), ["/health", "/", "/translate"])
    }

    func testHealthNotFoundFallsBackToRoot() async throws {
        MTranTestURLProtocol.statusCodes = ["/health": 404]

        let available = try await checker.checkAvailability(configuration: makeConfig())
        XCTAssertTrue(available)
        XCTAssertEqual(MTranTestURLProtocol.requests.map(\.url?.path), ["/health", "/"])
    }

    func testTranslateMethodNotAllowedIsNotAvailable() async throws {
        MTranTestURLProtocol.statusCodes = ["/health": 404, "/": 404, "/translate": 405]

        let available = try await checker.checkAvailability(configuration: makeConfig())
        XCTAssertFalse(available)
        XCTAssertEqual(MTranTestURLProtocol.requests.map(\.url?.path), ["/health", "/", "/translate"])
    }

    func testTranslateSuccessIsAValidLegacyHealthResponse() async throws {
        MTranTestURLProtocol.statusCodes = ["/health": 404, "/": 404]

        let available = try await checker.checkAvailability(configuration: makeConfig())
        XCTAssertTrue(available)
        XCTAssertEqual(MTranTestURLProtocol.requests.map(\.url?.path), ["/health", "/", "/translate"])
    }

    func testAvailabilityCacheUsesEndpointTTLAndForceRefresh() async throws {
        let clock = MTranTestClock()
        let checker = MTranServerChecker(session: session, cacheTTL: 5, now: { clock.now })
        let config = makeConfig()

        let initial = try await checker.checkAvailability(configuration: config)
        let cached = try await checker.checkAvailability(configuration: config)
        XCTAssertTrue(initial && cached)
        XCTAssertEqual(MTranTestURLProtocol.requests.count, 1)

        clock.advance(by: 6)
        let expired = try await checker.checkAvailability(configuration: config)
        let forced = try await checker.checkAvailability(configuration: config, forceRefresh: true)
        XCTAssertTrue(expired && forced)
        XCTAssertEqual(MTranTestURLProtocol.requests.count, 3)
    }

    func testAvailabilityCacheSeparatesEndpoints() async throws {
        MTranTestURLProtocol.statusCodes = ["/health": 404]
        let first = makeConfig()
        let second = makeConfig(host: "other.test", port: 8990)

        let firstAvailable = try await checker.checkAvailability(configuration: first)
        let secondAvailable = try await checker.checkAvailability(configuration: second)
        XCTAssertTrue(firstAvailable && secondAvailable)
        XCTAssertEqual(MTranTestURLProtocol.requests.count, 4)
        XCTAssertTrue(checker.cachedAvailability(for: first))
        XCTAssertTrue(checker.cachedAvailability(for: second))
    }

    func testLateOldEndpointResultDoesNotOverwriteNewEndpoint() async throws {
        let gate = MTranTestGate()
        let oldConfig = makeConfig(host: "old.test")
        let newConfig = makeConfig(host: "new.test")
        MTranTestURLProtocol.requestHandler = { request in
            if request.url?.host == "old.test" {
                await gate.wait()
                return MTranTestURLProtocol.success(statusCode: 200, request: request)
            }
            return MTranTestURLProtocol.success(statusCode: 500, request: request)
        }

        let checker = self.checker!
        async let oldAvailability = checker.checkAvailability(configuration: oldConfig)
        await waitForRequests(count: 1)
        let newAvailable = try await checker.checkAvailability(configuration: newConfig)
        gate.open()
        XCTAssertFalse(newAvailable)
        let oldResult = try await oldAvailability
        XCTAssertTrue(oldResult)

        XCTAssertFalse(checker.cachedAvailability(for: newConfig))
        XCTAssertTrue(checker.cachedAvailability(for: oldConfig))
    }

    func testConcurrentChecksShareOneHealthRequest() async throws {
        let gate = MTranTestGate()
        MTranTestURLProtocol.requestHandler = { request in
            await gate.wait()
            return MTranTestURLProtocol.success(statusCode: 200, request: request)
        }
        let config = makeConfig()
        let checker = self.checker!

        async let first = checker.checkAvailability(configuration: config)
        async let second = checker.checkAvailability(configuration: config)
        async let third = checker.checkAvailability(configuration: config, forceRefresh: true)
        await waitForRequests(count: 1)
        gate.open()

        let results = try await [first, second, third]
        XCTAssertEqual(results, [true, true, true])
        XCTAssertEqual(MTranTestURLProtocol.requests.count, 1)
    }

    func testVerifyConnectionForcesHealthRefresh() async throws {
        let engine = makeEngine()
        try await engine.verifyConnection()
        try await engine.verifyConnection()

        XCTAssertEqual(MTranTestURLProtocol.requests.map(\.httpMethod), ["GET", "GET"])
    }

    func testTranslationPropagatesCancellation() async {
        let started = expectation(description: "request started")
        let stopped = expectation(description: "request cancelled")
        MTranTestURLProtocol.requestHandler = { request in
            started.fulfill()
            try await Task.sleep(nanoseconds: 10_000_000_000)
            return MTranTestURLProtocol.success(statusCode: 200, request: request)
        }
        MTranTestURLProtocol.onStop = { stopped.fulfill() }
        let engine = makeEngine()
        let task = Task {
            try await engine.translate("hello", from: "en", to: "zh")
        }

        await fulfillment(of: [started], timeout: 1)
        task.cancel()
        await fulfillment(of: [stopped], timeout: 1)
        await assertThrows(CancellationError()) { _ = try await task.value }
    }

    func testTranslationAppliesConfiguredTimeout() async throws {
        _ = try await makeEngine(configuration: makeConfig(timeout: 0.25))
            .translate("hello", from: "en", to: "zh")

        XCTAssertEqual(MTranTestURLProtocol.requests.first?.timeoutInterval, 0.25)
    }

    func testIPv6ConfigurationBuildsBracketedURL() async throws {
        _ = try await makeEngine(configuration: makeConfig(host: "[::1]"))
            .translate("hello", from: "en", to: "zh")

        XCTAssertEqual(MTranTestURLProtocol.requests.first?.url?.absoluteString, "http://[::1]:8989/translate")
    }

    func testHealthCancellationPropagatesWithoutCaching() async {
        let gate = MTranTestGate()
        MTranTestURLProtocol.requestHandler = { request in
            await gate.wait()
            return MTranTestURLProtocol.success(statusCode: 500, request: request)
        }
        let config = makeConfig()
        let task = Task { [checker] in
            try await checker?.checkAvailability(configuration: config, forceRefresh: true)
        }

        await waitForRequests(count: 1)
        task.cancel()
        do {
            _ = try await task.value
            XCTFail("Expected cancellation")
        } catch is CancellationError {
            // Expected cancellation, not a false health result.
        } catch {
            XCTFail("Expected cancellation, got \(error)")
        }
        gate.open()

        XCTAssertFalse(checker.cachedAvailability(for: config))
    }

    func testServerURLConfigurationPreservesIPv6AndRejectsPaths() {
        let config = MTranServerEngine.Configuration(serverURL: "http://[::1]:8989")

        XCTAssertEqual(config?.serverAddress, "[::1]")
        XCTAssertEqual(config?.serverPort, 8989)
        XCTAssertNil(MTranServerEngine.Configuration(serverURL: "http://localhost:8989/api"))
        XCTAssertNil(MTranServerEngine.Configuration(serverURL: "http://[bad:host]:8989"))
    }

    /// 残留非法 host（空格/非 ASCII）不得触发 percentEncodedHost 的
    /// 不可捕获 NSInvalidArgumentException：endpointHost 返回 nil，配置判无效。
    func testIllegalHostnameIsRejectedNotCrashing() {
        for badHost in ["my host.example", "服务器.本地", "host\nevil"] {
            let config = MTranServerEngine.Configuration(
                serverAddress: badHost, serverPort: 8989, timeout: 10.0, autoDetectSourceLanguage: true
            )
            XCTAssertNil(config.endpointHost, "host 必须被拒绝: \(String(describing: badHost))")
            XCTAssertFalse(config.isValid)
        }
        // 合法 host 仍通过
        let valid = MTranServerEngine.Configuration(
            serverAddress: "192.168.1.10", serverPort: 8989, timeout: 10.0, autoDetectSourceLanguage: true
        )
        XCTAssertEqual(valid.endpointHost, "192.168.1.10")
    }

    func testEngineOptionsConfigureProviderEndpointAndTimeout() async {
        let engine = MTranServerEngine.provider(config: TranslationEngineConfig(
            id: .mtranServer,
            isEnabled: true,
            options: EngineOptions(baseURL: "http://mtran.test:8990", timeout: 2.5)
        ))
        XCTAssertEqual(engine.configuration, MTranServerEngine.Configuration(
            serverAddress: "mtran.test", serverPort: 8990, timeout: 2.5,
            autoDetectSourceLanguage: true
        ))
    }

    private func makeConfig(
        host: String = "mtran.test", port: Int = 8989, timeout: TimeInterval = 1
    ) -> MTranServerEngine.Configuration {
        .init(
            serverAddress: host, serverPort: port, timeout: timeout,
            autoDetectSourceLanguage: true
        )
    }

    private func makeEngine(
        configuration: MTranServerEngine.Configuration? = nil
    ) -> MTranServerEngine {
        MTranServerEngine(
            configuration: configuration ?? makeConfig(), session: session, checker: checker
        )
    }

    private func waitForRequests(count: Int) async {
        // Poll with real suspension: Task.yield() never lets URLSession's
        // networking threads run, so under load the old 100-iteration loop
        // spuriously timed out before the request was even issued.
        for _ in 0..<500 {
            if MTranTestURLProtocol.requests.count == count { return }
            try? await Task.sleep(nanoseconds: 10_000_000) // 10ms
        }
        XCTFail("Expected \(count) requests, got \(MTranTestURLProtocol.requests.count)")
    }

    private func assertThrows(
        _ expected: MTranServerError, operation: @escaping () async throws -> Void
    ) async {
        do {
            try await operation()
            XCTFail("Expected \(expected)")
        } catch let error as MTranServerError {
            switch (error, expected) {
            case (.invalidURL, .invalidURL), (.timeout, .timeout):
                break
            default:
                XCTFail("Expected \(expected), got \(error)")
            }
        } catch {
            XCTFail("Expected \(expected), got \(error)")
        }
    }

    private func assertThrows(
        _ expected: CancellationError, operation: @escaping () async throws -> Void
    ) async {
        do {
            try await operation()
            XCTFail("Expected \(expected)")
        } catch is CancellationError {
            // Expected cancellation from the request, not a generic unavailable error.
        } catch {
            XCTFail("Expected \(expected), got \(error)")
        }
    }
}

private enum MTranTestURLProtocol {
    static let lock = NSLock()
    nonisolated(unsafe) private static var recordedRequests: [URLRequest] = []
    nonisolated(unsafe) static var defaultStatusCode = 200
    nonisolated(unsafe) static var statusCodes: [String: Int] = [:]
    nonisolated(unsafe) static var onStop: @Sendable () -> Void = {}
    nonisolated(unsafe) static var requestHandler: (@Sendable (URLRequest) async throws -> (Data, URLResponse))?

    static var requests: [URLRequest] { lock.withLock { recordedRequests } }

    static func reset() {
        lock.withLock {
            recordedRequests = []
            defaultStatusCode = 200
            statusCodes = [:]
            onStop = {}
            requestHandler = nil
        }
    }

    static func success(statusCode: Int, request: URLRequest) -> (Data, URLResponse) {
        (
            Data(#"{"translated_text":"你好","detected_language":"en"}"#.utf8),
            HTTPURLResponse(url: request.url!, statusCode: statusCode, httpVersion: nil, headerFields: nil)!
        )
    }

    private struct UncheckedContext: @unchecked Sendable {
        let handler: Handler
        let client: (any URLProtocolClient)?
    }

    final class Handler: URLProtocol {
        private let taskLock = NSLock()
        nonisolated(unsafe) private var loadingTask: Task<Void, Never>?

        override class func canInit(with request: URLRequest) -> Bool { true }
        override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

        override func startLoading() {
            let context = UncheckedContext(handler: self, client: client)
            let task = Task { [request, context] in
                do {
                    MTranTestURLProtocol.lock.withLock {
                        MTranTestURLProtocol.recordedRequests.append(request)
                    }
                    let (data, response): (Data, URLResponse)
                    if let handler = MTranTestURLProtocol.requestHandler {
                        (data, response) = try await handler(request)
                    } else {
                        let statusCode = MTranTestURLProtocol.statusCodes[request.url?.path ?? ""]
                            ?? MTranTestURLProtocol.defaultStatusCode
                        (data, response) = MTranTestURLProtocol.success(
                            statusCode: statusCode, request: request
                        )
                    }
                    try Task.checkCancellation()
                    context.client?.urlProtocol(context.handler, didReceive: response, cacheStoragePolicy: .notAllowed)
                    context.client?.urlProtocol(context.handler, didLoad: data)
                    context.client?.urlProtocolDidFinishLoading(context.handler)
                } catch {
                    context.client?.urlProtocol(context.handler, didFailWithError: error)
                }
            }
            taskLock.withLock { loadingTask = task }
        }

        override func stopLoading() {
            taskLock.withLock {
                loadingTask?.cancel()
                loadingTask = nil
            }
            MTranTestURLProtocol.onStop()
        }
    }
}

private final class MTranTestGate: @unchecked Sendable {
    private let lock = NSLock()
    private var continuations: [CheckedContinuation<Void, Never>] = []
    private var isOpen = false

    func wait() async {
        await withCheckedContinuation { continuation in
            let resumed = lock.withLock {
                if isOpen { return true }
                continuations.append(continuation)
                return false
            }
            if resumed { continuation.resume() }
        }
    }

    func open() {
        let pending = lock.withLock {
            isOpen = true
            defer { continuations.removeAll() }
            return continuations
        }
        pending.forEach { $0.resume() }
    }
}

private final class MTranTestClock: @unchecked Sendable {
    private let lock = NSLock()
    private var time: TimeInterval = 0
    var now: TimeInterval { lock.withLock { time } }
    func advance(by interval: TimeInterval) { lock.withLock { time += interval } }
}
