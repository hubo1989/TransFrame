import XCTest
@testable import TransFrame

@available(macOS 13.0, *)
@MainActor
final class TranslationServicePipelineTests: XCTestCase {

    private var originalEngineConfigs: [TranslationEngineType: TranslationEngineConfig] = [:]

    override func setUp() async throws {
        try await super.setUp()
        let settings = AppSettings.shared
        originalEngineConfigs = settings.engineConfigs
        for engine in TranslationEngineType.allCases {
            if var config = settings.engineConfigs[engine] {
                config.isEnabled = true
                settings.engineConfigs[engine] = config
            }
        }
        settings.saveEngineConfigs()
    }

    override func tearDown() async throws {
        let settings = AppSettings.shared
        settings.engineConfigs = originalEngineConfigs
        settings.saveEngineConfigs()
        try await super.tearDown()
    }


    private func makeResult(
        source: String,
        translated: String,
        sourceLanguage: String = "English",
        targetLanguage: String = "Chinese"
    ) -> TranslationResult {
        TranslationResult(
            sourceText: source,
            translatedText: translated,
            sourceLanguage: sourceLanguage,
            targetLanguage: targetLanguage
        )
    }

    func testPrimaryEngineAppliesCustomPromptAndReturnsBundle() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        let apple = MockTranslationProvider(
            id: "apple",
            name: "Apple",
            batchResults: [
                makeResult(source: "Hello", translated: "你好"),
                makeResult(source: "World", translated: "世界")
            ]
        )
        await registry.register(apple, for: .apple)

        let service = TranslationService(registry: registry)
        await service.updatePromptConfig(
            TranslationPromptConfig(
                enginePrompts: [.apple: "Custom prompt {text}"]
            )
        )

        let bundle = try await service.translate(
            segments: ["Hello", "World"],
            to: "zh-Hans",
            from: "en",
            scene: .translateAndInsert,
            mode: .primaryWithFallback,
            preferredEngine: .apple,
            fallbackEnabled: false
        )

