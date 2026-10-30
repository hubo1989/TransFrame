import CoreGraphics
import Foundation

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
