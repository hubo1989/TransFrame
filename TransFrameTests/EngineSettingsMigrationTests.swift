import XCTest
@testable import TransFrame

/// Tests for legacy -> V2 engine settings migration
/// (PRD: tasks/prd-engine-mode-convergence.md US-005)
final class EngineSettingsMigrationTests: XCTestCase {

    private func kinds(_ items: [EngineChainItem]) -> [EngineChainItem.Kind] {
        items.map { $0.kind }
    }

    // MARK: - Mode mapping

    func testModeMappingPrimaryWithFallbackToOrdered() {
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .primaryWithFallback,
            parallelEngines: [.apple, .mtranServer],
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .apple
        )
        XCTAssertEqual(result.mode, .ordered)
    }

    func testModeMappingQuickSwitchToOrdered() {
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .quickSwitch,
            parallelEngines: [.apple],
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .apple
        )
        XCTAssertEqual(result.mode, .ordered)
    }

    func testModeMappingSceneBindingToOrdered() {
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .sceneBinding,
            parallelEngines: [],
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .apple
        )
        XCTAssertEqual(result.mode, .ordered)
    }

    func testModeMappingParallelToCompare() {
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .parallel,
            parallelEngines: [.apple, .openai],
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .apple
        )
        XCTAssertEqual(result.mode, .compare)
    }

    func testMissingParallelKeyHonorsLegacySingleEngine() {
        // Codex P1: a missing parallelEngines key must not be treated as [.apple]
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .primaryWithFallback,
            parallelEngines: nil,
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .mtranServer
        )
        XCTAssertEqual(kinds(result.chain), [.builtin(.mtranServer)])
    }

    func testCustomEngineResolvesToCompatibleUUID() throws {
        // Codex P1: .custom must resolve to the selected compatible instance
        let config = CompatibleTranslationProvider.CompatibleConfig(
            displayName: "My Relay",
            baseURL: "https://relay.example.com/v1",
            modelName: "gpt-4o-mini"
        )
        let jsonData = try JSONEncoder().encode(config)
        let customName = String(data: jsonData, encoding: .utf8)
        let engineConfig = TranslationEngineConfig(id: .custom, isEnabled: true, options: nil, customName: customName)

        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .quickSwitch,
            parallelEngines: [.apple, .custom],
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .apple,
            legacyCustomEngineConfig: engineConfig
        )
        XCTAssertEqual(result.chain.count, 2)
        XCTAssertEqual(result.chain.last?.kind, .compatible(config.id))
    }

    func testCustomEngineWithoutParsableNameFallsBackToBuiltin() {
        let engineConfig = TranslationEngineConfig(id: .custom, isEnabled: true, options: nil, customName: "not-json")
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .quickSwitch,
            parallelEngines: [.apple, .custom],
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .apple,
            legacyCustomEngineConfig: engineConfig
        )
        XCTAssertEqual(result.chain.count, 2)
        XCTAssertEqual(result.chain.last?.kind, .builtin(.custom))
    }

    func testPrimaryWithFallbackDeduplicatesLegacyDuplicates() {
        // OCR: dedupe must apply uniformly across mode branches
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .primaryWithFallback,
            parallelEngines: [.apple, .apple, .mtranServer],
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .apple
        )
        XCTAssertEqual(kinds(result.chain), [.builtin(.apple), .builtin(.mtranServer)])
    }

    func testComparePaddingNeverDuplicatesAnEngine() {
        // OCR: chain=[.apple] must pad with a DIFFERENT engine
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .primaryWithFallback,
            parallelEngines: [.apple],
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .apple
        )
        let compareKinds = Set(kinds(result.compare))
        XCTAssertEqual(result.compare.count, EngineSelectionLimits.compareEnginesMinimum)
        XCTAssertEqual(compareKinds.count, result.compare.count)
    }

    // MARK: - Ordered chain construction

    func testPrimaryWithFallbackTakesFirstTwoEngines() {
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .primaryWithFallback,
            parallelEngines: [.apple, .mtranServer, .openai],
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .apple
        )
        XCTAssertEqual(
            kinds(result.chain),
            [.builtin(.apple), .builtin(.mtranServer)]
        )
    }

    func testQuickSwitchDeduplicatesPreservingOrder() {
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .quickSwitch,
            parallelEngines: [.apple, .openai, .apple, .mtranServer, .openai, .claude],
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .apple
        )
        XCTAssertEqual(
            kinds(result.chain),
            [EngineChainItem.Kind.builtin(.apple), .builtin(.openai), .builtin(.mtranServer), .builtin(.claude)]
        )
    }

    func testSceneBindingUsesScreenshotPrimaryAndFallback() {
        let binding = SceneEngineBinding(
            scene: .screenshot,
            primaryEngine: .openai,
            fallbackEngine: .mtranServer,
            fallbackEnabled: true
        )
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .sceneBinding,
            parallelEngines: [.apple],
            sceneBindings: [.screenshot: binding],
            fallbackEnabled: true,
            legacySingleEngine: .apple
        )
        XCTAssertEqual(kinds(result.chain), [EngineChainItem.Kind.builtin(.openai), .builtin(.mtranServer)])
    }

    func testSceneBindingWithoutFallbackKeepsPrimaryOnly() {
        let binding = SceneEngineBinding(
            scene: .screenshot,
            primaryEngine: .openai,
            fallbackEngine: .mtranServer,
            fallbackEnabled: false
        )
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .sceneBinding,
            parallelEngines: [.apple],
            sceneBindings: [.screenshot: binding],
            fallbackEnabled: true,
            legacySingleEngine: .apple
        )
        XCTAssertEqual(kinds(result.chain), [EngineChainItem.Kind.builtin(.openai)])
    }

    // MARK: - fallbackEnabled == false truncation

    func testFallbackDisabledTruncatesChainToPrimary() {
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .primaryWithFallback,
            parallelEngines: [.apple, .mtranServer, .openai],
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: false,
            legacySingleEngine: .apple
        )
        XCTAssertEqual(kinds(result.chain), [EngineChainItem.Kind.builtin(.apple)])
    }

    // MARK: - Edge cases

    func testEmptyConfigurationFallsBackToLegacySingleEngine() {
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .primaryWithFallback,
            parallelEngines: [],
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .mtranServer
        )
        XCTAssertEqual(kinds(result.chain), [EngineChainItem.Kind.builtin(.mtranServer)])
    }

    func testOutOfBoundsIndicesDoNotCrash() {
        // primaryWithFallback with a single engine: prefix(2) must not trap
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .primaryWithFallback,
            parallelEngines: [.openai],
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .apple
        )
        XCTAssertEqual(kinds(result.chain), [EngineChainItem.Kind.builtin(.openai)])
    }

    func testChainRespectsOrderedLimit() {
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .quickSwitch,
            parallelEngines: [.apple, .openai, .claude, .gemini, .mtranServer, .deepl, .baidu],
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .apple
        )
        XCTAssertLessThanOrEqual(result.chain.count, EngineSelectionLimits.orderedChainLimit)
        XCTAssertEqual(result.chain.count, 5)
    }

    // MARK: - Compare set construction

    func testParallelWithMultipleEnginesSeedsCompare() {
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .parallel,
            parallelEngines: [.apple, .openai, .claude, .gemini, .deepl],
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .apple
        )
        XCTAssertEqual(
            kinds(result.compare),
            [.builtin(.apple), .builtin(.openai), .builtin(.claude), .builtin(.gemini)]
        )
    }

    func testNonParallelSeedsCompareFromChainWithApplePadding() {
        let result = AppSettings.migrateLegacyEngineSettings(
            mode: .primaryWithFallback,
            parallelEngines: [.mtranServer],
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .apple
        )
        XCTAssertEqual(
            kinds(result.compare),
            [.builtin(.mtranServer), .builtin(.apple)]
        )
    }

    // MARK: - Idempotency (V2 keys present -> no re-derivation)

    @MainActor
    func testLoadOrMigrateIsIdempotentWhenV2KeysExist() throws {
        let defaults = UserDefaults.standard
        let suiteKey = "EngineSettingsMigrationTests"
        defer { defaults.removePersistentDomain(forName: suiteKey) }

        // Simulate: V2 keys already written
        let storedMode = EngineModeV2.compare
        let storedChain: [EngineChainItem] = [.builtin(.claude)]
        let storedCompare: [EngineChainItem] = [.builtin(.claude), .builtin(.gemini)]
        defaults.set(storedMode.rawValue, forKey: AppSettings.Keys.engineModeV2)
        defaults.set(try JSONEncoder().encode(storedChain), forKey: AppSettings.Keys.engineChainV2)
        defaults.set(try JSONEncoder().encode(storedCompare), forKey: AppSettings.Keys.compareEnginesV2)
        defer {
            defaults.removeObject(forKey: AppSettings.Keys.engineModeV2)
            defaults.removeObject(forKey: AppSettings.Keys.engineChainV2)
            defaults.removeObject(forKey: AppSettings.Keys.compareEnginesV2)
        }

        // Legacy values that would produce a DIFFERENT result if re-derived
        let legacyMode = EngineSelectionMode.primaryWithFallback
        let legacyParallel: [TranslationEngineType] = [.apple, .mtranServer]
        let result = AppSettings.loadOrMigrateEngineSelectionV2ForTesting(
            legacyMode: legacyMode,
            parallelEngines: legacyParallel,
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .apple
        )

        XCTAssertEqual(result.mode, .compare)
        XCTAssertEqual(kinds(result.chain), [EngineChainItem.Kind.builtin(.claude)])
        XCTAssertEqual(kinds(result.compare), [.builtin(.claude), .builtin(.gemini)])
    }

    @MainActor
    func testLegacyKeysSurviveMigration() throws {
        let defaults = UserDefaults.standard
        // Ensure no stale V2 keys from other tests
        defaults.removeObject(forKey: AppSettings.Keys.engineModeV2)
        defaults.removeObject(forKey: AppSettings.Keys.engineChainV2)
        defaults.removeObject(forKey: AppSettings.Keys.compareEnginesV2)

        // Codex P2: seed a real legacy value so the survival assertion can fail
        let seededParallel = "apple,mtran"
        defaults.set(["apple", "mtran"], forKey: AppSettings.Keys.parallelEngines)
        defer { defaults.removeObject(forKey: AppSettings.Keys.parallelEngines) }

        _ = AppSettings.loadOrMigrateEngineSelectionV2ForTesting(
            legacyMode: .primaryWithFallback,
            parallelEngines: [.apple, .mtranServer],
            sceneBindings: SceneEngineBinding.allDefaults,
            fallbackEnabled: true,
            legacySingleEngine: .apple
        )

        // Legacy key retains its exact seeded value after migration
        XCTAssertEqual(
            defaults.stringArray(forKey: AppSettings.Keys.parallelEngines)?.joined(separator: ","),
            seededParallel
        )
        // V2 keys were written
        XCTAssertNotNil(defaults.data(forKey: AppSettings.Keys.engineChainV2))

        defaults.removeObject(forKey: AppSettings.Keys.engineModeV2)
        defaults.removeObject(forKey: AppSettings.Keys.engineChainV2)
        defaults.removeObject(forKey: AppSettings.Keys.compareEnginesV2)
    }

    @MainActor
    func testSetterEnforcesStructuralInvariants() {
        let settings = AppSettings.shared
        let originalChain = settings.engineChainV2
        let originalCompare = settings.compareEnginesV2
        defer {
            settings.setEngineChainV2(originalChain)
            settings.setCompareEnginesV2(originalCompare)
        }

        // empty chain rejected
        settings.setEngineChainV2([])
        XCTAssertEqual(settings.engineChainV2, originalChain)
        // under-minimum compare rejected
        settings.setCompareEnginesV2([.builtin(.apple)])
        XCTAssertEqual(settings.compareEnginesV2, originalCompare)
        // valid writes apply
        settings.setEngineChainV2([.builtin(.apple), .builtin(.mtranServer)])
        XCTAssertEqual(settings.engineChainV2.map { $0.kind }, [.builtin(.apple), .builtin(.mtranServer)])
    }
}