        XCTAssertEqual(bundle.primaryEngine, .apple)
        XCTAssertEqual(bundle.selectionMode, .primaryWithFallback)
        XCTAssertEqual(bundle.primaryResult.map(\.translated), ["你好", "世界"])
        let appleRequests = await apple.requests
        let applePromptTemplate = await apple.lastPromptTemplate()
        XCTAssertEqual(appleRequests, [
            MockTranslationProvider.Request(
                texts: ["Hello", "World"],
                sourceLanguage: "en",
                targetLanguage: "zh-Hans"
            )
        ])
        XCTAssertEqual(applePromptTemplate, "Custom prompt {text}")
    }

    func testTranslateAndInsertUsesDefaultInsertPromptWhenNoCustomPromptExists() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        let apple = MockTranslationProvider(
            id: "apple",
            name: "Apple",
            batchResults: [
                makeResult(source: "Translate me", translated: "请翻译我")
            ]
        )
        await registry.register(apple, for: .apple)

        let service = TranslationService(registry: registry)

        _ = try await service.translate(
            segments: ["Translate me"],
            to: "zh-Hans",
            from: "en",
            scene: .translateAndInsert,
            mode: .primaryWithFallback,
            preferredEngine: .apple,
            fallbackEnabled: false
        )

        let promptTemplate = await apple.lastPromptTemplate()
        XCTAssertEqual(promptTemplate, TranslationPromptConfig.defaultInsertPrompt)
    }

    func testCustomEngineUsesCompatiblePromptForSelectedProviderInstance() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        let custom = MockTranslationProvider(
            id: "custom:1",
            name: "Custom",
            batchResults: [
                makeResult(source: "Translate me", translated: "翻译我")
            ],
            promptContextID: "compatible-provider-1"
        )
        await registry.register(custom, for: .custom)

        let service = TranslationService(registry: registry)
        await service.updatePromptConfig(
            TranslationPromptConfig(
                compatibleEnginePrompts: ["compatible-provider-1": "Compatible prompt {text}"]
            )
        )

        _ = try await service.translate(
            segments: ["Translate me"],
            to: "zh-Hans",
            from: "en",
            scene: .translateAndInsert,
            mode: .primaryWithFallback,
            preferredEngine: .custom,
            fallbackEnabled: false
        )

        let promptTemplate = await custom.lastPromptTemplate()
        XCTAssertEqual(promptTemplate, "Compatible prompt {text}")
    }

    func testPrimaryWithFallbackUsesFallbackWhenPrimaryFails() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        let primary = MockTranslationProvider(
            id: "apple",
            name: "Apple",
            translateError: TranslationProviderError.connectionFailed("primary offline")
        )
        let fallback = MockTranslationProvider(
            id: "mtran",
            name: "MTran",
            batchResults: [
                makeResult(source: "Hello", translated: "你好")
            ]
        )

        await registry.register(primary, for: .apple)
        await registry.register(fallback, for: .mtranServer)

        let service = TranslationService(registry: registry)

        let bundle = try await service.translate(
            segments: ["Hello"],
            to: "zh-Hans",
            from: "en",
            mode: .primaryWithFallback,
            preferredEngine: .apple,
            fallbackEnabled: true
        )

        XCTAssertEqual(bundle.primaryEngine, .mtranServer)
        XCTAssertEqual(bundle.successfulEngines, [.mtranServer])
        XCTAssertEqual(bundle.failedEngines, [.apple])
        let primaryRequestCount = await primary.requestCount()
        let fallbackRequestCount = await fallback.requestCount()
        XCTAssertEqual(primaryRequestCount, 1)
        XCTAssertEqual(fallbackRequestCount, 1)
    }

    func testUnavailablePrimaryEngineFallsBackWithoutExecutingPrimaryTranslation() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        let primary = MockTranslationProvider(
            id: "apple",
            name: "Apple",
            available: false,
            batchResults: [
                makeResult(source: "Hello", translated: "你好")
            ]
        )
        let fallback = MockTranslationProvider(
            id: "mtran",
            name: "MTran",
            batchResults: [
                makeResult(source: "Hello", translated: "您好")
            ]
        )

        await registry.register(primary, for: .apple)
        await registry.register(fallback, for: .mtranServer)

        let service = TranslationService(registry: registry)

        let bundle = try await service.translate(
            segments: ["Hello"],
            to: "zh-Hans",
            from: "en",
            mode: .primaryWithFallback,
            preferredEngine: .apple,
            fallbackEnabled: true
        )

        XCTAssertEqual(bundle.primaryEngine, .mtranServer)
        XCTAssertEqual(bundle.primaryResult.map(\.translated), ["您好"])
        let primaryRequestCount = await primary.requestCount()
        let fallbackRequestCount = await fallback.requestCount()
        XCTAssertEqual(primaryRequestCount, 0)
        XCTAssertEqual(fallbackRequestCount, 1)
    }

    func testRegistryCreatesBuiltInProviderWhenRegistrationWasSkipped() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)

        let provider = try await registry.createProvider(
            for: .apple,
            config: .default(for: .apple)
        )

        XCTAssertTrue(provider is AppleTranslationProvider)
        let registeredProvider = await registry.provider(for: .apple)
        XCTAssertNotNil(registeredProvider)
    }

    func testRegistryCreatesLLMProvidersThatArePromptConfigurable() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        let provider = try await registry.createProvider(
            for: .ollama,
            config: TranslationEngineConfig(
                id: .ollama,
                isEnabled: true,
                options: EngineOptions(
                    baseURL: "http://127.0.0.1:11434",
                    modelName: "llama3",
                    timeout: 30
                )
            )
        )

        XCTAssertNotNil(provider as? any TranslationPromptConfigurable)
    }

    func testParallelModeCapturesSuccessAndFailurePerEngine() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        let apple = MockTranslationProvider(
            id: "apple",
            name: "Apple",
            batchResults: [
                makeResult(source: "Hello", translated: "你好")
            ]
        )
        let mtran = MockTranslationProvider(
            id: "mtran",
            name: "MTran",
            translateError: TranslationProviderError.timeout
        )

        await registry.register(apple, for: .apple)
        await registry.register(mtran, for: .mtranServer)

        let service = TranslationService(registry: registry)

        let bundle = try await service.translate(
            segments: ["Hello"],
            to: "zh-Hans",
            from: "en",
            mode: .parallel,
            preferredEngine: .apple,
            parallelEngines: [.apple, .mtranServer]
        )

        XCTAssertEqual(bundle.selectionMode, .parallel)
        XCTAssertEqual(bundle.result(for: .apple)?.segments.map(\.translated), ["你好"])
        XCTAssertNil(bundle.result(for: .mtranServer)?.segments.first)
        XCTAssertNotNil(bundle.result(for: .mtranServer)?.error)
        XCTAssertTrue(bundle.hasErrors)
    }

    func testQuickSwitchUsesPrimaryEngineWithoutFallback() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        let apple = MockTranslationProvider(
            id: "apple",
            name: "Apple",
            batchResults: [
                makeResult(source: "Hello", translated: "你好")
            ]
        )
        let mtran = MockTranslationProvider(
            id: "mtran",
            name: "MTran",
            batchResults: [
                makeResult(source: "Hello", translated: "您好")
            ]
        )

        await registry.register(apple, for: .apple)
        await registry.register(mtran, for: .mtranServer)

        let service = TranslationService(registry: registry)

        let bundle = try await service.translate(
            segments: ["Hello"],
            to: "zh-Hans",
            from: "en",
            mode: .quickSwitch,
            preferredEngine: .apple
        )

        // Phase 6: quickSwitch converged into ordered (no fallback)
        XCTAssertEqual(bundle.primaryEngine, .apple)
        XCTAssertEqual(bundle.primaryResult.map(\.translated), ["你好"])
        let appleRequestCount = await apple.requestCount()
        let mtranRequestCount = await mtran.requestCount()
        XCTAssertEqual(appleRequestCount, 1)
        XCTAssertEqual(mtranRequestCount, 0)
    }

    func testSceneBindingHonorsSceneSpecificPrimaryEngine() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        let apple = MockTranslationProvider(
            id: "apple",
            name: "Apple",
            batchResults: [
                makeResult(source: "Hello", translated: "你好")
            ]
        )
        let mtran = MockTranslationProvider(
            id: "mtran",
            name: "MTran",
            batchResults: [
                makeResult(source: "Hello", translated: "您好")
            ]
        )

        await registry.register(apple, for: .apple)
        await registry.register(mtran, for: .mtranServer)

        let service = TranslationService(registry: registry)

        let bundle = try await service.translate(
            segments: ["Hello"],
            to: "zh-Hans",
            from: "en",
            scene: .screenshot,
            mode: .sceneBinding,
            preferredEngine: .apple,
            sceneBindings: [
                .screenshot: SceneEngineBinding(
                    scene: .screenshot,
                    primaryEngine: .mtranServer,
                    fallbackEngine: nil,
                    fallbackEnabled: false
                )
            ]
        )

        XCTAssertEqual(bundle.primaryEngine, .mtranServer)
        XCTAssertEqual(bundle.primaryResult.map(\.translated), ["您好"])
        let appleRequestCount = await apple.requestCount()
        let mtranRequestCount = await mtran.requestCount()
        XCTAssertEqual(appleRequestCount, 0)
        XCTAssertEqual(mtranRequestCount, 1)
    }

    func testAllEnginesFailThrowsMultiEngineError() async {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        let apple = MockTranslationProvider(
            id: "apple",
            name: "Apple",
            translateError: TranslationProviderError.connectionFailed("primary offline")
        )
        let mtran = MockTranslationProvider(
            id: "mtran",
            name: "MTran",
            translateError: TranslationProviderError.timeout
        )

        await registry.register(apple, for: .apple)
        await registry.register(mtran, for: .mtranServer)

        let service = TranslationService(registry: registry)

        do {
            _ = try await service.translate(
                segments: ["Hello"],
                to: "zh-Hans",
                from: "en",
                mode: .primaryWithFallback,
                preferredEngine: .apple,
                fallbackEnabled: true
            )
            XCTFail("Expected translation failure")
        } catch let error as MultiEngineError {
            switch error {
            case .allEnginesFailed(let errors):
                XCTAssertEqual(errors.count, 2)
            default:
                XCTFail("Unexpected multi-engine error: \(error)")
            }
        } catch {
            XCTFail("Unexpected error type: \(error)")
        }
    }

    func testEmptyInputReturnsEmptyBundleWithoutQueryingProviders() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        let apple = MockTranslationProvider(id: "apple", name: "Apple")
        await registry.register(apple, for: .apple)

        let service = TranslationService(registry: registry)

        let bundle = try await service.translate(
            segments: [],
            to: "zh-Hans",
            from: "en",
            mode: .primaryWithFallback,
            preferredEngine: .apple
        )

        XCTAssertTrue(bundle.results.isEmpty)
        let appleRequestCount = await apple.requestCount()
        XCTAssertEqual(appleRequestCount, 0)
    }

    func testTextTranslationFlowUpdatesStateAndResultOnSuccess() async throws {
        let service = MockTranslationServicing(
            nextResult: [
                BilingualSegment(
                    original: TextSegment(text: "Hello", boundingBox: .zero, confidence: 1.0),
                    translated: "你好",
                    sourceLanguage: "English",
                    targetLanguage: "Chinese"
                )
            ]
        )
        let flow = TextTranslationFlow(service: service)

        let result = try await flow.translate(
            "Hello",
            config: TextTranslationConfig(
                targetLanguage: "zh-Hans",
                sourceLanguage: "en",
                preferredEngine: .apple,
                scene: .translateAndInsert,
                mode: .parallel,
                fallbackEnabled: false,
                parallelEngines: [.apple, .mtranServer],
                sceneBindings: [.translateAndInsert: SceneEngineBinding(
                    scene: .translateAndInsert,
                    primaryEngine: .custom,
                    fallbackEngine: .ollama,
                    fallbackEnabled: true
                )]
            )
        )

        XCTAssertEqual(result.translatedText, "你好")
        XCTAssertEqual(result.targetLanguage, "Chinese")
        let currentPhase = await flow.currentPhase
        let lastError = await flow.lastError
        let lastResult = await flow.lastResult
        let serviceRequests = await service.requests
        let serviceRequestCount = await service.requestCount()
        XCTAssertEqual(currentPhase, .completed)
        XCTAssertNil(lastError)
        XCTAssertEqual(lastResult?.translatedText, "你好")
        XCTAssertEqual(serviceRequests, [
            MockTranslationServicing.Request(
                segments: ["Hello"],
                targetLanguage: "zh-Hans",
                preferredEngine: .apple,
                sourceLanguage: "en",
                scene: .translateAndInsert,
                mode: .parallel,
                fallbackEnabled: false,
                parallelEngines: [.apple, .mtranServer],
                sceneBindings: [.translateAndInsert: SceneEngineBinding(
                    scene: .translateAndInsert,
                    primaryEngine: .custom,
                    fallbackEngine: .ollama,
                    fallbackEnabled: true
                )]
            )
        ])
        XCTAssertEqual(serviceRequestCount, 1)
    }

    func testTextTranslationFlowMapsServiceFailureToUserFacingErrorState() async {
        let service = MockTranslationServicing(
            nextError: TranslationProviderError.connectionFailed("offline")
        )
        let flow = TextTranslationFlow(service: service)

        do {
            _ = try await flow.translate(
                "Hello",
                config: TextTranslationConfig(
                    targetLanguage: "zh-Hans",
                    sourceLanguage: "en",
                    preferredEngine: .apple,
                    scene: .textSelection,
                    mode: .primaryWithFallback,
                    fallbackEnabled: true,
                    parallelEngines: [],
                    sceneBindings: [:]
                )
            )
            XCTFail("Expected flow to fail")
        } catch let error as TextTranslationError {
            switch error {
            case .translationFailed(let message):
                XCTAssertTrue(message.contains("offline"))
            default:
                XCTFail("Unexpected text translation error: \(error)")
            }
        } catch {
            XCTFail("Unexpected error type: \(error)")
        }

        let currentPhase = await flow.currentPhase
        let lastError = await flow.lastError
        let serviceRequestCount = await service.requestCount()

        if case .failed(let failedError) = currentPhase,
           case .translationFailed(let message) = failedError {
            XCTAssertTrue(message.contains("offline"))
        } else {
            XCTFail("Expected failed phase with translationFailed error")
        }

        if case .translationFailed(let message)? = lastError {
            XCTAssertTrue(message.contains("offline"))
        } else {
            XCTFail("Expected translationFailed error")
        }
        XCTAssertEqual(serviceRequestCount, 1)
    }

    func testSelfLanguageTranslationBypass() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        let apple = MockTranslationProvider(
            id: "apple",
            name: "Apple",
            batchResults: []
        )
        await registry.register(apple, for: .apple)

        let service = TranslationService(registry: registry)

        // Explicit source == target: always bypass
        let bundle = try await service.translate(
            segments: ["你好"],
            to: "zh-Hans",
            from: "zh-Hans",
            mode: .primaryWithFallback,
            preferredEngine: .apple,
            fallbackEnabled: false
        )

        let appleRequests = await apple.requests
        XCTAssertEqual(appleRequests.count, 0, "Same source/target should bypass provider")

        let primaryResult = bundle.primaryResult
        XCTAssertEqual(primaryResult.count, 1)
        XCTAssertEqual(primaryResult[0].translated, "你好")
    }

    func testExplicitSourceLanguageRespected() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        let apple = MockTranslationProvider(
            id: "apple",
            name: "Apple",
            batchResults: [makeResult(source: "漢語", translated: "汉语")]
        )
        await registry.register(apple, for: .apple)
        let service = TranslationService(registry: registry)

        // Explicit zh-Hant → zh-Hans: must translate (script conversion)
        let bundle = try await service.translate(
            segments: ["漢語"],
            to: "zh-Hans",
            from: "zh-Hant",
            mode: .primaryWithFallback,
            preferredEngine: .apple,
            fallbackEnabled: false
        )

        let appleRequests = await apple.requests
        XCTAssertEqual(appleRequests.count, 1, "Different source/target should call provider")
        XCTAssertEqual(appleRequests[0].texts, ["漢語"])
    }

    func testPrintRealUserSettings() async {
        await MainActor.run {
            print("--- USER SETTINGS PRINT ---")
            let settings = AppSettings.shared
            print("ocrEngine: \(settings.ocrEngine.rawValue)")
            print("translationEngine: \(settings.translationEngine.rawValue)")
            print("translationTargetLanguage: \(String(describing: settings.translationTargetLanguage?.rawValue))")
            print("translationSourceLanguage: \(settings.translationSourceLanguage.rawValue)")
            print("translationFallbackEnabled: \(settings.translationFallbackEnabled)")
            print("engineSelectionMode: \(settings.engineSelectionMode.rawValue)")
            print("vlmProvider: \(settings.vlmProvider.rawValue)")
            print("---------------------------")
        }
    }

    func testSanitizeTranslationCurlyBraces() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        let apple = MockTranslationProvider(
            id: "apple",
            name: "Apple",
            batchResults: [
                makeResult(source: "Hello world", translated: #"{""}"#),
            ]
        )
        await registry.register(apple, for: .apple)

        let service = TranslationService(registry: registry)

        do {
            let bundle = try await service.translate(
                segments: ["Hello world"],
                to: "zh-Hans",
                from: "en",
                scene: .screenshot,
                mode: .primaryWithFallback,
                preferredEngine: .apple,
                fallbackEnabled: false
            )
            let results = bundle.primaryResult
            XCTAssertNotEqual(results.first?.translated, "Hello world",
                              "Invalid translation should not silently return source")
        } catch {
            // Expected: invalid translation triggers error (wrapped as MultiEngineError when fallback disabled)
            XCTAssertTrue(error is TranslationProviderError || error is MultiEngineError,
                          "Expected translation error, got \(error)")
        }
    }

    func testEmptyTranslationTriggersError() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        let apple = MockTranslationProvider(
            id: "apple",
            name: "Apple",
            batchResults: [
                makeResult(source: "Hello", translated: ""),
            ]
        )
        await registry.register(apple, for: .apple)
        let service = TranslationService(registry: registry)

        do {
            _ = try await service.translate(
                segments: ["Hello"],
                to: "zh-Hans",
                from: "en",
                mode: .primaryWithFallback,
                preferredEngine: .apple,
                fallbackEnabled: false
            )
            XCTFail("Empty translation should trigger error for fallback")
        } catch {
            // Expected
        }
    }
}

