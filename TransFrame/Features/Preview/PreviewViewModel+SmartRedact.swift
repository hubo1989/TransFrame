import Foundation
import SwiftUI
import os

// MARK: - Smart Privacy Redaction (智能马赛克)

extension PreviewViewModel {
    private static let smartRedactLogger = Logger.ui

    /// Runs VLM privacy detection over the current image and overlays mosaic
    /// annotations on every detected sensitive region. One undo step covers the batch.
    func performSmartRedaction() {
        guard !isDetectingPrivacy else { return }
        if isWaitingForTextInputInternal {
            commitTextInput()
        }

        isDetectingPrivacy = true
        smartRedactionMessage = String(localized: "smartMosaic.detecting")

        Task { @MainActor in
            defer { isDetectingPrivacy = false }

            do {
                let rects = try await SmartPrivacyRedactionService.shared.detectPrivacyRegions(in: image)

                guard !rects.isEmpty else {
                    smartRedactionMessage = String(localized: "smartMosaic.noneFound")
                    dismissSmartRedactionMessage()
                    return
                }

                let blockSize = Int(settings.mosaicBlockSize)
                let mosaics = rects.map { rect in
                    Annotation.mosaic(MosaicAnnotation(rect: rect, blockSize: blockSize))
                }

                pushUndoState()
                for mosaic in mosaics {
                    screenshot = screenshot.adding(mosaic)
                }
                redoStack.removeAll()
                drawingUpdateCounter += 1

                smartRedactionMessage = String(format: String(localized: "smartMosaic.redacted"), rects.count)
                dismissSmartRedactionMessage()
            } catch {
                Self.smartRedactLogger.error("Smart redaction failed: \(error.localizedDescription)")
                smartRedactionMessage = nil
                errorMessage = String(localized: "smartMosaic.error.failed")
                clearError()
            }
        }
    }

    /// Auto-hides the transient smart-redaction toast.
    func dismissSmartRedactionMessage() {
        Task {
            try? await Task.sleep(for: .seconds(3))
            smartRedactionMessage = nil
        }
    }
}
