import Foundation
import CoreGraphics
import AppKit

/// Unified document model representing a capture lifecycle session.
/// Stable across cropping, undo/redo, pinning, annotation, OCR, and translation.
struct CaptureDocument: Identifiable, Sendable {
    /// Stable document identifier across all operations
    let id: UUID

    /// Immutable base image captured at session confirmation
    let baseImage: CGImage

    /// Screen coordinates of the original capture in Quartz space (used for 1:1 in-place pinning)
    let sourceScreenRect: QuartzGlobalRect

    /// Optional DisplayInfo describing the capture origin
    let sourceDisplay: DisplayInfo?

    /// Non-destructive crop viewport in baseImage pixel coordinates (nil means full baseImage)
    var cropTransform: CGRect?

    /// Annotations placed on the document (coordinates relative to baseImage top-left pixel)
    var annotations: [Annotation]

    /// Edit revision counter, incremented on any visual annotation or crop modification
    var editRevision: Int

    /// Analysis revision counter, incremented only when pixels, crop, or privacy redactions change
    var analysisInputRevision: Int

    /// Revision of the document when it was last saved to disk
    var lastSavedRevision: Int

    /// Associated OCR result for this document (invalidated when analysisInputRevision increases)
    var cachedAnalysisResult: ScreenAnalysisResult?

    /// Creation timestamp
    let createdAt: Date

    init(
        id: UUID = UUID(),
        baseImage: CGImage,
        sourceScreenRect: QuartzGlobalRect,
        sourceDisplay: DisplayInfo? = nil,
        cropTransform: CGRect? = nil,
        annotations: [Annotation] = [],
        editRevision: Int = 1,
        analysisInputRevision: Int = 1,
        lastSavedRevision: Int = 0,
        cachedAnalysisResult: ScreenAnalysisResult? = nil,
        createdAt: Date = Date()
    ) {
        self.id = id
        self.baseImage = baseImage
        self.sourceScreenRect = sourceScreenRect
        self.sourceDisplay = sourceDisplay
        self.cropTransform = cropTransform
        self.annotations = annotations
        self.editRevision = editRevision
        self.analysisInputRevision = analysisInputRevision
        self.lastSavedRevision = lastSavedRevision
        self.cachedAnalysisResult = cachedAnalysisResult
        self.createdAt = createdAt
    }

    // MARK: - Computed Properties

    /// Effective crop rect in baseImage coordinates (defaults to full baseImage bounds if nil)
    var effectiveCropRect: CGRect {
        if let crop = cropTransform {
            return crop
        }
        return CGRect(x: 0, y: 0, width: baseImage.width, height: baseImage.height)
    }

    /// Effective visible image size taking cropTransform into account
    var visiblePixelSize: CGSize {
        if let crop = cropTransform {
            return crop.size
        }
        return CGSize(width: baseImage.width, height: baseImage.height)
    }

    /// Whether this document has unsaved edits
    var hasUnsavedEdits: Bool {
        editRevision > lastSavedRevision
    }

    /// Converts this CaptureDocument to a legacy Screenshot model for backward compatibility
    func toLegacyScreenshot(format: ExportFormat = .png) -> Screenshot {
        let display = sourceDisplay ?? DisplayInfo(
            id: CGMainDisplayID(),
            name: "Display",
            frame: sourceScreenRect.rawValue,
            scaleFactor: 2.0,
            isPrimary: true
        )

        return Screenshot(
            id: id,
            image: baseImage,
            captureDate: createdAt,
            sourceDisplay: display,
            annotations: annotations,
            format: format
        )
    }
}
