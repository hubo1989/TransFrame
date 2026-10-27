import XCTest
@testable import TransFrame

final class TranslationConfigurationTests: XCTestCase {
    func testScenePromptTakesPriorityOverEngineAndCompatiblePrompts() {
        let config = TranslationPromptConfig(
            enginePrompts: [.openai: "Engine prompt {text}"],
            compatibleEnginePrompts: ["compatible-0": "Compatible prompt {text}"],
            scenePrompts: [.screenshot: "Scene prompt {source_language} -> {target_language}: {text}"]
        )

        let resolved = config.resolvedPrompt(
            for: .openai,
            scene: .screenshot,
            sourceLanguage: "English",
            targetLanguage: "Chinese",
            text: "Hello",
            compatiblePromptID: "compatible-0"
        )

        XCTAssertEqual(resolved, "Scene prompt English -> Chinese: Hello")
    }

    func testCompatiblePromptIsUsedForCustomEngineInstances() {
        let config = TranslationPromptConfig(
            compatibleEnginePrompts: ["compatible-2": "Compatible prompt {text}"]
        )

        let resolved = config.resolvedPrompt(
            for: .custom,
            scene: .textSelection,
            sourceLanguage: "English",
            targetLanguage: "Japanese",
            text: "Hello",
            compatiblePromptID: "compatible-2"
        )

        XCTAssertEqual(resolved, "Compatible prompt Hello")
    }

    func testTranslateAndInsertUsesDedicatedDefaultPromptWhenNoCustomPromptExists() {
        let config = TranslationPromptConfig()

        XCTAssertEqual(
            config.promptPreview(for: .apple, scene: .translateAndInsert),
            TranslationPromptConfig.defaultInsertPrompt
        )
    }

    func testResetRemovesAllCustomPrompts() {
        var config = TranslationPromptConfig(
            enginePrompts: [.openai: "Engine"],
            compatibleEnginePrompts: ["compatible-1": "Compatible"],
            scenePrompts: [.textSelection: "Scene"]
        )

        XCTAssertTrue(config.hasCustomPrompts)
        config.reset()

        XCTAssertFalse(config.hasCustomPrompts)
        XCTAssertTrue(config.enginePrompts.isEmpty)
        XCTAssertTrue(config.compatibleEnginePrompts.isEmpty)
        XCTAssertTrue(config.scenePrompts.isEmpty)
    }

    func testEngineConfigDefaultsCoverBuiltInAndCloudEngines() {
        let apple = TranslationEngineConfig.default(for: .apple)
        let openai = TranslationEngineConfig.default(for: .openai)

        XCTAssertTrue(apple.isEnabled)
        XCTAssertNil(apple.options)

        XCTAssertFalse(openai.isEnabled)
        XCTAssertEqual(openai.options?.baseURL, TranslationEngineType.openai.defaultBaseURL)
        XCTAssertEqual(openai.options?.modelName, TranslationEngineType.openai.defaultModelName)
        XCTAssertEqual(openai.options?.timeout, 30)
    }

    func testSceneBindingsDefaultToAppleWithMTranFallback() {
        let binding = SceneEngineBinding.default(for: .textSelection)

        XCTAssertEqual(binding.primaryEngine, .apple)
        XCTAssertEqual(binding.fallbackEngine, .mtranServer)
        XCTAssertTrue(binding.fallbackEnabled)

        let allDefaults = SceneEngineBinding.allDefaults
        XCTAssertEqual(allDefaults.count, TranslationScene.allCases.count)
    }

