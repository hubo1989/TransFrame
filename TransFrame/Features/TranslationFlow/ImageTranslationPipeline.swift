import CoreGraphics
import Foundation

struct ImageTranslationPipelineConfig: Sendable, Equatable {
    let targetLanguage: String
    let sourceLanguage: String?
    let preferredEngine: TranslationEngineType
    let scene: TranslationScene?
    let mode: EngineSelectionMode
    let fallbackEnabled: Bool
    let parallelEngines: [TranslationEngineType]
    let sceneBindings: [TranslationScene: SceneEngineBinding]
    /// V2 ordered chain: when non-nil and engineModeV2 == .ordered, the
    /// render pipeline routes through translateOrderedV2 (Phase 3).
    var orderedChainV2: [EngineChainItem]? = nil

    @MainActor
    static func fromAppSettings(scene: TranslationScene? = nil) -> ImageTranslationPipelineConfig {
        let settings = AppSettings.shared
        // Resolve preferred engine consistently with TextTranslationFlow:
        // sceneBinding uses scene binding; all other modes use parallelEngines[0].
        let preferredEngine: TranslationEngineType
        if settings.engineSelectionMode == .sceneBinding, let scene {
            preferredEngine = settings.sceneBindings[scene]?.primaryEngine
                ?? SceneEngineBinding.default(for: scene).primaryEngine
        } else if let first = settings.parallelEngines.first {
            preferredEngine = first
        } else {
            preferredEngine = settings.translationEngine
        }
        return ImageTranslationPipelineConfig(
            targetLanguage: settings.translationTargetLanguage?.rawValue ?? "zh-Hans",
            sourceLanguage: settings.translationSourceLanguage == .auto
                ? nil
                : settings.translationSourceLanguage.rawValue,
            preferredEngine: preferredEngine,
            scene: scene,
            mode: settings.engineSelectionMode,
            fallbackEnabled: settings.translationFallbackEnabled,
            parallelEngines: settings.parallelEngines,
            sceneBindings: settings.sceneBindings,
            orderedChainV2: settings.engineModeV2 == .ordered ? settings.engineChainV2 : nil
        )
    }
}

struct ImageTranslationPipelineOutput: Sendable, Equatable {
    let originalImage: CGImage
    let renderedImage: CGImage
    let analysisResult: ScreenAnalysisResult
    let filteredAnalysisResult: ScreenAnalysisResult
    let segments: [BilingualSegment]
    let processingTime: TimeInterval

    var flowResult: TranslationFlowResult {
        TranslationFlowResult(
            originalImage: originalImage,
            renderedImage: renderedImage,
            segments: segments,
            processingTime: processingTime
        )
    }
}

@available(macOS 13.0, *)
final class ImageTranslationPipeline: Sendable {
    typealias Analyzer = @Sendable (CGImage) async throws -> ScreenAnalysisResult
    typealias OCRFallback = @Sendable (CGImage) async throws -> OCRResult
    typealias Renderer = @Sendable (CGImage, [BilingualSegment], OverlayTheme) -> CGImage?

    private let analysisPipeline: AnalysisPipeline
    private let translationRenderPipeline: TranslationRenderPipeline

    init(
        analyzer: @escaping Analyzer = { image in
            try await ScreenCoderEngine.shared.analyze(image: image)
        },
        ocrFallback: @escaping OCRFallback = { image in
            try await OCRService.shared.recognize(image)
        },
        translationService: any TranslationServicing = TranslationService.shared,
        renderer: @escaping Renderer = { image, segments, theme in
            OverlayRenderer().render(image: image, segments: segments, theme: theme)
        },
        recorder: PerformanceRecorder = .shared
    ) {
        self.analysisPipeline = AnalysisPipeline(
            analyzer: analyzer,
            ocrFallback: ocrFallback,
            recorder: recorder
        )
        self.translationRenderPipeline = TranslationRenderPipeline(
            translationService: translationService,
            renderer: renderer,
            recorder: recorder
        )
    }

    func run(
        image: CGImage,
        config: ImageTranslationPipelineConfig,
        theme: OverlayTheme,
        context: PipelineContext = PipelineContext()
    ) async throws -> ImageTranslationPipelineOutput {
        let analysisResult = try await analyze(image: image, context: context)
        let filteredAnalysisResult = analysisResult.filteredForTranslation()
        guard !filteredAnalysisResult.segments.isEmpty else {
            throw PipelineError.noTextFound
        }

        let bilingualSegments = try await translate(
            analysisResult: filteredAnalysisResult,
            config: config,
            context: context
        )

        let renderedImage = try await render(
            image: image,
            segments: bilingualSegments,
            theme: theme,
            context: context
        )

        return ImageTranslationPipelineOutput(
            originalImage: image,
            renderedImage: renderedImage,
            analysisResult: analysisResult,
            filteredAnalysisResult: filteredAnalysisResult,
            segments: bilingualSegments,
            processingTime: context.elapsed
        )
    }

    func analyze(image: CGImage, context: PipelineContext) async throws -> ScreenAnalysisResult {
        try await analysisPipeline.analyze(image: image, context: context)
    }

    func translate(
        analysisResult: ScreenAnalysisResult,
        config: ImageTranslationPipelineConfig,
        context: PipelineContext
    ) async throws -> [BilingualSegment] {
        try await translationRenderPipeline.translate(
            analysisResult: analysisResult,
            config: config,
            context: context
        )
    }

    func render(
        image: CGImage,
        segments: [BilingualSegment],
        theme: OverlayTheme,
        context: PipelineContext
    ) async throws -> CGImage {
        try await translationRenderPipeline.render(
            image: image,
            segments: segments,
            theme: theme,
            context: context
        )
    }

    static func recoverAnalysisResultIfNeeded(
        _ analysisResult: ScreenAnalysisResult,
        ocrFallback: @Sendable () async throws -> OCRResult
    ) async throws -> ScreenAnalysisResult {
        try await AnalysisPipeline.recoverAnalysisResultIfNeeded(
            analysisResult,
            ocrFallback: ocrFallback
        )
    }
}
