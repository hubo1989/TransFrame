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
        var updatedAnnotations: [Annotation] = []
        for anno in screenshot.annotations {
            var shifted = anno
            switch shifted {
            case .rectangle(var r):
                r.rect.origin.x -= originShift.x
                r.rect.origin.y -= originShift.y
                updatedAnnotations.append(.rectangle(r))
            case .ellipse(var e):
                e.rect.origin.x -= originShift.x
                e.rect.origin.y -= originShift.y
                updatedAnnotations.append(.ellipse(e))
            case .line(var l):
                l.startPoint.x -= originShift.x
                l.startPoint.y -= originShift.y
                l.endPoint.x -= originShift.x
                l.endPoint.y -= originShift.y
                updatedAnnotations.append(.line(l))
            case .freehand(var f):
                f.points = f.points.map { CGPoint(x: $0.x - originShift.x, y: $0.y - originShift.y) }
                updatedAnnotations.append(.freehand(f))
            case .arrow(var a):
                a.startPoint.x -= originShift.x
                a.startPoint.y -= originShift.y
                a.endPoint.x -= originShift.x
                a.endPoint.y -= originShift.y
                updatedAnnotations.append(.arrow(a))
            case .highlight(var h):
                h.rect.origin.x -= originShift.x
                h.rect.origin.y -= originShift.y
                updatedAnnotations.append(.highlight(h))
            case .mosaic(var m):
                m.rect.origin.x -= originShift.x
                m.rect.origin.y -= originShift.y
                updatedAnnotations.append(.mosaic(m))
            case .text(var t):
                t.position.x -= originShift.x
                t.position.y -= originShift.y
                updatedAnnotations.append(.text(t))
            case .numberLabel(var n):
                n.position.x -= originShift.x
                n.position.y -= originShift.y
                updatedAnnotations.append(.numberLabel(n))
            case .privacyRedaction(var pr):
                pr.rect.origin.x -= originShift.x
                pr.rect.origin.y -= originShift.y
                updatedAnnotations.append(.privacyRedaction(pr))
            case .calloutBubble(var cb):
                cb.rect.origin.x -= originShift.x
                cb.rect.origin.y -= originShift.y
                cb.targetPoint.x -= originShift.x
                cb.targetPoint.y -= originShift.y
                updatedAnnotations.append(.calloutBubble(cb))
            }
        }

        // Invalidate OCR cache for the previous crop area
        ocrResult = nil

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
