import Foundation
import CoreGraphics

/// A drawing element placed on a screenshot.
/// Supports rectangle, freehand, arrow, text, ellipse, line, mosaic, highlight, and numberLabel annotation types.
enum Annotation: Identifiable, Equatable, Sendable {
    case rectangle(RectangleAnnotation)
    case freehand(FreehandAnnotation)
    case arrow(ArrowAnnotation)
    case text(TextAnnotation)
    case ellipse(EllipseAnnotation)
    case line(LineAnnotation)
    case mosaic(MosaicAnnotation)
    case highlight(HighlightAnnotation)
    case numberLabel(NumberLabelAnnotation)
    case privacyRedaction(PrivacyRedactionAnnotation)
    case calloutBubble(CalloutBubbleAnnotation)

    /// Unique identifier for this annotation
    var id: UUID {
        switch self {
        case .rectangle(let annotation):
            return annotation.id
        case .freehand(let annotation):
            return annotation.id
        case .arrow(let annotation):
            return annotation.id
        case .text(let annotation):
            return annotation.id
        case .ellipse(let annotation):
            return annotation.id
        case .line(let annotation):
            return annotation.id
        case .mosaic(let annotation):
            return annotation.id
        case .highlight(let annotation):
            return annotation.id
        case .numberLabel(let annotation):
            return annotation.id
        case .privacyRedaction(let annotation):
            return annotation.id
        case .calloutBubble(let annotation):
            return annotation.id
        }
    }

    /// The bounding rect of this annotation
    var bounds: CGRect {
        switch self {
        case .rectangle(let annotation):
            return annotation.rect
        case .freehand(let annotation):
            return annotation.bounds
        case .arrow(let annotation):
            return annotation.bounds
        case .text(let annotation):
            return annotation.bounds
        case .ellipse(let annotation):
            return annotation.rect
        case .line(let annotation):
            return annotation.bounds
        case .mosaic(let annotation):
            return annotation.rect
        case .highlight(let annotation):
            return annotation.rect
        case .numberLabel(let annotation):
            return annotation.bounds
        case .privacyRedaction(let annotation):
            return annotation.rect
        case .calloutBubble(let annotation):
            return annotation.bounds
        }
    }
}

// MARK: - Rectangle Annotation

/// A rectangle annotation with position, size, and stroke style.
struct RectangleAnnotation: Identifiable, Equatable, Sendable {
    /// Unique identifier
    let id: UUID

    /// Position and size in image coordinates
    var rect: CGRect

    /// Stroke color and line width
    var style: StrokeStyle

    /// Whether the rectangle is filled (solid) or hollow (outline only)
    /// When filled, the rectangle uses the stroke color as fill to hide underlying content
    var isFilled: Bool

    init(id: UUID = UUID(), rect: CGRect, style: StrokeStyle = .default, isFilled: Bool = false) {
        self.id = id
        self.rect = rect
        self.style = style
        self.isFilled = isFilled
    }
}

// MARK: - Freehand Annotation

/// A freehand path annotation with points and stroke style.
struct FreehandAnnotation: Identifiable, Equatable, Sendable {
    /// Unique identifier
    let id: UUID

    /// Path vertices in image coordinates (minimum 2 points)
    var points: [CGPoint]

    /// Stroke color and line width
    var style: StrokeStyle

    init(id: UUID = UUID(), points: [CGPoint], style: StrokeStyle = .default) {
        self.id = id
        self.points = points
        self.style = style
    }

    /// Whether this annotation has enough points to be valid
    var isValid: Bool {
        points.count >= 2
    }

    /// The bounding rectangle of all points
    var bounds: CGRect {
        guard !points.isEmpty else { return .zero }

        var minX = CGFloat.infinity
        var minY = CGFloat.infinity
        var maxX = -CGFloat.infinity
        var maxY = -CGFloat.infinity

        for point in points {
            minX = min(minX, point.x)
            minY = min(minY, point.y)
            maxX = max(maxX, point.x)
            maxY = max(maxY, point.y)
        }

        return CGRect(x: minX, y: minY, width: maxX - minX, height: maxY - minY)
    }
}

// MARK: - Arrow Annotation

/// An arrow annotation with start point, end point (arrowhead), and stroke style.
struct ArrowAnnotation: Identifiable, Equatable, Sendable {
    /// Unique identifier
    let id: UUID

    /// Start point of the arrow (tail) in image coordinates
    var startPoint: CGPoint

    /// End point of the arrow (head) in image coordinates
    var endPoint: CGPoint

    /// Stroke color and line width
    var style: StrokeStyle

