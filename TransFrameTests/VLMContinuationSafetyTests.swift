import CoreGraphics
import XCTest
@testable import TransFrame

final class VLMContinuationSafetyTests: XCTestCase {
    func testOpenAIContinuationKeepsImageContextAndExhaustionFails() async throws {
        let image = Self.makeImage()
        let requests = VLMRequestBox()
        let sessionConfiguration = URLSessionConfiguration.ephemeral
        sessionConfiguration.protocolClasses = [VLMContinuationURLProtocol.self]
        let session = URLSession(configuration: sessionConfiguration)
        defer { session.invalidateAndCancel() }
        VLMContinuationURLProtocol.handler = { request in
            requests.append(request)
            return Data(#"{"choices":[{"message":{"content":"{\"segments\":[{\"text\":\"A\",\"boundingBox\":{\"x\":0.1,\"y\":0.1,\"width\":0.2,\"height\":0.1},\"confidence\":0.9}]}"},"finish_reason":"length"}]}"#.utf8)
        }
        let provider = OpenAIVLMProvider(apiKey: "test", baseURL: URL(string: "http://vlm.invalid/v1")!, session: session)
        do {
            _ = try await provider.analyze(image: image)
            XCTFail("续写耗尽不能把部分结果当完整结果返回")
        } catch { }
        XCTAssertEqual(requests.items.count, 3)
        for request in requests.items {
            let body = try XCTUnwrap(request.requestBody)
            let text = try XCTUnwrap(String(data: body, encoding: .utf8)).replacingOccurrences(of: "\\/", with: "/")
            XCTAssertTrue(text.contains("data:image/jpeg;base64,"), "每轮续写都必须保留原图上下文")
            XCTAssertFalse(text.contains("image omitted"))
        }
    }

    func testClaudeContinuationKeepsImageContextAndExhaustionFails() async throws {
        let image = Self.makeImage()
        let requests = VLMRequestBox()
        let sessionConfiguration = URLSessionConfiguration.ephemeral
        sessionConfiguration.protocolClasses = [VLMContinuationURLProtocol.self]
        let session = URLSession(configuration: sessionConfiguration)
        defer { session.invalidateAndCancel() }
        VLMContinuationURLProtocol.handler = { request in
            requests.append(request)
            return Data(#"{"content":[{"type":"text","text":"{\"segments\":[{\"text\":\"A\",\"boundingBox\":{\"x\":0.1,\"y\":0.1,\"width\":0.2,\"height\":0.1},\"confidence\":0.9}]}"}],"stop_reason":"max_tokens"}"#.utf8)
        }
        let provider = ClaudeVLMProvider(apiKey: "test", baseURL: URL(string: "http://vlm.invalid")!, session: session)
        do {
            _ = try await provider.analyze(image: image)
            XCTFail("Claude 续写耗尽不能把部分结果当完整结果返回")
        } catch { }
        XCTAssertEqual(requests.items.count, 3)
        for request in requests.items {
            let body = try XCTUnwrap(request.requestBody)
            let text = try XCTUnwrap(String(data: body, encoding: .utf8))
            XCTAssertTrue(text.contains("\"type\":\"base64\""))
            XCTAssertFalse(text.contains("image omitted"))
        }
    }

    private static func makeImage() -> CGImage {
        let context = CGContext(
            data: nil, width: 32, height: 32, bitsPerComponent: 8,
            bytesPerRow: 0, space: CGColorSpace(name: CGColorSpace.sRGB)!,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        )!
        context.setFillColor(CGColor(red: 0.2, green: 0.4, blue: 0.6, alpha: 1))
        context.fill(CGRect(x: 0, y: 0, width: 32, height: 32))
        return context.makeImage()!
    }
}

private final class VLMRequestBox: @unchecked Sendable {
    private let lock = NSLock()
    private var storage: [URLRequest] = []
    var items: [URLRequest] { lock.withLock { storage } }
    func append(_ request: URLRequest) { lock.withLock { storage.append(request) } }
}

private final class VLMContinuationURLProtocol: URLProtocol {
    nonisolated(unsafe) static var handler: (URLRequest) -> Data = { _ in Data() }
    override class func canInit(with request: URLRequest) -> Bool { request.url?.host == "vlm.invalid" }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func startLoading() {
        let response = HTTPURLResponse(url: request.url!, statusCode: 200, httpVersion: nil, headerFields: nil)!
        client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: Self.handler(request))
        client?.urlProtocolDidFinishLoading(self)
    }
    override func stopLoading() {}
}
