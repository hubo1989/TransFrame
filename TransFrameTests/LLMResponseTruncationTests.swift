import XCTest
@testable import TransFrame

final class LLMResponseTruncationTests: XCTestCase {
    func testOpenAIAndClaudeLengthResponsesAreRejected() throws {
        let openAI = Data(#"{"choices":[{"message":{"content":"部分译文"},"finish_reason":"length"}]}"#.utf8)
        let claude = Data(#"{"content":[{"type":"text","text":"partial"}],"stop_reason":"max_tokens"}"#.utf8)
        for (data, engine) in [(openAI, TranslationEngineType.openai), (claude, .claude)] {
            XCTAssertThrowsError(try LLMTranslationResponseParser.extractText(from: data, for: engine)) { error in
                guard case TranslationProviderError.incompleteResponse = error else {
                    return XCTFail("应为截断错误，实际 \(error)")
                }
            }
        }
    }

    func testOpenAISSETruncationCannotBeReportedAsSuccess() {
        let data = Data("data: {\"choices\":[{\"delta\":{\"content\":\"part\"},\"finish_reason\":null}]}\ndata: {\"choices\":[{\"delta\":{},\"finish_reason\":\"length\"}]}\ndata: [DONE]\n".utf8)
        XCTAssertThrowsError(try LLMTranslationResponseParser.extractText(from: data, for: .openai))
    }

    func testBatchingRespectsBudgetAndPreservesAllSourceText() {
        let texts = [String(repeating: "汉", count: 20), String(repeating: "字", count: 20), "third", "fourth"]
        let batches = LLMTranslationBatching.split(texts: texts, maxOutputTokens: 20, promptTemplate: nil)
        XCTAssertEqual(batches.flatMap { $0 }, texts)
        XCTAssertGreaterThan(batches.count, 1)
        XCTAssertEqual(batches.map { $0.joined(separator: "\n---\n") }.joined(separator: "\n---\n"), texts.joined(separator: "\n---\n"))
    }

    func testLongSingleSegmentIsSplitAndCanBeRecombined() {
        let text = String(repeating: "漢👨‍👩‍👧‍👦", count: 100)
        let batches = LLMTranslationBatching.split(texts: [text], maxOutputTokens: 32, promptTemplate: nil)
        XCTAssertGreaterThan(batches.count, 1)
        XCTAssertEqual(batches.flatMap { $0 }.joined(), text)
    }

    func testLongCustomPromptStillSplitsWithoutDroppingSource() {
        let texts = [String(repeating: "汉", count: 20), String(repeating: "字", count: 20)]
        let batches = LLMTranslationBatching.split(
            texts: texts,
            maxOutputTokens: 32,
            promptTemplate: String(repeating: "p", count: 120) + "{text}"
        )
        XCTAssertGreaterThanOrEqual(batches.count, 2)
        XCTAssertEqual(batches.flatMap { $0 }.joined(), texts.joined())
    }

    func testCompatibleBatchRequestsKeepOriginalSegments() async throws {
        let recorder = LLMFixtureRecorder()
        let sessionConfiguration = URLSessionConfiguration.ephemeral
        sessionConfiguration.protocolClasses = [LLMURLProtocol.self]
        let session = URLSession(configuration: sessionConfiguration)
        defer { session.invalidateAndCancel() }
        LLMURLProtocol.handler = { [recorder] request in
            recorder.append(request)
            return Data(#"{"choices":[{"message":{"content":"hello"},"finish_reason":"stop"}]}"#.utf8)
        }
        let id = UUID()
        let compatible = CompatibleTranslationProvider.CompatibleConfig(
            id: id, displayName: "fixture", baseURL: "http://fixture.invalid/v1", modelName: "fixture", hasAPIKey: false
        )
        var config = TranslationEngineConfig.default(for: .custom)
        config.options = EngineOptions(maxTokens: 28)
        let provider = try await CompatibleTranslationProvider(
            config: config, compatibleConfig: compatible, keychain: .shared, session: session
        )
        let texts = [String(repeating: "汉", count: 20), String(repeating: "字", count: 20)]
        let results = try await provider.translate(texts: texts, from: "zh", to: "en")
        XCTAssertEqual(results.count, 2)
        let requests = recorder.requests
        XCTAssertEqual(requests.count, 2)
        for (text, request) in zip(texts, requests) {
            let body = try XCTUnwrap(request.requestBody)
            let json = try JSONSerialization.jsonObject(with: body) as? [String: Any]
            let messages = json?["messages"] as? [[String: Any]]
            let prompt = messages?.first?["content"] as? String
            XCTAssertNotNil(messages)
            XCTAssertEqual(prompt?.contains(text), true)
        }
    }
}

private final class LLMFixtureRecorder: @unchecked Sendable {
    private let lock = NSLock()
    private var storage: [URLRequest] = []
    var requests: [URLRequest] { lock.withLock { storage } }
    func append(_ request: URLRequest) { lock.withLock { storage.append(request) } }
}

private final class LLMURLProtocol: URLProtocol {
    nonisolated(unsafe) static var handler: (URLRequest) -> Data = { _ in Data() }

    override class func canInit(with request: URLRequest) -> Bool {
        request.url?.host == "fixture.invalid"
    }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func startLoading() {
        let response = HTTPURLResponse(url: request.url!, statusCode: 200, httpVersion: nil, headerFields: nil)!
        client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: Self.handler(request))
        client?.urlProtocolDidFinishLoading(self)
    }
    override func stopLoading() {}
}
