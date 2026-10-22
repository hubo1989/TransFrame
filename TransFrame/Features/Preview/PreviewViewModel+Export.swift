import Foundation
import SwiftUI
import AppKit

extension PreviewViewModel {
    @discardableResult
    func copyToClipboard(dismissOnSuccess: Bool = false) -> Bool {
        if isWaitingForTextInputInternal {
            commitTextInput()
        }
        guard !isCopying else { return false }
        isCopying = true

        let image = image
        let annotations = annotations
        let operationID = UUID()

        Task { @MainActor in
            do {
                let finalImage = try await Self.prepareImageForExport(
                    image: image,
                    annotations: annotations,
                    operationID: operationID
                )
                try clipboardService.copy(finalImage)
                isCopying = false
                if dismissOnSuccess {
                    dismiss()
                }
            } catch is CancellationError {
                isCopying = false
            } catch {
                errorMessage = NSLocalizedString("error.clipboard.write.failed", comment: "Failed to copy to clipboard")
                clearError()
                isCopying = false
            }
        }

        return true
    }

    func saveScreenshot() {
        if isWaitingForTextInputInternal {
            commitTextInput()
        }
        guard !isSaving else { return }
        isSaving = true

        Task {
            await performSave()
        }
    }

    func performSave() async {
        defer { isSaving = false }

        let directory = settings.saveLocation
        let format = settings.defaultFormat
        let quality: Double
        switch format {
        case .jpeg:
            quality = settings.jpegQuality
        case .heic:
            quality = settings.heicQuality
        case .png:
            quality = 1.0
        }

        let fileURL = imageExporter.generateFileURL(in: directory, format: format)

        do {
            let image = image
            let annotations = annotations
            let operationID = UUID()
            try await PerformanceRecorder.shared.measure(stage: .export, operationID: operationID) {
                try await Task.detached(priority: .userInitiated) {
                    try Task.checkCancellation()
                    try ImageExporter.shared.save(
                        image,
                        annotations: annotations,
                        to: fileURL,
                        format: format,
                        quality: quality
                    )
                }.value
            }

            screenshot = screenshot.saved(to: fileURL)
            onSave?(fileURL)
            hide()
        } catch let error as TransFrameError {
            handleSaveError(error)
        } catch {
            errorMessage = NSLocalizedString("error.save.unknown", comment: "An unexpected error occurred while saving")
            clearError()
        }
    }

    func handleSaveError(_ error: TransFrameError) {
        switch error {
        case .invalidSaveLocation(let url):
            errorMessage = String(
                format: NSLocalizedString("error.save.location.invalid.detail", comment: ""),
                url.path
            )
        case .diskFull:
            errorMessage = NSLocalizedString("error.disk.full", comment: "Not enough disk space")
        case .exportEncodingFailed(let format):
            errorMessage = String(
                format: NSLocalizedString("error.export.encoding.failed.detail", comment: ""),
                format.displayName
            )
        default:
            errorMessage = error.localizedDescription
        }
        clearError()
    }

    func dismissSuccessMessage() {
        saveSuccessMessage = nil
    }

    func dismissCopySuccessMessage() {
        copySuccessMessage = nil
    }

    private static func prepareImageForExport(
        image: CGImage,
        annotations: [Annotation],
        operationID: UUID
    ) async throws -> CGImage {
        try await PerformanceRecorder.shared.measure(stage: .export, operationID: operationID) {
            try await Task.detached(priority: .userInitiated) {
                try Task.checkCancellation()
                guard !annotations.isEmpty else {
                    return image
                }
                return try ImageExporter.shared.compositeAnnotations(annotations, onto: image)
            }.value
        }
    }
}
