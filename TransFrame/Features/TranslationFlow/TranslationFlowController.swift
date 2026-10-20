import AppKit
import Observation
import os.log

/// 翻译流程阶段
enum TranslationFlowPhase: Sendable, Equatable {
    case idle
    case analyzing
    case translating
    case rendering
    case completed
    case failed(TranslationFlowError)

    var isProcessing: Bool {
        switch self {
        case .analyzing, .translating, .rendering:
            return true
        default:
            return false
        }
    }

    var localizedDescription: String {
        switch self {
        case .idle:
            return String(localized: "translationFlow.phase.idle")
        case .analyzing:
            return String(localized: "translationFlow.phase.analyzing")
        case .translating:
            return String(localized: "translationFlow.phase.translating")
        case .rendering:
            return String(localized: "translationFlow.phase.rendering")
        case .completed:
            return String(localized: "translationFlow.phase.completed")
        case .failed:
            return String(localized: "translationFlow.phase.failed")
        }
    }

    var progress: Double {
        switch self {
        case .idle: return 0.0
        case .analyzing: return 0.25
        case .translating: return 0.50
        case .rendering: return 0.75
        case .completed: return 1.0
        case .failed: return 0.0
        }
    }
}

/// 翻译流程错误
enum TranslationFlowError: LocalizedError, Sendable, Equatable {
    case analysisFailure(String)
    case translationFailure(String)
    case renderingFailure(String)
    case cancelled
    case noTextFound

    var errorDescription: String? {
        switch self {
        case .analysisFailure(let message):
            return String(format: NSLocalizedString("translationFlow.error.analysis", comment: ""), message)
        case .translationFailure(let message):
            return String(format: NSLocalizedString("translationFlow.error.translation", comment: ""), message)
        case .renderingFailure(let message):
            return String(format: NSLocalizedString("translationFlow.error.rendering", comment: ""), message)
        case .cancelled:
            return String(localized: "translationFlow.error.cancelled")
        case .noTextFound:
            return String(localized: "translationFlow.error.noTextFound")
        }
    }

    var recoverySuggestion: String? {
        switch self {
        case .analysisFailure:
            return String(localized: "translationFlow.recovery.analysis")
        case .translationFailure:
            return String(localized: "translationFlow.recovery.translation")
        case .renderingFailure:
            return String(localized: "translationFlow.recovery.rendering")
        case .cancelled:
            return nil
        case .noTextFound:
            return String(localized: "translationFlow.recovery.noTextFound")
        }
    }
}

/// 翻译流程结果
struct TranslationFlowResult: Sendable {
    let originalImage: CGImage
    let renderedImage: CGImage
    let segments: [BilingualSegment]
    let processingTime: TimeInterval
}

/// 翻译流程控制器 - 协调整个翻译流程
@MainActor
@Observable
final class TranslationFlowController {
    static let shared = TranslationFlowController()

    // MARK: - Observable State

    private(set) var currentPhase: TranslationFlowPhase = .idle
    private(set) var lastError: TranslationFlowError?
    private(set) var lastResult: TranslationFlowResult?

    // MARK: - Private

