import Foundation
import SwiftUI

// MARK: - Embedded Translation (预览窗口内嵌翻译)

extension PreviewViewModel {
    var hasTranslationResults: Bool {
        !bilingualSegments.isEmpty
    }

    /// Runs the shared image translation pipeline (analyze + translate) and
    /// stores bilingual segments for the embedded side panel. No external window.
    func performTranslation() {
        guard !isTranslating else { return }
        if isWaitingForTextInputInternal {
            commitTextInput()
        }

        resultsPanelTab = .translate
        isTranslating = true
        ocrTranslationError = nil

        Task { @MainActor in
            defer { isTranslating = false }

            let pipeline = ImageTranslationPipeline()
            let context = PipelineContext()

            // Stage-split so failures are attributed correctly: analysis
            // (VLM) errors read as recognition, not translation failures.
            let analysis: ScreenAnalysisResult
            do {
                analysis = try await pipeline.analyze(image: image, context: context)
            } catch {
                let message = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
                errorMessage = String(format: String(localized: "error.pipeline.ocrFailed"), message)
                ocrTranslationError = errorMessage
                clearError()
                return
            }

            let filtered = analysis.filteredForTranslation()
            guard !filtered.segments.isEmpty else {
                errorMessage = String(localized: "error.ocr.no.text")
                clearError()
                return
            }

            let config = ImageTranslationPipelineConfig.fromAppSettings(scene: nil)
            do {
                bilingualSegments = try await pipeline.translate(
                    analysisResult: filtered,
                    config: config,
                    context: context
                )
            } catch {
                let message = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
                errorMessage = String(format: String(localized: "error.pipeline.translateFailed"), message)
                ocrTranslationError = errorMessage
                clearError()
            }
        }
    }
}
