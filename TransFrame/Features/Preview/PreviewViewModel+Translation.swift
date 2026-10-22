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

        isTranslating = true
        ocrTranslationError = nil

        Task { @MainActor in
            defer { isTranslating = false }

            do {
                let pipeline = ImageTranslationPipeline()
                let context = PipelineContext()

                let analysis = try await pipeline.analyze(image: image, context: context)
                let filtered = analysis.filteredForTranslation()
                guard !filtered.segments.isEmpty else {
                    errorMessage = String(localized: "error.ocr.no.text")
                    clearError()
                    return
                }

                let config = ImageTranslationPipelineConfig.fromAppSettings(scene: nil)
                bilingualSegments = try await pipeline.translate(
                    analysisResult: filtered,
                    config: config,
                    context: context
                )
            } catch {
                let message = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
                ocrTranslationError = message
                errorMessage = message
                clearError()
            }
        }
    }
}
