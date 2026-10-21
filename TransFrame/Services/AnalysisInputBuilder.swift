import Foundation
import CoreGraphics
import AppKit

/// Builds a sanitized, cropped, and desensitized image input for OCR and VLM analysis.
/// Guarantees that solid privacy redactions are permanently burned into pixels before transmission.
final class AnalysisInputBuilder: @unchecked Sendable {
    static let shared = AnalysisInputBuilder()

    private init() {}

    /// Builds a sanitized analysis image from a CaptureDocument.
    /// - Parameters:
    ///   - document: The source document.
    ///   - roiRect: Optional Region of Interest relative to the document's effective visible content.
    /// - Returns: A sanitized CGImage with all privacy redactions burned in and non-destructive crops applied.
    func buildInputImage(
        for document: CaptureDocument,
        roiRect: CGRect? = nil
    ) -> CGImage? {
        let base = document.baseImage
        let originalWidth = CGFloat(base.width)
        let originalHeight = CGFloat(base.height)

        var cropRect = document.effectiveCropRect

        // Apply local ROI if specified
        if let roi = roiRect {
            // ROI is relative to the cropped document content
            let adjustedROI = CGRect(
                x: cropRect.origin.x + roi.origin.x,
                y: cropRect.origin.y + roi.origin.y,
                width: roi.width,
                height: roi.height
            )
            cropRect = cropRect.intersection(adjustedROI)
        }

        guard cropRect.width >= 1, cropRect.height >= 1 else {
            return nil
        }

        let targetWidth = max(1, Int(cropRect.width.rounded()))
        let targetHeight = max(1, Int(cropRect.height.rounded()))

        guard let colorSpace = base.colorSpace ?? CGColorSpace(name: CGColorSpace.sRGB) else {
            return nil
        }

        guard let context = CGContext(
            data: nil,
            width: targetWidth,
            height: targetHeight,
            bitsPerComponent: 8,
            bytesPerRow: 0,
            space: colorSpace,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ) else {
            return nil
        }

        // 1. Draw cropped base image
        context.saveGState()
        let drawX = -cropRect.origin.x
        let drawY = -(originalHeight - (cropRect.origin.y + cropRect.height))
        context.draw(base, in: CGRect(x: drawX, y: drawY, width: originalWidth, height: originalHeight))
        context.restoreGState()

        // 2. CRITICAL PRIVACY BURN-IN:
        // Permanently destroy all redacted pixels so they cannot be retrieved by cloud or local models.
        let shiftTransform = CGAffineTransform(translationX: -cropRect.origin.x, y: -cropRect.origin.y)

        for annotation in document.annotations {
            if case .privacyRedaction(let redaction) = annotation {
                let shiftedRect = redaction.rect.applying(shiftTransform)
                let cgRect = CGRect(
                    x: shiftedRect.origin.x,
                    y: CGFloat(targetHeight) - shiftedRect.origin.y - shiftedRect.height,
                    width: shiftedRect.width,
                    height: shiftedRect.height
                )

                context.saveGState()
                context.setFillColor(redaction.fillColor.cgColor)
                context.fill(cgRect)
                context.restoreGState()
            }
        }

        return context.makeImage()
    }
}
