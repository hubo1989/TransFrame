import XCTest
@testable import TransFrame

/// Compare mode (V2) service tests — PRD US-002, plan Phase 4
@available(macOS 13.0, *)
final class CompareModeTranslationTests: XCTestCase {

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
        error: Error? = nil
    ) -> MockTranslationProvider {
        MockTranslationProvider(
            id: id,
            name: id,
            batchResults: results,
            translateError: error
        )
    }

    func testParallelExecutionReturnsOneResultPerEngine() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        await registry.register(
            makeProvider("apple", results: [makeResult("Hello", "你好(Apple)")]),
            for: .apple
        )
        await registry.register(
            makeProvider("mtran", results: [makeResult("Hello", "你好(MTran)")]),
            for: .mtranServer
        )
        let service = TranslationService(registry: registry)

        let bundle = try await service.translateCompareV2(
            segments: ["Hello"],
            to: "zh-Hans",
            engines: [.builtin(.apple), .builtin(.mtranServer)]
        )

        XCTAssertEqual(bundle.results.count, 2)
        XCTAssertTrue(bundle.successfulEngines.contains(.apple))
        XCTAssertTrue(bundle.successfulEngines.contains(.mtranServer))
    }

    func testOneFailureAmongSuccessesStillSucceeds() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        await registry.register(
            makeProvider("dead", error: TranslationProviderError.notAvailable),
            for: .apple
        )
        await registry.register(
            makeProvider("live", results: [makeResult("Hello", "你好")]),
            for: .mtranServer
        )
        let service = TranslationService(registry: registry)

        let bundle = try await service.translateCompareV2(
            segments: ["Hello"],
            to: "zh-Hans",
            engines: [.builtin(.apple), .builtin(.mtranServer)]
        )

        XCTAssertEqual(bundle.results.count, 2) // failure detail preserved
        XCTAssertTrue(bundle.successfulEngines.contains(.mtranServer))
    }

    func testAllFailuresThrowAggregatedErrorWithEngineIdentity() async {
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
            _ = try await service.translateCompareV2(
                segments: ["Hello"],
                to: "zh-Hans",
                engines: [.builtin(.apple), .builtin(.mtranServer)]
            )
            XCTFail("expected allEnginesFailed")
        } catch let error as MultiEngineError {
            guard case .allEnginesFailed(let errors) = error else {
                return XCTFail("wrong error: \(error)")
            }
            XCTAssertEqual(errors.count, 2)
            // Engine identity preserved in the aggregated message
            let message = errors.map { $0.localizedDescription }.joined(separator: "; ")
            XCTAssertTrue(message.contains("Apple") || message.contains("MTranServer"),
                          "aggregated message should name engines: \(message)")
        } catch {
            XCTFail("unexpected error type: \(error)")
        }
    }

    func testResultsPreserveSelectionOrder() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        await registry.register(
            makeProvider("apple", results: [makeResult("Hello", "你好")]),
            for: .apple
        )
        await registry.register(
            makeProvider("mtran", results: [makeResult("Hello", "你好")]),
            for: .mtranServer
        )
        let service = TranslationService(registry: registry)

        let bundle = try await service.translateCompareV2(
            segments: ["Hello"],
            to: "zh-Hans",
            engines: [.builtin(.mtranServer), .builtin(.apple)] // mtran first
        )

        XCTAssertEqual(
            bundle.results.map(\.engine),
            [.mtranServer, .apple],
            "results must follow the user's selection order, not completion order"
        )
    }

    func testPrimaryIsFirstSuccessfulInSelectionOrder() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        await registry.register(
            makeProvider("apple", error: TranslationProviderError.notAvailable),
            for: .apple
        )
        await registry.register(
            makeProvider("mtran", results: [makeResult("Hello", "你好")]),
            for: .mtranServer
        )
        let service = TranslationService(registry: registry)

        let bundle = try await service.translateCompareV2(
            segments: ["Hello"],
            to: "zh-Hans",
            engines: [.builtin(.apple), .builtin(.mtranServer)]
        )

        XCTAssertEqual(bundle.primaryEngine, .mtranServer)
        XCTAssertEqual(bundle.primaryResult.first?.translated, "你好")
    }

    func testEmptySelectionRunsAppleDefensively() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        await registry.register(
            makeProvider("apple", results: [makeResult("Hello", "你好")]),
            for: .apple
        )
        let service = TranslationService(registry: registry)

        let bundle = try await service.translateCompareV2(
            segments: ["Hello"],
            to: "zh-Hans",
            engines: []
        )
        XCTAssertEqual(bundle.primaryEngine, .apple)
    }

    // MARK: - Settings invariants

    @MainActor
    func testCompareMinimumEnforcedBySetter() {
        let settings = AppSettings.shared
        let original = settings.compareEnginesV2
        defer { settings.setCompareEnginesV2(original) }

        settings.setCompareEnginesV2([.builtin(.apple), .builtin(.mtranServer)])
        XCTAssertEqual(settings.compareEnginesV2.count, 2)

        // Attempting to persist a 1-engine compare set keeps the valid value
        settings.setCompareEnginesV2([.builtin(.apple)])
        XCTAssertEqual(settings.compareEnginesV2.count, 2)
    }
}
