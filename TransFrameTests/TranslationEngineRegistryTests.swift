import XCTest
@testable import TransFrame

@available(macOS 13.0, *)
final class TranslationEngineRegistryTests: XCTestCase {
    func testUnchangedConfigurationReusesProvider() async throws {
        let counter = CreationCounter()
        let registry = TranslationEngineRegistry(
            registerBuiltInProviders: false,
            providerFactory: { type, config, _ in
                let index = await counter.next()
                return MockTranslationProvider(
                    id: "\(type.rawValue)-\(index)",
                    name: config.options?.modelName ?? "default"
                )
            }
        )
        let config = TranslationEngineConfig.default(for: .ollama)

        let first = try await registry.createProvider(for: .ollama, config: config)
        let second = try await registry.createProvider(for: .ollama, config: config)
        let count = await counter.value

        XCTAssertTrue((first as AnyObject) === (second as AnyObject))
        XCTAssertEqual(count, 1)
    }

    func testChangedOptionsCreateProviderWithCurrentConfiguration() async throws {
        let registry = TranslationEngineRegistry(registerBuiltInProviders: false)
        let initial = TranslationEngineConfig.default(for: .ollama)
        var updated = initial
        updated.options?.modelName = "updated-model"
        updated.options?.timeout = 19
        updated.options?.customHeaders = ["X-Test-Configuration": "updated"]

        let first = try await registry.createProvider(for: .ollama, config: initial)
        let second = try await registry.createProvider(for: .ollama, config: updated)

        XCTAssertFalse((first as AnyObject) === (second as AnyObject))
        let provider = try XCTUnwrap(second as? LLMTranslationProvider)
        let actualConfig = await provider.config
        XCTAssertEqual(actualConfig, updated)
    }

    func testSameConfigurationCoalescesConcurrentCreation() async throws {
        let gate = AsyncGate()
        let counter = CreationCounter()
        let registry = TranslationEngineRegistry(
            registerBuiltInProviders: false,
            providerFactory: { type, config, _ in
                let index = await counter.next()
                await gate.enterAndWait()
                return MockTranslationProvider(
                    id: "\(type.rawValue)-\(index)",
                    name: config.options?.modelName ?? "default"
                )
            }
        )
        let config = TranslationEngineConfig.default(for: .ollama)

        let firstTask = Task { try await registry.createProvider(for: .ollama, config: config) }
        await gate.waitUntilEntered(count: 1)
        let secondTask = Task { try await registry.createProvider(for: .ollama, config: config) }
        await Task.yield()
        await gate.resumeAll()

        let first = try await firstTask.value
        let second = try await secondTask.value
        let count = await counter.value

        XCTAssertTrue((first as AnyObject) === (second as AnyObject))
        XCTAssertEqual(count, 1)
    }

    func testStaleCreationFinishingAfterNewerConfigurationDoesNotOverwriteCache() async throws {
        let firstGate = AsyncGate()
        let registry = TranslationEngineRegistry(
            registerBuiltInProviders: false,
            providerFactory: { _, config, _ in
                let model = config.options?.modelName ?? "default"
                if model == "first-model" {
                    await firstGate.enterAndWait()
                }
                return MockTranslationProvider(id: model, name: model)
            }
        )
        var firstConfig = TranslationEngineConfig.default(for: .ollama)
        firstConfig.options?.modelName = "first-model"
        var secondConfig = firstConfig
        secondConfig.options?.modelName = "second-model"

        let staleTask = Task { try await registry.createProvider(for: .ollama, config: firstConfig) }
        await firstGate.waitUntilEntered(count: 1)

        let newest = try await registry.createProvider(for: .ollama, config: secondConfig)
        await firstGate.resumeAll()
        _ = try await staleTask.value

        let cached = await registry.provider(for: .ollama)
        let cachedAfter = try await registry.createProvider(for: .ollama, config: secondConfig)

        XCTAssertEqual(cached?.id, "second-model")
        XCTAssertTrue((cachedAfter as AnyObject) === (newest as AnyObject))
    }

    func testUnregisterDuringInFlightCreationPreventsLateRegistration() async throws {
        let gate = AsyncGate()
        let registry = TranslationEngineRegistry(
            registerBuiltInProviders: false,
            providerFactory: { type, _, _ in
                await gate.enterAndWait()
                return MockTranslationProvider(id: type.rawValue, name: type.rawValue)
            }
        )
        let config = TranslationEngineConfig.default(for: .ollama)

        let task = Task { try await registry.createProvider(for: .ollama, config: config) }
        await gate.waitUntilEntered(count: 1)
        await registry.unregister(.ollama)
        await gate.resumeAll()
        _ = try await task.value

        let cached = await registry.provider(for: .ollama)
        XCTAssertNil(cached)
    }

