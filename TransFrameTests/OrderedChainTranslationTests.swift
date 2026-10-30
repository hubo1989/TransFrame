import XCTest
@testable import TransFrame

/// Ordered fallback chain (V2) service tests — PRD US-001, plan Phase 3
@available(macOS 13.0, *)
final class OrderedChainTranslationTests: XCTestCase {

    private func makeResult(_ source: String, _ translated: String) -> TranslationResult {
        TranslationResult(
            sourceText: source,
            translatedText: translated,
            sourceLanguage: "English",
            targetLanguage: "Chinese"
        )
    }

    private func makeProvider(
        _ id: String,
        results: [TranslationResult] = [],
        error: Error? = nil,
        available: Bool = true
    ) -> MockTranslationProvider {
        MockTranslationProvider(
            id: id,
            name: id,
            available: available,
            batchResults: results,
            translateError: error
        )
    }

    // MARK: - Sequential fallback

    func testFirstFailureFallsThroughToSecondEngine() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        await registry.register(
            makeProvider("a", error: TranslationProviderError.notAvailable),
            for: .apple
        )
        await registry.register(
            makeProvider("b", results: [makeResult("Hello", "你好")]),
            for: .mtranServer
        )
        let service = TranslationService(registry: registry)

        let bundle = try await service.translateOrderedV2(
            segments: ["Hello"],
            to: "zh-Hans",
            chain: [.builtin(.apple), .builtin(.mtranServer)]
        )

        XCTAssertEqual(bundle.primaryEngine, .mtranServer)
        XCTAssertEqual(bundle.results.count, 2) // one failure + one success
        XCTAssertTrue(bundle.successfulEngines.contains(.mtranServer))
    }

    func testFirstSuccessStopsTheChain() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        let apple = makeProvider("apple", results: [makeResult("Hello", "你好")])
        let fallback = makeProvider("mtran", results: [makeResult("Hello", "你好(备用)")])
        await registry.register(apple, for: .apple)
        await registry.register(fallback, for: .mtranServer)
        let service = TranslationService(registry: registry)

        _ = try await service.translateOrderedV2(
            segments: ["Hello"],
            to: "zh-Hans",
            chain: [.builtin(.apple), .builtin(.mtranServer)]
        )

        let fallbackRequests = await fallback.requestCount()
        XCTAssertEqual(fallbackRequests, 0, "chain must stop at first success")
    }

    func testUnavailableMemberIsSkippedNotFatal() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        await registry.register(
            makeProvider("dead", available: false),
            for: .apple
        )
        await registry.register(
            makeProvider("live", results: [makeResult("Hello", "你好")]),
            for: .mtranServer
        )
        let service = TranslationService(registry: registry)

        let bundle = try await service.translateOrderedV2(
            segments: ["Hello"],
            to: "zh-Hans",
            chain: [.builtin(.apple), .builtin(.mtranServer)]
        )
        XCTAssertEqual(bundle.primaryEngine, .mtranServer)
    }

    func testAllFailuresThrowAggregatedError() async {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        await registry.register(
            makeProvider("a", error: TranslationProviderError.notAvailable),
            for: .apple
        )
        await registry.register(
            makeProvider("b", error: TranslationProviderError.translationFailed("boom")),
            for: .mtranServer
        )
        let service = TranslationService(registry: registry)

        do {
            _ = try await service.translateOrderedV2(
                segments: ["Hello"],
                to: "zh-Hans",
                chain: [.builtin(.apple), .builtin(.mtranServer)]
            )
            XCTFail("expected allEnginesFailed")
        } catch let error as MultiEngineError {
            guard case .allEnginesFailed(let errors) = error else {
                return XCTFail("wrong error: \(error)")
            }
            XCTAssertEqual(errors.count, 2, "error count must equal chain length")
        } catch {
            XCTFail("unexpected error type: \(error)")
        }
    }

    func testEmptyChainFallsBackToApple() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        await registry.register(
            makeProvider("apple", results: [makeResult("Hello", "你好")]),
            for: .apple
        )
        let service = TranslationService(registry: registry)

        let bundle = try await service.translateOrderedV2(
            segments: ["Hello"],
            to: "zh-Hans",
            chain: []
        )
        XCTAssertEqual(bundle.primaryEngine, .apple)
    }

    // MARK: - Compatible instance entries

    func testDanglingCompatibleUUIDIsSkippedAndNextEngineUsed() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        await registry.register(
            makeProvider("mtran", results: [makeResult("Hello", "你好")]),
            for: .mtranServer
        )
        let service = TranslationService(registry: registry)

        let bundle = try await service.translateOrderedV2(
            segments: ["Hello"],
            to: "zh-Hans",
            chain: [.compatible(UUID()), .builtin(.mtranServer)]
        )
        XCTAssertEqual(bundle.primaryEngine, .mtranServer)
    }

    // MARK: - Chain item helpers

    func testResolvedEngineSurfacesBuiltinAndCustom() {
        XCTAssertEqual(EngineChainItem.builtin(.apple).resolvedEngine, .apple)
        XCTAssertEqual(EngineChainItem.compatible(UUID()).resolvedEngine, .custom)
    }
}
