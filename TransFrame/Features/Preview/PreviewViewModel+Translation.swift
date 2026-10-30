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

        translationTask?.cancel()
        analysisGeneration &+= 1
        let generation = analysisGeneration
        translationTask = Task { @MainActor in
            defer {
                if generation == self.analysisGeneration {
                    self.isTranslating = false
                }
            }

            let pipeline = ImageTranslationPipeline()
            let context = PipelineContext()

            let analysis: ScreenAnalysisResult
            do {
                analysis = try await pipeline.analyze(image: image, context: context)
            } catch {
                guard generation == self.analysisGeneration else { return }
                let message = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
                errorMessage = String(format: String(localized: "error.pipeline.ocrFailed"), message)
                ocrTranslationError = errorMessage
                clearError()
                return
            }

            guard generation == self.analysisGeneration else { return }
            let filtered = analysis.filteredForTranslation()
            guard !filtered.segments.isEmpty else {
                errorMessage = String(localized: "error.ocr.no.text")
                clearError()
                return
            }

            let config = ImageTranslationPipelineConfig.fromAppSettings(scene: nil)
            do {
                let bundle = try await pipeline.translateBundle(
                    analysisResult: filtered,
                    config: config,
                    context: context
                )
                guard generation == self.analysisGeneration else { return }
                if config.compareEnginesV2 != nil, bundle.results.count > 1 {
                    // Compare mode: keep ALL entries (failures included) so the
                    // chips can show per-engine status; display follows the
                    // primary (first successful) result (PR #93 review).
                    compareResults = bundle.results
                    selectedCompareEngineId = bundle.results.first(where: { $0.isSuccess })?.id
                    bilingualSegments = bundle.primaryResult
                } else {
                    compareResults = []
                    selectedCompareEngineId = nil
                    bilingualSegments = bundle.primaryResult
                }
            } catch {
                guard generation == self.analysisGeneration else { return }
                let message = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
                errorMessage = String(format: String(localized: "error.pipeline.translateFailed"), message)
                ocrTranslationError = errorMessage
                clearError()
            }
        }
    }
}