    init(id: UUID = UUID(), startPoint: CGPoint, endPoint: CGPoint, style: StrokeStyle = .default) {
        self.id = id
        self.startPoint = startPoint
        self.endPoint = endPoint
        self.style = style
    }

    /// Whether this annotation has meaningful length
    var isValid: Bool {
        let dx = endPoint.x - startPoint.x
        let dy = endPoint.y - startPoint.y
        let length = sqrt(dx * dx + dy * dy)
        return length >= 5
    }

    /// The bounding rectangle of the arrow
    var bounds: CGRect {
        let minX = min(startPoint.x, endPoint.x)
        let minY = min(startPoint.y, endPoint.y)
        let maxX = max(startPoint.x, endPoint.x)
        let maxY = max(startPoint.y, endPoint.y)

        // Add padding for the arrowhead
        let padding: CGFloat = style.lineWidth * 3
        return CGRect(
            x: minX - padding,
            y: minY - padding,
            width: maxX - minX + padding * 2,
            height: maxY - minY + padding * 2
        )
    }
}

// MARK: - Text Annotation

/// A text annotation with position, content, and text style.
struct TextAnnotation: Identifiable, Equatable, Sendable {
    /// Unique identifier
    let id: UUID

    /// Anchor point in image coordinates
    var position: CGPoint

    /// User-entered text content
    var content: String

    /// Font, size, and color
    var style: TextStyle

    init(id: UUID = UUID(), position: CGPoint, content: String, style: TextStyle = .default) {
        self.id = id
        self.position = position
        self.content = content
        self.style = style
    }

    /// Whether this annotation has non-empty content
    var isValid: Bool {
        !content.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    /// Estimated bounds of the text annotation based on font size and content
    var bounds: CGRect {
        // Estimate text size based on font size and character count
        // Average character width is approximately 0.6 × font size for most fonts
        let averageCharWidth = style.fontSize * 0.6
        let estimatedWidth = max(CGFloat(content.count) * averageCharWidth, style.fontSize * 2)
        // Height is approximately 1.2 × font size (line height)
        let estimatedHeight = style.fontSize * 1.3

        return CGRect(
            origin: position,
            size: CGSize(width: estimatedWidth, height: estimatedHeight)
        )
    }
}

// MARK: - Ellipse Annotation

/// An ellipse annotation with position, size, and stroke style.
struct EllipseAnnotation: Identifiable, Equatable, Sendable {
    /// Unique identifier
    let id: UUID

    /// Position and size in image coordinates
    var rect: CGRect

    /// Stroke color and line width
    var style: StrokeStyle

    /// Whether the ellipse is filled (solid) or hollow (outline only)
    var isFilled: Bool

    init(id: UUID = UUID(), rect: CGRect, style: StrokeStyle = .default, isFilled: Bool = false) {
        self.id = id
        self.rect = rect
        self.style = style
        self.isFilled = isFilled
    }

    /// Whether this annotation has meaningful size
    var isValid: Bool {
        rect.width >= 5 && rect.height >= 5
    }
}

// MARK: - Line Annotation

/// A line annotation with start point, end point, and stroke style.
struct LineAnnotation: Identifiable, Equatable, Sendable {
    /// Unique identifier
    let id: UUID

    /// Start point in image coordinates
    var startPoint: CGPoint

    /// End point in image coordinates
    var endPoint: CGPoint

    /// Stroke color and line width
    var style: StrokeStyle

    init(id: UUID = UUID(), startPoint: CGPoint, endPoint: CGPoint, style: StrokeStyle = .default) {
        self.id = id
        self.startPoint = startPoint
        self.endPoint = endPoint
        self.style = style
    }

    /// Whether this annotation has meaningful length
    var isValid: Bool {
        let dx = endPoint.x - startPoint.x
        let dy = endPoint.y - startPoint.y
        let length = sqrt(dx * dx + dy * dy)
        return length >= 5
    }

    /// The bounding rectangle of the line
    var bounds: CGRect {
        let minX = min(startPoint.x, endPoint.x)
        let minY = min(startPoint.y, endPoint.y)
        let maxX = max(startPoint.x, endPoint.x)
        let maxY = max(startPoint.y, endPoint.y)

        let padding = style.lineWidth / 2
        return CGRect(
            x: minX - padding,
            y: minY - padding,
            width: maxX - minX + padding * 2,
            height: maxY - minY + padding * 2
        )
    }
}

// MARK: - Mosaic Annotation

/// A mosaic annotation that pixelates a region to hide sensitive content.
struct MosaicAnnotation: Identifiable, Equatable, Sendable {
    /// Unique identifier
    let id: UUID

    /// Position and size in image coordinates
    var rect: CGRect

    /// Block size for pixelation (8-32)
    var blockSize: Int

