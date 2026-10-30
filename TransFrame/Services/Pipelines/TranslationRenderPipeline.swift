import CoreGraphics
import Foundation
import os

@available(macOS 13.0, *)
actor TranslationRenderPipeline {
    typealias Renderer = @Sendable (CGImage, [BilingualSegment], OverlayTheme) -> CGImage?

    private let translationService: any TranslationServicing
    private let renderer: Renderer
    private let recorder: PerformanceRecorder

    init(
        translationService: any TranslationServicing = TranslationService.shared,
        renderer: @escaping Renderer = { image, segments, theme in
            OverlayRenderer().render(image: image, segments: segments, theme: theme)
        },
        recorder: PerformanceRecorder = .shared
    ) {
        self.translationService = translationService
        self.renderer = renderer
        self.recorder = recorder
    }

    func translate(
        analysisResult: ScreenAnalysisResult,
        config: ImageTranslationPipelineConfig,
        context: PipelineContext
    ) async throws -> [BilingualSegment] {
        do {
            return try await recorder.measure(stage: .translation, operationID: context.operationID) {
                try Task.checkCancellation()
                // Merge same-line fragments first: every visual line is
                // translated as one unit and gets one overlay anchor.
                let groupedSegments = VisualLineGrouping.mergeLines(in: analysisResult.segments)
                let texts = groupedSegments.map(\.text)
                let translatedSegments: [BilingualSegment]
                if let chain = config.orderedChainV2 {
                    // V2 ordered mode (Phase 3): sequential fallback chain
                    let bundle = try await translationService.translateOrderedV2(
                        segments: texts,
                        to: config.targetLanguage,
                        from: config.sourceLanguage,
                        scene: config.scene,
                        chain: chain
                    )
                    translatedSegments = bundle.primaryResult
                } else if let compareEngines = config.compareEnginesV2 {
                    // V2 compare mode (Phase 4): parallel compare, primary wins
                    let bundle = try await translationService.translateCompareV2(
                        segments: texts,
                        to: config.targetLanguage,
                        from: config.sourceLanguage,
                        scene: config.scene,
                        engines: compareEngines
                    )
                    translatedSegments = bundle.primaryResult
                } else {
                    translatedSegments = try await translationService.translate(
                        segments: texts,
                        to: config.targetLanguage,
                        preferredEngine: config.preferredEngine,
                        from: config.sourceLanguage,
                        scene: config.scene,
                        mode: config.mode,
                        fallbackEnabled: config.fallbackEnabled,
                        parallelEngines: config.parallelEngines,
                        sceneBindings: config.sceneBindings
                    )
                }

                guard translatedSegments.count == groupedSegments.count else {
                    throw PipelineError.translationFailed(
                        "Translation segment count mismatch: expected \(groupedSegments.count), got \(translatedSegments.count)"
                    )
                }

                return zip(groupedSegments, translatedSegments).map { original, translated in
                    BilingualSegment(
                        segment: original,
                        translatedText: translated.translated,
                        sourceLanguage: translated.sourceLanguage,
                        targetLanguage: translated.targetLanguage
                    )
                }
            }
        } catch {
            throw PipelineError(stage: .translation, error: error)
        }
    }

    func translateBundle(
        text: String,
        config: TextTranslationConfig,
        context: PipelineContext
    ) async throws -> TranslationResultBundle {
        do {
            return try await recorder.measure(stage: .translation, operationID: context.operationID) {
                try Task.checkCancellation()
                // V2 dispatch (Phase 4): ordered chain or compare set
                if let v2 = config.v2Selection {
                    switch v2.mode {
                    case .ordered:
                        return try await translationService.translateOrderedV2(
                            segments: [text],
                            to: config.targetLanguage,
                            from: config.sourceLanguage,
                            scene: config.scene,
                            chain: v2.entries
                        )
                    case .compare:
                        return try await translationService.translateCompareV2(
                            segments: [text],
                            to: config.targetLanguage,
                            from: config.sourceLanguage,
                            scene: config.scene,
                            engines: v2.entries
                        )
                    }
                }
                return try await translationService.translateBundle(
                    segments: [text],
                    to: config.targetLanguage,
                    preferredEngine: config.preferredEngine,
                    from: config.sourceLanguage,
                    scene: config.scene,
                    mode: config.mode,
                    fallbackEnabled: config.fallbackEnabled,
                    parallelEngines: config.parallelEngines,
                    sceneBindings: config.sceneBindings
                )
            }
        } catch {
            throw PipelineError(stage: .translation, error: error)
        }
    }

    func translateBundle(
        analysisResult: ScreenAnalysisResult,
        config: ImageTranslationPipelineConfig,
        context: PipelineContext
    ) async throws -> TranslationResultBundle {
        do {
            return try await recorder.measure(stage: .translation, operationID: context.operationID) {
                try Task.checkCancellation()
                let groupedSegments = VisualLineGrouping.mergeLines(in: analysisResult.segments)
                let texts = groupedSegments.map(\.text)

                // Ambiguity guard: both V2 arrays set means a hand-built
                // config; ordered would silently win — log for diagnosis.
                if config.orderedChainV2 != nil && config.compareEnginesV2 != nil {
                    Logger.pipeline.warning("config carries both V2 selections; ordered wins")
                }
                if let v2Ordered = config.orderedChainV2 {
                    let bundle = try await translationService.translateOrderedV2(
                        segments: texts,
                        to: config.targetLanguage,
                        from: config.sourceLanguage,
                        scene: config.scene,
                        chain: v2Ordered
                    )
                    return try Self.reanchor(bundle, onto: groupedSegments)
                }
                if let v2Compare = config.compareEnginesV2 {
                    let bundle = try await translationService.translateCompareV2(
                        segments: texts,
                        to: config.targetLanguage,
                        from: config.sourceLanguage,
                        scene: config.scene,
                        engines: v2Compare
                    )
                    return try Self.reanchor(bundle, onto: groupedSegments)
                }
                let translatedSegments = try await translationService.translate(
                    segments: texts,
                    to: config.targetLanguage,
                    preferredEngine: config.preferredEngine,
                    from: config.sourceLanguage,
                    scene: config.scene,
                    mode: config.mode,
                    fallbackEnabled: config.fallbackEnabled,
                    parallelEngines: config.parallelEngines,
                    sceneBindings: config.sceneBindings
                )
                guard translatedSegments.count == groupedSegments.count else {
                    throw PipelineError.translationFailed(
                        "Translation segment count mismatch: expected \(groupedSegments.count), got \(translatedSegments.count)"
                    )
                }
                let anchored = zip(groupedSegments, translatedSegments).map { original, translated in
                    BilingualSegment(
                        segment: original,
                        translatedText: translated.translated,
                        sourceLanguage: translated.sourceLanguage,
                        targetLanguage: translated.targetLanguage
                    )
                }
                return TranslationResultBundle(
                    results: [EngineResult(
                        engine: config.preferredEngine,
                        segments: anchored,
                        latency: 0
                    )],
                    primaryEngine: config.preferredEngine,
                    selectionMode: config.mode,
                    scene: config.scene
                )
            }
        } catch {
            throw PipelineError(stage: .translation, error: error)
        }
    }

    /// Re-anchor V2 bundle results onto merged visual lines: each engine's
    /// plain translations are zipped back onto the grouped segments (geometry
    /// anchors) with a count guard, mirroring the legacy translate path —
    /// compare-mode preview must not bypass line merging/validation.
    static func reanchor(
        _ bundle: TranslationResultBundle,
        onto groupedSegments: [TextSegment]
    ) throws -> TranslationResultBundle {
        var reanchored: [EngineResult] = []
        for result in bundle.results {
            guard result.isSuccess else {
                reanchored.append(result)
                continue
            }
            guard result.segments.count == groupedSegments.count else {
                reanchored.append(EngineResult.failed(
                    engine: result.engine,
                    error: PipelineError.translationFailed(
                        "Translation segment count mismatch: expected \(groupedSegments.count), got \(result.segments.count)"
                    )
                ))
                continue
            }
            let anchored = zip(groupedSegments, result.segments).map { original, translated in
                BilingualSegment(
                    segment: original,
                    translatedText: translated.translated,
                    sourceLanguage: translated.sourceLanguage,
                    targetLanguage: translated.targetLanguage
                )
            }
            reanchored.append(EngineResult(
                engine: result.engine,
                segments: anchored,
                latency: result.latency
            ))
        }
        guard reanchored.contains(where: { $0.isSuccess }) else {
            throw MultiEngineError.noResults
        }
        let primary = reanchored.first(where: { $0.isSuccess }) ?? reanchored[0]
        return TranslationResultBundle(
            results: reanchored,
            primaryEngine: primary.engine,
            selectionMode: bundle.selectionMode,
            scene: bundle.scene
        )
    }

    func render(
        image: CGImage,
        segments: [BilingualSegment],
        theme: OverlayTheme,
        context: PipelineContext
    ) async throws -> CGImage {
        do {
            return try await recorder.measure(stage: .render, operationID: context.operationID) {
                try Task.checkCancellation()
                guard let renderedImage = renderer(image, segments, theme) else {
                    throw PipelineError.renderFailed("Failed to render overlay")
                }
                return renderedImage
            }
        } catch {
            throw PipelineError(stage: .render, error: error)
        }
    }
}
