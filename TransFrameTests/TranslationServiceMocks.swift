import XCTest
@testable import TransFrame

@available(macOS 13.0, *)
actor MockTranslationProvider: TranslationProvider, TranslationPromptConfigurable, TranslationPromptContextProviding {
    struct Request: Sendable, Equatable {
        let texts: [String]
        let sourceLanguage: String?
        let targetLanguage: String
    }

    nonisolated let id: String
    nonisolated let name: String

    private var available: Bool
    private var translateError: Error?
    private var batchResults: [TranslationResult]
    private var checkConnectionResult: Bool
    private var promptContextID: String?
    private(set) var requests: [Request] = []
    private(set) var promptTemplates: [String?] = []

    init(
        id: String,
        name: String,
        available: Bool = true,
        batchResults: [TranslationResult] = [],
        translateError: Error? = nil,
        checkConnectionResult: Bool = true,
        promptContextID: String? = nil
    ) {
        self.id = id
        self.name = name
        self.available = available
        self.batchResults = batchResults
        self.translateError = translateError
        self.checkConnectionResult = checkConnectionResult
        self.promptContextID = promptContextID
    }

    var isAvailable: Bool {
        get async { available }
    }

    func translate(
        text: String,
        from sourceLanguage: String?,
        to targetLanguage: String
    ) async throws -> TranslationResult {
        let results = try await translate(
            texts: [text],
            from: sourceLanguage,
            to: targetLanguage
        )
        guard let result = results.first else {
            XCTFail("MockTranslationProvider returned no results for a single-text request")
            throw TranslationProviderError.translationFailed("MockTranslationProvider returned no results")
        }
        return result
    }

    func translate(
        texts: [String],
        from sourceLanguage: String?,
        to targetLanguage: String
    ) async throws -> [TranslationResult] {
        try await translate(
            texts: texts,
            from: sourceLanguage,
            to: targetLanguage,
            promptTemplate: nil
        )
    }

    func translate(
        texts: [String],
        from sourceLanguage: String?,
        to targetLanguage: String,
        promptTemplate: String?
    ) async throws -> [TranslationResult] {
        requests.append(
            Request(texts: texts, sourceLanguage: sourceLanguage, targetLanguage: targetLanguage)
        )
        promptTemplates.append(promptTemplate)

        if let translateError {
            throw translateError
        }

        if batchResults.count == texts.count {
            return batchResults
        }

        if batchResults.count == 1, let first = batchResults.first {
            return texts.map { text in
                TranslationResult(
                    sourceText: text,
                    translatedText: first.translatedText,
                    sourceLanguage: first.sourceLanguage,
                    targetLanguage: first.targetLanguage
                )
            }
        }

        return texts.map { text in
            TranslationResult(
                sourceText: text,
                translatedText: "\(text) -> \(targetLanguage)",
                sourceLanguage: sourceLanguage ?? "Auto",
                targetLanguage: targetLanguage
            )
        }
    }

    func checkConnection() async -> Bool {
        checkConnectionResult
    }

    func verifyConnection() async throws {
        if let translateError {
            throw translateError
        }
    }

    func requestCount() async -> Int {
        requests.count
    }

    func lastPromptTemplate() async -> String? {
        promptTemplates.last.flatMap { $0 }
    }

    func compatiblePromptIdentifier() async -> String? {
        promptContextID
    }
}

@available(macOS 13.0, *)
actor MockTranslationServicing: TranslationServicing {
    struct Request: Sendable, Equatable {
        let segments: [String]
        let targetLanguage: String
        let preferredEngine: TranslationEngineType
        let sourceLanguage: String?
        let scene: TranslationScene?
        let mode: EngineSelectionMode
        let fallbackEnabled: Bool
        let parallelEngines: [TranslationEngineType]
        let sceneBindings: [TranslationScene: SceneEngineBinding]
    }

    private var nextResult: [BilingualSegment]
    private var nextError: Error?
    private(set) var requests: [Request] = []

    init(nextResult: [BilingualSegment] = [], nextError: Error? = nil) {
        self.nextResult = nextResult
        self.nextError = nextError
    }

    func translate(
        segments: [String],
        to targetLanguage: String,
        preferredEngine: TranslationEngineType,
        from sourceLanguage: String?,
        scene: TranslationScene?,
        mode: EngineSelectionMode,
        fallbackEnabled: Bool,
        parallelEngines: [TranslationEngineType],
        sceneBindings: [TranslationScene: SceneEngineBinding]
    ) async throws -> [BilingualSegment] {
        requests.append(
            Request(
                segments: segments,
                targetLanguage: targetLanguage,
                preferredEngine: preferredEngine,
                sourceLanguage: sourceLanguage,
                scene: scene,
                mode: mode,
                fallbackEnabled: fallbackEnabled,
                parallelEngines: parallelEngines,
                sceneBindings: sceneBindings
            )
        )

        if let nextError {
            throw nextError
        }

        return nextResult
    }

    /// Real V2 recording: independent of the protocol default's legacy
    // flattening, records the chain it was called with (PR #92 review).
    private(set) var orderedRequests: [[EngineChainItem]] = []

    func translateOrderedV2(
        segments: [String],
        to targetLanguage: String,
        from sourceLanguage: String?,
        scene: TranslationScene?,
        chain: [EngineChainItem]
    ) async throws -> TranslationResultBundle {
        orderedRequests.append(chain)
        if let nextError {
            throw nextError
        }
        return TranslationResultBundle(
            results: [EngineResult(
                engine: chain.first?.resolvedEngine ?? .apple,
                segments: nextResult,
                latency: 0
            )],
            primaryEngine: chain.first?.resolvedEngine ?? .apple,
            selectionMode: .primaryWithFallback,
            scene: scene
        )
    }

    func requestCount() async -> Int {
        requests.count
    }
}