    init(id: UUID = UUID(), rect: CGRect, blockSize: Int = 10) {
        self.id = id
        self.rect = rect
        self.blockSize = max(4, min(32, blockSize))
    }

    /// Whether this annotation has meaningful size
    var isValid: Bool {
        rect.width >= 10 && rect.height >= 10
    }
}

// MARK: - Highlight Annotation

/// A highlight annotation with semi-transparent background.
struct HighlightAnnotation: Identifiable, Equatable, Sendable {
    /// Unique identifier
    let id: UUID

    /// Position and size in image coordinates
    var rect: CGRect

    /// Highlight color
    var color: CodableColor

    /// Opacity (0.0 - 1.0)
    var opacity: Double

    init(id: UUID = UUID(), rect: CGRect, color: CodableColor = CodableColor(.yellow), opacity: Double = 0.4) {
        self.id = id
        self.rect = rect
        self.color = color
        self.opacity = max(0.1, min(0.8, opacity))
    }

    /// Whether this annotation has meaningful size
    var isValid: Bool {
        rect.width >= 5 && rect.height >= 5
    }
}

// MARK: - Number Label Annotation

/// A numbered label annotation (①②③...) for marking sequential items.
struct NumberLabelAnnotation: Identifiable, Equatable, Sendable {
    /// Unique identifier
    let id: UUID

    /// Center position in image coordinates
    var position: CGPoint

    /// The number to display
    var number: Int

    /// Circle size (diameter)
    var size: CGFloat

    /// Text color
    var color: CodableColor

    init(id: UUID = UUID(), position: CGPoint, number: Int, size: CGFloat = 24, color: CodableColor = CodableColor(.red)) {
        self.id = id
        self.position = position
        self.number = max(1, number)
        self.size = max(16, min(64, size))
        self.color = color
    }

    /// The bounding rectangle centered at position
    var bounds: CGRect {
        CGRect(
            x: position.x - size / 2,
            y: position.y - size / 2,
            width: size,
            height: size
        )
    }
}

// MARK: - CGPoint Sendable Conformance

extension CGPoint: @retroactive @unchecked Sendable {}

// MARK: - Annotation Type

extension Annotation {
    /// The type of this annotation for display purposes
    var typeName: String {
        switch self {
        case .rectangle:
            return NSLocalizedString("tool.rectangle", comment: "")
        case .freehand:
            return NSLocalizedString("tool.freehand", comment: "")
        case .arrow:
            return NSLocalizedString("tool.arrow", comment: "")
        case .text:
            return NSLocalizedString("tool.text", comment: "")
        case .ellipse:
            return NSLocalizedString("tool.ellipse", comment: "")
        case .line:
            return NSLocalizedString("tool.line", comment: "")
        case .mosaic:
            return NSLocalizedString("tool.mosaic", comment: "")
        case .highlight:
            return NSLocalizedString("tool.highlight", comment: "")
        case .numberLabel:
            return NSLocalizedString("tool.numberLabel", comment: "")
        case .privacyRedaction:
            return NSLocalizedString("tool.privacyRedaction", comment: "Privacy Redaction")
        case .calloutBubble:
            return NSLocalizedString("tool.calloutBubble", comment: "Callout Bubble")
        }
    }
}

// MARK: - Privacy Redaction Annotation

/// A solid opaque rectangle used to permanently redact confidential regions before OCR or export.
struct PrivacyRedactionAnnotation: Identifiable, Equatable, Sendable {
    let id: UUID
    var rect: CGRect
    var fillColor: CodableColor

    init(id: UUID = UUID(), rect: CGRect, fillColor: CodableColor = CodableColor(.black)) {
        self.id = id
        self.rect = rect
        self.fillColor = fillColor
    }

    var isValid: Bool {
        rect.width >= 5 && rect.height >= 5
    }
}

// MARK: - Callout Bubble Annotation

/// A speech/callout bubble with text and an adjustable pointer arrow.
struct CalloutBubbleAnnotation: Identifiable, Equatable, Sendable {
    let id: UUID
    var rect: CGRect
    var targetPoint: CGPoint
    var text: String
    var style: TextStyle
    var bubbleColor: CodableColor

    init(
        id: UUID = UUID(),
        rect: CGRect,
        targetPoint: CGPoint,
        text: String = "",
        style: TextStyle = .default,
        bubbleColor: CodableColor = CodableColor(.white)
    ) {
        self.id = id
        self.rect = rect
        self.targetPoint = targetPoint
        self.text = text
        self.style = style
        self.bubbleColor = bubbleColor
    }

