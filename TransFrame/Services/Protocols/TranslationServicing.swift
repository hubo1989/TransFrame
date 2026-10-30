//
//  TranslationServicing.swift
//  TransFrame
//
//  Protocol abstraction for TranslationService to enable testing
//

import Foundation

/// Protocol for translation service operations.
/// Provides abstraction for testing and dependency injection.
protocol TranslationServicing: Sendable {
    /// Translates segments using the provided routing configuration
    /// - Parameters:
    ///   - segments: Source texts to translate
    ///   - targetLanguage: Target language code
    ///   - preferredEngine: Primary engine for modes that need one
    ///   - sourceLanguage: Source language code (nil for auto-detect)
    ///   - mode: Engine selection mode
    ///   - fallbackEnabled: Whether fallback should be attempted
    ///   - parallelEngines: Engines to run in parallel mode
    ///   - sceneBindings: Scene-specific routing bindings
    /// - Returns: Array of bilingual segments with source and translated text
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
    ) async throws -> [BilingualSegment]

    /// Translates segments and returns a full result bundle with per-engine details.
    /// Use this when you need information about which engines succeeded/failed.
    func translateBundle(
        segments: [String],
        to targetLanguage: String,
        preferredEngine: TranslationEngineType,
        from sourceLanguage: String?,
        scene: TranslationScene?,
        mode: EngineSelectionMode,
        fallbackEnabled: Bool,
        parallelEngines: [TranslationEngineType],
        sceneBindings: [TranslationScene: SceneEngineBinding]
    ) async throws -> TranslationResultBundle

    /// V2 ordered mode (Phase 3): sequential fallback chain.
    func translateOrderedV2(
        segments: [String],
        to targetLanguage: String,
        from sourceLanguage: String?,
        scene: TranslationScene?,
        chain: [EngineChainItem]
    ) async throws -> TranslationResultBundle

    /// V2 compare mode (Phase 4): run entries in parallel, one result each.
    func translateCompareV2(
        segments: [String],
        to targetLanguage: String,
        from sourceLanguage: String?,
        scene: TranslationScene?,
        engines: [EngineChainItem]
    ) async throws -> TranslationResultBundle
}

// MARK: - Default Implementation

extension TranslationServicing {
    /// Default V2 ordered implementation: routes down to legacy translate so
    /// existing mocks/tests keep working without knowing about V2 chains.
    /// The real TranslationService overrides this with chain semantics.
    func translateOrderedV2(
        segments: [String],
        to targetLanguage: String,
        from sourceLanguage: String? = nil,
        scene: TranslationScene? = nil,
        chain: [EngineChainItem]
    ) async throws -> TranslationResultBundle {
        let bilingualSegments = try await translate(
            segments: segments,
            to: targetLanguage,
            preferredEngine: chain.first?.resolvedEngine ?? .apple,
            from: sourceLanguage,
            scene: scene,
            mode: .primaryWithFallback,
            fallbackEnabled: true,
            parallelEngines: chain.compactMap {
                if case .builtin(let engine) = $0.kind { return engine } else { return nil }
            },
            sceneBindings: [:]
        )
        return TranslationResultBundle(
            results: [EngineResult(
                engine: chain.first?.resolvedEngine ?? .apple,
                segments: bilingualSegments,
                latency: 0
            )],
            primaryEngine: chain.first?.resolvedEngine ?? .apple,
            selectionMode: .primaryWithFallback,
            scene: scene
        )
    }

    /// Default V2 compare implementation: same forwarding shape as ordered.
    func translateCompareV2(
        segments: [String],
        to targetLanguage: String,
        from sourceLanguage: String? = nil,
        scene: TranslationScene? = nil,
        engines: [EngineChainItem]
    ) async throws -> TranslationResultBundle {
        try await translateOrderedV2(
            segments: segments,
            to: targetLanguage,
            from: sourceLanguage,
            scene: scene,
            chain: engines
        )
    }

    func translateBundle(
        segments: [String],
        to targetLanguage: String,
        preferredEngine: TranslationEngineType = .apple,
        from sourceLanguage: String? = nil,
        scene: TranslationScene? = nil,
        mode: EngineSelectionMode = .primaryWithFallback,
        fallbackEnabled: Bool = true,
        parallelEngines: [TranslationEngineType] = [],
        sceneBindings: [TranslationScene: SceneEngineBinding] = [:]
    ) async throws -> TranslationResultBundle {
        let bilingualSegments = try await translate(
            segments: segments,
            to: targetLanguage,
            preferredEngine: preferredEngine,
            from: sourceLanguage,
            scene: scene,
            mode: mode,
            fallbackEnabled: fallbackEnabled,
            parallelEngines: parallelEngines,
            sceneBindings: sceneBindings
        )
        return TranslationResultBundle(
            results: [EngineResult(engine: preferredEngine, segments: bilingualSegments, latency: 0)],
            primaryEngine: preferredEngine,
            selectionMode: mode,
            scene: scene
        )
    }
}

// MARK: - TranslationService Conformance

@available(macOS 13.0, *)
extension TranslationService: TranslationServicing {
    func translateBundle(
        segments: [String],
        to targetLanguage: String,
        preferredEngine: TranslationEngineType = .apple,
        from sourceLanguage: String? = nil,
        scene: TranslationScene? = nil,
        mode: EngineSelectionMode = .primaryWithFallback,
        fallbackEnabled: Bool = true,
        parallelEngines: [TranslationEngineType] = [],
        sceneBindings: [TranslationScene: SceneEngineBinding] = [:]
    ) async throws -> TranslationResultBundle {
        return try await translate(
            segments: segments,
            to: targetLanguage,
            from: sourceLanguage,
            scene: scene,
            mode: mode,
            preferredEngine: preferredEngine,
            fallbackEnabled: fallbackEnabled,
            parallelEngines: parallelEngines,
            sceneBindings: sceneBindings
        )
    }
}