    func testManualRegisterDuringInFlightCreationPreservesManualOverride() async throws {
        let gate = AsyncGate()
        let registry = TranslationEngineRegistry(
            registerBuiltInProviders: false,
            providerFactory: { _, _, _ in
                await gate.enterAndWait()
                return MockTranslationProvider(id: "factory", name: "Factory")
            }
        )
        let config = TranslationEngineConfig.default(for: .ollama)
        let manual = MockTranslationProvider(id: "manual", name: "Manual")

        let task = Task { try await registry.createProvider(for: .ollama, config: config) }
        await gate.waitUntilEntered(count: 1)
        await registry.register(manual, for: .ollama)
        await gate.resumeAll()
        _ = try await task.value

        let cached = await registry.provider(for: .ollama)
        let resolved = try await registry.createProvider(for: .ollama, config: config)
        XCTAssertEqual(cached?.id, "manual")
        XCTAssertEqual(resolved.id, "manual")
    }

    func testCompatibleConfigurationUpdatesAndCoalescesConcurrentCreation() async throws {
        let gate = AsyncGate()
        let counter = CreationCounter()
        let registry = TranslationEngineRegistry(
            registerBuiltInProviders: false,
            compatibleProviderFactory: { compatibleConfig, engineConfig in
                _ = await counter.next()
                if compatibleConfig.displayName == "Initial" {
                    await gate.enterAndWait()
                }
                return try await CompatibleTranslationProvider(
                    config: engineConfig,
                    compatibleConfig: compatibleConfig,
                    keychain: .shared
                )
            }
        )
        let id = UUID()
        let initial = compatibleConfig(id: id, displayName: "Initial")
        var renamed = initial
        renamed.displayName = "Renamed"

        let firstTask = Task { try await registry.createCompatibleProvider(compatibleConfig: initial) }
        await gate.waitUntilEntered(count: 1)
        let coalescedTask = Task { try await registry.createCompatibleProvider(compatibleConfig: initial) }
        await Task.yield()
        await gate.resumeAll()

        let first = try await firstTask.value
        let coalesced = try await coalescedTask.value
        let countAfterCoalesce = await counter.value
        let second = try await registry.createCompatibleProvider(compatibleConfig: renamed)

        XCTAssertTrue(first === coalesced)
        XCTAssertEqual(countAfterCoalesce, 1)
        XCTAssertFalse(first === second)
        XCTAssertEqual(second.name, "Renamed")
    }

    func testCustomEngineRefreshesWhenSavedCompatibleConfigOrOptionsChange() async throws {
        let id = UUID()
        let savedStore = CompatibleConfigStore([
            compatibleConfig(id: id, displayName: "Instance V1", modelName: "model-v1")
        ])
        let registry = TranslationEngineRegistry(
            registerBuiltInProviders: false,
            compatibleConfigsProvider: { await savedStore.configs() }
        )
        let encodedSelection = String(
            decoding: try JSONEncoder().encode(compatibleConfig(id: id, displayName: "Stale JSON", modelName: "stale")),
            as: UTF8.self
        )
        let initial = TranslationEngineConfig(
            id: .custom,
            isEnabled: true,
            options: EngineOptions(timeout: 15, temperature: 0.2),
            customName: encodedSelection
        )

        let first = try await registry.createProvider(for: .custom, config: initial)
        let cachedSame = try await registry.createProvider(for: .custom, config: initial)
        XCTAssertTrue((first as AnyObject) === (cachedSame as AnyObject))
        XCTAssertEqual(first.name, "Instance V1")

        await savedStore.update([
            compatibleConfig(id: id, displayName: "Instance V2", modelName: "model-v2")
        ])
        let afterSavedUpdate = try await registry.createProvider(for: .custom, config: initial)
        XCTAssertFalse((first as AnyObject) === (afterSavedUpdate as AnyObject))
        XCTAssertEqual(afterSavedUpdate.name, "Instance V2")

        var updatedOptions = initial
        updatedOptions.options?.timeout = 45
        let afterOptionsUpdate = try await registry.createProvider(for: .custom, config: updatedOptions)
        XCTAssertFalse((afterSavedUpdate as AnyObject) === (afterOptionsUpdate as AnyObject))
        let compatible = try XCTUnwrap(afterOptionsUpdate as? CompatibleTranslationProvider)
        XCTAssertEqual(compatible.configurationSnapshot.engineConfig.options?.timeout, 45)
    }

