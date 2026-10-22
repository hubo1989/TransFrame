import Foundation
import SwiftUI
import AppKit

extension PreviewViewModel {
    func performOCR() {
        if isWaitingForTextInputInternal {
            commitTextInput()
        }
        guard !isPerformingOCR else { return }
        isPerformingOCR = true
        ocrTranslationError = nil

        Task {
            await executeOCR()
        }
    }

    func executeOCR() async {
        defer { isPerformingOCR = false }

        do {
            let result = try await ocrService.recognize(
                image,
                languages: [.english, .chineseSimplified]
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