    var bounds: CGRect {
        let minX = min(rect.minX, targetPoint.x)
        let minY = min(rect.minY, targetPoint.y)
        let maxX = max(rect.maxX, targetPoint.x)
        let maxY = max(rect.maxY, targetPoint.y)
        return CGRect(x: minX - 4, y: minY - 4, width: maxX - minX + 8, height: maxY - minY + 8)
    }

    var isValid: Bool {
        rect.width >= 20 && rect.height >= 16
    }
}

// MARK: - Precise Geometric Hit Testing

extension Annotation {
    /// Tests if a point hits the annotation with exact geometric distance tolerance.
    func hitTest(point: CGPoint, tolerance: CGFloat = 8.0) -> Bool {
        switch self {
        case .rectangle(let r):
            if r.isFilled {
                return r.rect.insetBy(dx: -tolerance, dy: -tolerance).contains(point)
            }
            return isPointNearRectBorder(point, rect: r.rect, strokeWidth: r.style.lineWidth, tolerance: tolerance)

        case .ellipse(let e):
            if e.isFilled {
                return isPointInsideEllipse(point, rect: e.rect)
            }
            return isPointNearEllipseBorder(point, rect: e.rect, strokeWidth: e.style.lineWidth, tolerance: tolerance)

        case .line(let l):
            let d = distanceToSegment(p: point, a: l.startPoint, b: l.endPoint)
            return d <= (l.style.lineWidth / 2 + tolerance)

        case .arrow(let a):
            let d = distanceToSegment(p: point, a: a.startPoint, b: a.endPoint)
            if d <= (a.style.lineWidth / 2 + tolerance) { return true }
            return a.bounds.insetBy(dx: -tolerance, dy: -tolerance).contains(point)

        case .freehand(let f):
            guard f.points.count >= 2 else { return false }
            let maxDist = f.style.lineWidth / 2 + tolerance
            for i in 0..<(f.points.count - 1) {
                if distanceToSegment(p: point, a: f.points[i], b: f.points[i + 1]) <= maxDist {
                    return true
                }
            }
            return false

        case .text(let t):
            return t.bounds.insetBy(dx: -tolerance, dy: -tolerance).contains(point)

        case .mosaic(let m):
            return m.rect.insetBy(dx: -tolerance, dy: -tolerance).contains(point)

        case .highlight(let h):
            return h.rect.insetBy(dx: -tolerance, dy: -tolerance).contains(point)

        case .numberLabel(let n):
            return n.bounds.insetBy(dx: -tolerance, dy: -tolerance).contains(point)

        case .privacyRedaction(let pr):
            return pr.rect.insetBy(dx: -tolerance, dy: -tolerance).contains(point)

        case .calloutBubble(let cb):
            return cb.bounds.insetBy(dx: -tolerance, dy: -tolerance).contains(point)
        }
    }

    private func distanceToSegment(p: CGPoint, a: CGPoint, b: CGPoint) -> CGFloat {
        let abx = b.x - a.x
        let aby = b.y - a.y
        let apx = p.x - a.x
        let apy = p.y - a.y
        let segLenSq = abx * abx + aby * aby
        if segLenSq == 0 {
            return hypot(apx, apy)
        }
        let t = max(0, min(1, (apx * abx + apy * aby) / segLenSq))
        let projX = a.x + t * abx
        let projY = a.y + t * aby
        return hypot(p.x - projX, p.y - projY)
    }

    private func isPointNearRectBorder(_ p: CGPoint, rect: CGRect, strokeWidth: CGFloat, tolerance: CGFloat) -> Bool {
        let effectiveTol = strokeWidth / 2 + tolerance
        let outer = rect.insetBy(dx: -effectiveTol, dy: -effectiveTol)
        let inner = rect.insetBy(dx: effectiveTol, dy: effectiveTol)
        return outer.contains(p) && !inner.contains(p)
    }

    private func isPointInsideEllipse(_ p: CGPoint, rect: CGRect) -> Bool {
        let rx = rect.width / 2
        let ry = rect.height / 2
        guard rx > 0 && ry > 0 else { return false }
        let cx = rect.midX
        let cy = rect.midY
        let dx = (p.x - cx) / rx
        let dy = (p.y - cy) / ry
        return (dx * dx + dy * dy) <= 1.0
    }

    private func isPointNearEllipseBorder(_ p: CGPoint, rect: CGRect, strokeWidth: CGFloat, tolerance: CGFloat) -> Bool {
        let effTol = strokeWidth / 2 + tolerance
        let outerRect = rect.insetBy(dx: -effTol, dy: -effTol)
        let innerRect = rect.insetBy(dx: effTol, dy: effTol)
        return isPointInsideEllipse(p, rect: outerRect) && !isPointInsideEllipse(p, rect: innerRect)
    }
}
