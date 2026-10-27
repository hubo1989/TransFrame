import Foundation
import SwiftUI

extension PreviewViewModel {
    func toggleCropMode() {
        isCropMode.toggle()
    }

    func beginCropSelection(at point: CGPoint) {
        guard isCropMode else { return }
        cropStartPoint = point
        cropRect = CGRect(origin: point, size: .zero)
        isCropSelecting = true
    }

    func continueCropSelection(to point: CGPoint) {
        guard isCropMode, let start = cropStartPoint else { return }

        let minX = min(start.x, point.x)
        let minY = min(start.y, point.y)
        let width = abs(point.x - start.x)
        let height = abs(point.y - start.y)

        cropRect = CGRect(x: minX, y: minY, width: width, height: height)
    }

    func endCropSelection(at point: CGPoint) {
        guard isCropMode else { return }
        continueCropSelection(to: point)
        isCropSelecting = false

        if let rect = cropRect, rect.width < 10 || rect.height < 10 {
            cropRect = nil
        }
    }

    func applyCrop() {
        guard let rect = cropRect else { return }

        let imageWidth = CGFloat(screenshot.image.width)
        let imageHeight = CGFloat(screenshot.image.height)

        let clampedRect = CGRect(
            x: max(0, rect.origin.x),
            y: max(0, rect.origin.y),
            width: min(rect.width, imageWidth - rect.origin.x),
            height: min(rect.height, imageHeight - rect.origin.y)
        )

        guard clampedRect.width >= 10, clampedRect.height >= 10 else {
            errorMessage = "Crop area is too small"
            cropRect = nil
            isCropMode = false
            return
        }

        guard let croppedImage = screenshot.image.cropping(to: clampedRect) else {
            errorMessage = "Failed to crop image"
            return
        }

        pushUndoState()

        // Shift annotations relative to the new crop origin
        let originShift = clampedRect.origin
        let updatedAnnotations = screenshot.annotations.map {
            $0.offsetBy(dx: -originShift.x, dy: -originShift.y)
        }

        // Invalidate OCR cache and translation results for the previous crop area
        ocrTask?.cancel()
        translationTask?.cancel()
        analysisGeneration &+= 1
        isPerformingOCR = false
        isTranslating = false
        ocrResult = nil
        bilingualSegments = []

        screenshot = Screenshot(
            id: screenshot.id,
            image: croppedImage,
            captureDate: screenshot.captureDate,
            sourceDisplay: screenshot.sourceDisplay,
            annotations: updatedAnnotations,
            format: screenshot.format
        )

        // Sync with central CaptureDocumentStore if present
        if var activeDoc = CaptureDocumentStore.shared.document(for: screenshot.id) {
            activeDoc.cropTransform = clampedRect
            activeDoc.annotations = updatedAnnotations
            activeDoc.editRevision += 1
            activeDoc.analysisInputRevision += 1
            activeDoc.cachedAnalysisResult = nil
            CaptureDocumentStore.shared.update(activeDoc)
        }

        redoStack.removeAll()
        isCropMode = false
        cropRect = nil
        imageSizeChangeCounter += 1
    }

    func cancelCrop() {
        cropRect = nil
        isCropMode = false
        isCropSelecting = false
        cropStartPoint = nil
    }
}