    func testConnectionVerificationAndTestingUseCurrentEngineConfiguration() async throws {
        let configStore = EngineConfigStore([
            .openai: TranslationEngineConfig(
                id: .openai,
                isEnabled: true,
                options: EngineOptions(baseURL: "https://initial.example/v1", modelName: "gpt-initial")
            )
        ])
        let registry = TranslationEngineRegistry(
            registerBuiltInProviders: false,
            providerFactory: { _, config, _ in
                let model = config.options?.modelName ?? "missing"
                let isUpdated = model == "gpt-updated"
                return MockTranslationProvider(
                    id: model,
                    name: model,
                    batchResults: [
                        TranslationResult(
                            sourceText: "Hello",
                            translatedText: "Translated by \(model)",
                            sourceLanguage: "en",
                            targetLanguage: "zh-Hans"
                        )
                    ],
                    translateError: isUpdated ? nil : TranslationProviderError.connectionFailed("stale config"),
                    checkConnectionResult: isUpdated
                )
            }
        )
        let service = TranslationService(
            registry: registry,
            engineConfigsProvider: { await configStore.configs() }
        )

        let initialConnection = await service.testConnection(for: .openai)
        XCTAssertFalse(initialConnection)

        await configStore.update([
            .openai: TranslationEngineConfig(
                id: .openai,
                isEnabled: true,
                options: EngineOptions(baseURL: "https://updated.example/v1", modelName: "gpt-updated")
            )
        ])

        try await service.verifyConnection(for: .openai)
        let updatedConnection = await service.testConnection(for: .openai)
        let segments = try await service.translate(
            segments: ["Hello"],
            to: "zh-Hans",
            preferredEngine: .openai,
            from: "en",
            fallbackEnabled: false
        )

        XCTAssertTrue(updatedConnection)
        XCTAssertEqual(segments.map(\.translated), ["Translated by gpt-updated"])
    }

    func testConnectionTestWithUnsavedConfigDoesNotReplaceTranslationProvider() async throws {
        let registry = TranslationEngineRegistry(
            registerBuiltInProviders: false,
            providerFactory: { _, config, _ in
                let model = config.options?.modelName ?? "missing"
                return MockTranslationProvider(
                    id: model,
                    name: model,
                    batchResults: [TranslationResult(sourceText: "Hello", translatedText: model, sourceLanguage: "en", targetLanguage: "zh-Hans")],
                    checkConnectionResult: true
                )
            }
        )
        let service = TranslationService(registry: registry, engineConfigsProvider: {
            [.openai: TranslationEngineConfig(id: .openai, isEnabled: true, options: EngineOptions(modelName: "saved"))]
        })
        let _: [BilingualSegment] = try await service.translate(
            segments: ["Hello"], to: "zh-Hans", preferredEngine: .openai, from: "en", fallbackEnabled: false
        )
        try await service.verifyConnection(for: .openai, config: TranslationEngineConfig(
            id: .openai, isEnabled: true, options: EngineOptions(modelName: "unsaved")
        ))
        let segments: [BilingualSegment] = try await service.translate(
            segments: ["Hello"], to: "zh-Hans", preferredEngine: .openai, from: "en", fallbackEnabled: false
        )
        XCTAssertEqual(segments.map(\.translated), ["saved"])
    }

    private func compatibleConfig(
        id: UUID = UUID(),
        displayName: String = "Test",
        modelName: String = "model"
    ) -> CompatibleTranslationProvider.CompatibleConfig {
        .init(
            id: id,
            displayName: displayName,
            baseURL: "http://localhost:8000/v1",
            modelName: modelName,
            hasAPIKey: false
        )
    }
}

private actor CreationCounter {
    private(set) var value = 0

    func next() -> Int {
        value += 1
        return value
    }
}

private actor CompatibleConfigStore {
    private var stored: [CompatibleTranslationProvider.CompatibleConfig]

    init(_ stored: [CompatibleTranslationProvider.CompatibleConfig]) {
        self.stored = stored
    }

    func configs() -> [CompatibleTranslationProvider.CompatibleConfig] {
        stored
    }

    func update(_ newConfigs: [CompatibleTranslationProvider.CompatibleConfig]) {
        stored = newConfigs
    }
}

private actor EngineConfigStore {
    private var stored: [TranslationEngineType: TranslationEngineConfig]

    init(_ stored: [TranslationEngineType: TranslationEngineConfig]) {
        self.stored = stored
    }

    func configs() -> [TranslationEngineType: TranslationEngineConfig] {
        stored
    }

    func update(_ newConfigs: [TranslationEngineType: TranslationEngineConfig]) {
        stored = newConfigs
    }
}

private actor AsyncGate {
    private var enteredCount = 0
    private var enteredWaiters: [(target: Int, continuation: CheckedContinuation<Void, Never>)] = []
    private var releaseWaiters: [CheckedContinuation<Void, Never>] = []
    private var isOpen = false

    func enterAndWait() async {
        enteredCount += 1
        flushEnteredWaiters()
        guard !isOpen else { return }
        await withCheckedContinuation { continuation in
            releaseWaiters.append(continuation)
        }
    }

    func waitUntilEntered(count: Int) async {
        guard enteredCount < count else { return }
        await withCheckedContinuation { continuation in
            enteredWaiters.append((count, continuation))
        }
    }

    func resumeAll() {
        isOpen = true
        let waiters = releaseWaiters
        releaseWaiters.removeAll()
        for waiter in waiters {
            waiter.resume()
        }
    }

    private func flushEnteredWaiters() {
        var remaining: [(target: Int, continuation: CheckedContinuation<Void, Never>)] = []
        for waiter in enteredWaiters {
            if enteredCount >= waiter.target {
                waiter.continuation.resume()
            } else {
                remaining.append(waiter)
            }
        }
        enteredWaiters = remaining
    }
}