    func testTranslationResultHelpersPreserveAndCombineContent() {
        let first = TranslationResult(
            sourceText: "Hello",
            translatedText: "你好",
            sourceLanguage: "English",
            targetLanguage: "Chinese",
            timestamp: Date(timeIntervalSince1970: 1)
        )
        let second = TranslationResult(
            sourceText: "World",
            translatedText: "世界",
            sourceLanguage: "English",
            targetLanguage: "Chinese",
            timestamp: Date(timeIntervalSince1970: 2)
        )

        let empty = TranslationResult.empty(for: "Same")
        XCTAssertFalse(empty.hasChanges)
        XCTAssertEqual(empty.sourceText, "Same")
        XCTAssertEqual(empty.translatedText, "Same")

        let combined = TranslationResult.combine([first, second])
        XCTAssertEqual(combined?.sourceText, "Hello\nWorld")
        XCTAssertEqual(combined?.translatedText, "你好\n世界")
        XCTAssertEqual(combined?.sourceLanguage, "English")
        XCTAssertEqual(combined?.targetLanguage, "Chinese")
        XCTAssertEqual(combined?.timestamp, first.timestamp)
    }

    func testTranslationResultBundleDerivesStatusCorrectly() {
        let primary = TranslationResult(
            sourceText: "Hello",
            translatedText: "你好",
            sourceLanguage: "English",
            targetLanguage: "Chinese"
        )
        let secondaryError = TranslationProviderError.timeout

        let bundle = TranslationResultBundle(
            results: [
                EngineResult(engine: .apple, segments: [BilingualSegment(from: primary)], latency: 0.5),
                EngineResult.failed(engine: .mtranServer, error: secondaryError)
            ],
            primaryEngine: .apple,
            selectionMode: .parallel,
            scene: .screenshot,
            timestamp: Date(timeIntervalSince1970: 100)
        )

        XCTAssertEqual(bundle.primaryResult.count, 1)
        XCTAssertEqual(bundle.successfulEngines, [.apple])
        XCTAssertEqual(bundle.failedEngines, [.mtranServer])
        XCTAssertTrue(bundle.hasErrors)
        XCTAssertFalse(bundle.allFailed)
        XCTAssertEqual(bundle.averageLatency, 0.5, accuracy: 0.0001)
        XCTAssertEqual(bundle.scene, .screenshot)
        XCTAssertEqual(bundle.timestamp, Date(timeIntervalSince1970: 100))
    }

    func testFailedBundleMarksAllEnginesFailed() {
        let bundle = TranslationResultBundle.failed(
            engine: .deepl,
            error: TranslationProviderError.connectionFailed("offline"),
            selectionMode: .primaryWithFallback,
            scene: .translateAndInsert
        )

        XCTAssertTrue(bundle.allFailed)
        XCTAssertEqual(bundle.primaryEngine, .deepl)
        XCTAssertEqual(bundle.selectionMode, .primaryWithFallback)
        XCTAssertEqual(bundle.scene, .translateAndInsert)
    }

    func testTranslationProviderErrorExposesRecoveryGuidance() {
        let errors: [TranslationProviderError] = [
            .notAvailable,
            .connectionFailed("offline"),
            .invalidConfiguration("missing key"),
            .translationFailed("bad gateway"),
            .incompleteResponse("truncated"),
            .emptyInput,
            .unsupportedLanguage("tlh"),
            .timeout,
            .rateLimited(retryAfter: 10)
        ]

        for error in errors {
            guard let description = error.errorDescription,
                  let suggestion = error.recoverySuggestion else {
                return XCTFail("Expected localized description and recovery suggestion")
            }

            XCTAssertFalse(description.isEmpty)
            XCTAssertFalse(suggestion.isEmpty)
        }
    }

    /// 面板国旗横排：每个语言都有非空国旗，快捷列表内国旗互不重复。
    func testFlagEmojiCoversAllLanguagesUniquelyForQuickSwitcher() {
        let allFlags = TranslationLanguage.allCases.map(\.flagEmoji)
        XCTAssertTrue(allFlags.allSatisfy { !$0.isEmpty }, "每个语言都必须有国旗 emoji")

        let quickSwitcher: [TranslationLanguage] = [
            .chineseSimplified, .english, .japanese, .korean,
            .french, .german, .spanish, .russian,
        ]
        let quickFlags = quickSwitcher.map(\.flagEmoji)
        XCTAssertEqual(Set(quickFlags).count, quickFlags.count, "快捷切换语言的国旗必须可区分")
        XCTAssertEqual(TranslationLanguage.auto.flagEmoji, "🌐")
    }
}
