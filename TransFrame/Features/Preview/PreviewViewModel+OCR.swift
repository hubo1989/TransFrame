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

        Task {
            await executeOCR()
        }
    }

    func executeOCR() async {
        defer { isPerformingOCR = false }

        // Recognize in the user's source language; auto (or an unsupported
        // language) falls back to Vision's automatic language detection.
        let languages = Set(
            [AppSettings.shared.translationSourceLanguage]
                .compactMap { OCREngine.RecognitionLanguage(translationLanguage: $0) }
        )

        do {
            let result = try await ocrService.recognize(
                image,
                languages: languages
            )
            ocrResult = result
            if result.observations.isEmpty {
                errorMessage = String(localized: "error.ocr.no.text")
            }
        } catch {
            let message = "\(String(localized: "error.ocr.failed")) \(error.localizedDescription)"
            ocrTranslationError = message
            errorMessage = message
        }
    }
}
