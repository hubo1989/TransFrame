import Foundation
import SwiftUI
import AppKit

extension PreviewViewModel {
    func performOCR() {
        if isWaitingForTextInputInternal {
            commitTextInput()
        }
        guard !isPerformingOCR else { return }
        resultsPanelTab = .ocr
        isPerformingOCR = true
        ocrTranslationError = nil

        ocrTask?.cancel()
        analysisGeneration &+= 1
        let generation = analysisGeneration
        ocrTask = Task {
            await executeOCR(generation: generation)
        }
    }

    func executeOCR(generation: UInt64? = nil) async {
        let currentGeneration = generation ?? analysisGeneration
        defer {
            if currentGeneration == analysisGeneration {
                isPerformingOCR = false
            }
        }

        let languages = Set(
            [AppSettings.shared.translationSourceLanguage]
                .compactMap { OCREngine.RecognitionLanguage(translationLanguage: $0) }
        )

        do {
            let result = try await ocrService.recognize(
                image,
                languages: languages
            )
            guard currentGeneration == analysisGeneration else { return }
            ocrResult = result
            if result.observations.isEmpty {
                errorMessage = String(localized: "error.ocr.no.text")
            }
        } catch {
            guard currentGeneration == analysisGeneration else { return }
            let message = "\(String(localized: "error.ocr.failed")) \(error.localizedDescription)"
            ocrTranslationError = message
            errorMessage = message
        }
    }
}