    private var currentTask: Task<Void, Never>?
    private let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "TransFrame", category: "TranslationFlow")
    private let pipeline = ImageTranslationPipeline()

    private init() {}

    // MARK: - Public API

    func startTranslation(image: CGImage, scaleFactor: CGFloat) {
        cancel()

        currentTask = Task {
            await performTranslation(image: image, scaleFactor: scaleFactor)
        }
    }

    func cancel() {
        currentTask?.cancel()
        currentTask = nil

        if currentPhase.isProcessing {
            currentPhase = .failed(.cancelled)
            lastError = .cancelled
        }
    }

    func reset() {
        cancel()
        currentPhase = .idle
        lastError = nil
        lastResult = nil
    }

    // MARK: - Private Implementation

    private func performTranslation(image: CGImage, scaleFactor: CGFloat) async {
        let context = PipelineContext()
        lastError = nil
        lastResult = nil

        // Show loading window immediately with original image
        await MainActor.run {
            BilingualResultWindowController.shared.showLoading(
                originalImage: image,
                scaleFactor: scaleFactor,
                message: String(localized: "bilingualResult.loading.analyzing")
            )
        }

        // Phase 1: 分析图像
        currentPhase = .analyzing

        let analysisResult: ScreenAnalysisResult
        let filteredAnalysisResult: ScreenAnalysisResult
        do {
            try Task.checkCancellation()
            analysisResult = try await pipeline.analyze(image: image, context: context)
            filteredAnalysisResult = analysisResult.filteredForTranslation()
            if filteredAnalysisResult.segments.isEmpty {
                throw TranslationFlowError.noTextFound
            }
        } catch is CancellationError {
            handleCancellation()
            return
        } catch let error as TranslationFlowError {
            handleError(error)
            return
        } catch let error as PipelineError {
            handleError(Self.mapPipelineError(error))
            return
        } catch {
            logger.error("Analysis phase failed: \(String(describing: error))")
            let errorMessage = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
            handleError(.analysisFailure(errorMessage))
            return
        }

        // Phase 2: 翻译文本
        currentPhase = .translating

        let bilingualSegments: [BilingualSegment]
        do {
            try Task.checkCancellation()

            let config = ImageTranslationPipelineConfig.fromAppSettings(scene: nil)
            bilingualSegments = try await pipeline.translate(
                analysisResult: filteredAnalysisResult,
                config: config,
                context: context
            )
        } catch is CancellationError {
            handleCancellation()
            return
        } catch let error as TranslationFlowError {
            handleError(error)
            return
        } catch let error as PipelineError {
            handleError(Self.mapPipelineError(error))
            return
        } catch {
            logger.error("Translation phase failed: \(String(describing: error))")
            let errorMessage = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
            handleError(.translationFailure(errorMessage))
            return
        }

        // Phase 3: 渲染结果
        currentPhase = .rendering

        do {
            try Task.checkCancellation()

            // Get theme on main thread (OverlayTheme.current requires @MainActor)
            let theme = OverlayTheme.current
            let renderedImage = try await pipeline.render(
                image: image,
                segments: bilingualSegments,
                theme: theme,
                context: context
            )

            lastResult = TranslationFlowResult(
                originalImage: image,
                renderedImage: renderedImage,
                segments: bilingualSegments,
                processingTime: context.elapsed
            )

            currentPhase = .completed

            // Save to translation history
            await saveToHistory(
                originalImage: image,
                segments: bilingualSegments,
                renderedImage: renderedImage
            )

            showResultWindow(renderedImage: renderedImage, scaleFactor: scaleFactor)

        } catch is CancellationError {
            handleCancellation()
            return
        } catch let error as TranslationFlowError {
            handleError(error)
            return
        } catch let error as PipelineError {
            handleError(Self.mapPipelineError(error))
            return
        } catch {
            logger.error("Rendering phase failed: \(String(describing: error))")
            let errorMessage = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
            handleError(.renderingFailure(errorMessage))
            return
        }
    }

    private func handleCancellation() {
        currentPhase = .failed(.cancelled)
        lastError = .cancelled
        BilingualResultWindowController.shared.close()
    }

    private func handleError(_ error: TranslationFlowError) {
        currentPhase = .failed(error)
        lastError = error
        BilingualResultWindowController.shared.close()
        showErrorAlert(error)
    }

    private func showResultWindow(renderedImage: CGImage, scaleFactor: CGFloat) {
        // Get translated text from last result
        let translatedText = lastResult?.segments.map { $0.translated }.joined(separator: "\n")
        BilingualResultWindowController.shared.showResult(image: renderedImage, scaleFactor: scaleFactor, translatedText: translatedText)
    }

    static func recoverAnalysisResultIfNeeded(
        _ analysisResult: ScreenAnalysisResult,
        ocrFallback: @Sendable () async throws -> OCRResult
    ) async throws -> ScreenAnalysisResult {
        try await ImageTranslationPipeline.recoverAnalysisResultIfNeeded(
            analysisResult,
            ocrFallback: ocrFallback
        )
    }

    private static func mapPipelineError(_ error: PipelineError) -> TranslationFlowError {
        switch error {
        case .noTextFound:
            return .noTextFound
        case .captureFailed(let message):
            return .analysisFailure(message)
        case .analysisFailed(let message):
            return .analysisFailure(message)
        case .translationFailed(let message):
            return .translationFailure(message)
        case .renderFailed(let message):
            return .renderingFailure(message)
        case .historyFailed(let message):
            return .renderingFailure(message)
        case .exportFailed(let message):
            return .renderingFailure(message)
        case .cancelled:
            return .cancelled
        }
    }

    private func saveToHistory(
        originalImage: CGImage,
        segments: [BilingualSegment],
        renderedImage: CGImage
    ) async {
        // Combine all source text and translated text
        let sourceText = segments.map { $0.original.text }.joined(separator: "\n")
        let translatedText = segments.map { $0.translated }.joined(separator: "\n")

        // Get language info from first segment (all segments should have same language pair)
        let sourceLanguage = segments.first?.sourceLanguage ?? "auto"
        let targetLanguage = segments.first?.targetLanguage ?? "zh-Hans"

        // Create TranslationResult
        let result = TranslationResult(
            sourceText: sourceText,
            translatedText: translatedText,
            sourceLanguage: sourceLanguage,
            targetLanguage: targetLanguage
        )

        // Save to history with thumbnail
        await MainActor.run {
            HistoryWindowController.shared.addTranslation(
                result: result,
                image: originalImage
            )
        }
    }

    private func showErrorAlert(_ error: TranslationFlowError) {
        NSApp.activate(ignoringOtherApps: true)

        let alert = NSAlert()

        // Use different titles for different error types
        switch error {
        case .analysisFailure, .noTextFound:
            alert.messageText = String(localized: "translationFlow.error.title.analysis")
        case .translationFailure:
            alert.messageText = String(localized: "translationFlow.error.title.translation")
        case .renderingFailure:
            alert.messageText = String(localized: "translationFlow.error.title.rendering")
        case .cancelled:
            alert.messageText = String(localized: "translationFlow.error.title")
        }

        var errorDetails = error.errorDescription ?? String(localized: "translationFlow.error.unknown")

        // Add provider info for analysis errors
        if case .analysisFailure = error {
            let settings = AppSettings.shared
            errorDetails += "\n\nProvider: \(settings.vlmProvider.localizedName)"
            // Show appropriate model info based on provider type
            switch settings.vlmProvider {
            case .paddleocr:
                // For local/cloud PaddleOCR modes, model info is not applicable
                break
            default:
                errorDetails += "\nModel: \(settings.vlmModelName)"
            }
        }

        // Add provider info for translation errors
        if case .translationFailure = error {
            let settings = AppSettings.shared
            errorDetails += "\n\n" + String(localized: "translationFlow.error.translation.engine")
            errorDetails += ": \(settings.preferredTranslationEngine.rawValue)"
        }

        alert.informativeText = errorDetails

        if let recovery = error.recoverySuggestion {
            alert.informativeText += "\n\n" + recovery
        }
        alert.alertStyle = .warning
        alert.addButton(withTitle: String(localized: "common.ok"))
        alert.runModal()
    }
}
