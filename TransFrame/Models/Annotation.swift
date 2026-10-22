import Foundation
import CoreGraphics
import AppKit

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
    var isFilled: Bool

    /// Background fill color used when filled; nil falls back to the stroke color (legacy documents)
    var fillColor: CodableColor?

    init(
        id: UUID = UUID(),
        rect: CGRect,
        style: StrokeStyle = .default,
        isFilled: Bool = false,
        fillColor: CodableColor? = nil
    ) {
        self.id = id
        self.rect = rect
        self.style = style
        self.isFilled = isFilled
        self.fillColor = fillColor
    }

    /// The color actually painted inside a filled rectangle
    var effectiveFillColor: CodableColor {
        fillColor ?? style.color
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
/// Supports straight arrow or curved arrow via quadratic bezier control point.
struct ArrowAnnotation: Identifiable, Equatable, Sendable {
    /// Unique identifier
    let id: UUID

    /// Start point of the arrow (tail) in image coordinates
    var startPoint: CGPoint

    /// End point of the arrow (head) in image coordinates
    var endPoint: CGPoint

    /// Optional curvature control point for curved arrows
    var controlPoint: CGPoint?

    /// Stroke color and line width
    var style: StrokeStyle

    init(id: UUID = UUID(), startPoint: CGPoint, endPoint: CGPoint, style: StrokeStyle = .default, controlPoint: CGPoint? = nil) {
        self.id = id
        self.startPoint = startPoint
        self.endPoint = endPoint
        self.style = style
        self.controlPoint = controlPoint
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
        var minX = min(startPoint.x, endPoint.x)
        var minY = min(startPoint.y, endPoint.y)
        var maxX = max(startPoint.x, endPoint.x)
        var maxY = max(startPoint.y, endPoint.y)

        if let cp = controlPoint {
            minX = min(minX, cp.x)
            minY = min(minY, cp.y)
            maxX = max(maxX, cp.x)
            maxY = max(maxY, cp.y)
        }

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

    /// Anchor point in image coordinates (top-leading corner of the text block)
    var position: CGPoint

    /// User-entered text content
    var content: String

    /// Font, size, and color
    var style: TextStyle

    /// Maximum line width for automatic word wrapping; nil = single line without wrapping
    var wrapWidth: CGFloat?

    init(
        id: UUID = UUID(),
        position: CGPoint,
        content: String,
        style: TextStyle = .default,
        wrapWidth: CGFloat? = nil
    ) {
        self.id = id
        self.position = position
        self.content = content
        self.style = style
        self.wrapWidth = wrapWidth
    }

    /// Whether this annotation has non-empty content
    var isValid: Bool {
        !content.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    /// Bounds of the text annotation measured with the actual font so multi-line
    /// content is never truncated.
    var bounds: CGRect {
        guard !content.isEmpty else {
            return CGRect(origin: position, size: CGSize(width: style.fontSize * 2, height: style.fontSize * 1.3))
        }

        let font = NSFont(name: style.fontName, size: style.fontSize) ?? NSFont.systemFont(ofSize: style.fontSize)
        let paragraph = NSMutableParagraphStyle()
        paragraph.lineBreakMode = .byWordWrapping
        let attributes: [NSAttributedString.Key: Any] = [
            .font: font,
            .paragraphStyle: paragraph
        ]

        let constraintWidth = wrapWidth ?? .greatestFiniteMagnitude
        let measured = (content as NSString).boundingRect(
            with: CGSize(width: constraintWidth, height: .greatestFiniteMagnitude),
            options: [.usesLineFragmentOrigin, .usesFontLeading],
            attributes: attributes,
            context: nil
        )

        // Add a small horizontal tolerance so the trailing glyph is fully covered
        return CGRect(
            origin: position,
            size: CGSize(width: ceil(measured.width) + 1, height: ceil(measured.height))
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

    /// Background fill color used when filled; nil falls back to the stroke color (legacy documents)
    var fillColor: CodableColor?

    init(
        id: UUID = UUID(),
        rect: CGRect,
        style: StrokeStyle = .default,
        isFilled: Bool = false,
        fillColor: CodableColor? = nil
    ) {
        self.id = id
        self.rect = rect
        self.style = style
        self.isFilled = isFilled
        self.fillColor = fillColor
    }

    /// Whether this annotation has meaningful size
    var isValid: Bool {
        rect.width >= 5 && rect.height >= 5
    }

    /// The color actually painted inside a filled ellipse
    var effectiveFillColor: CodableColor {
        fillColor ?? style.color
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
        self.blockSize = max(4, min(128, blockSize))
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

    /// Optional lead point for step annotation pointing arrow/line
    var leadPoint: CGPoint?

    init(id: UUID = UUID(), position: CGPoint, number: Int, size: CGFloat = 24, color: CodableColor = CodableColor(.red), leadPoint: CGPoint? = nil) {
        self.id = id
        self.position = position
        self.number = max(1, number)
        self.size = max(12, min(256, size))
        self.color = color
        self.leadPoint = leadPoint
    }

    /// The circle bounding rectangle centered at position (excluding leadPoint)
    var circleRect: CGRect {
        CGRect(
            x: position.x - size / 2,
            y: position.y - size / 2,
            width: size,
            height: size
        )
    }

    /// The bounding rectangle centered at position, expanded to cover leadPoint if set
    var bounds: CGRect {
        var rect = circleRect
        if let lp = leadPoint {
            rect = rect.union(CGRect(x: lp.x - 4, y: lp.y - 4, width: 8, height: 8))
        }
        return rect
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

// MARK: - Geometry Constraints

/// Pure geometry helpers shared by drawing tools and the in-place overlay.
enum GeometryConstraint {
    /// 1:1 square bounding box from a start point toward a drag point (⌘ constraint).
    static func squareRect(from start: CGPoint, to end: CGPoint) -> CGRect {
        let side = max(abs(end.x - start.x), abs(end.y - start.y))
        let originX = end.x >= start.x ? start.x : start.x - side
        let originY = end.y >= start.y ? start.y : start.y - side
        return CGRect(x: originX, y: originY, width: side, height: side)
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
            let threshold = a.style.lineWidth / 2 + tolerance
            if let cp = a.controlPoint {
                let steps = 16
                var prev = a.startPoint
                for i in 1...steps {
                    let t = CGFloat(i) / CGFloat(steps)
                    let oneMinusT = 1.0 - t
                    let cur = CGPoint(
                        x: oneMinusT * oneMinusT * a.startPoint.x + 2 * oneMinusT * t * cp.x + t * t * a.endPoint.x,
                        y: oneMinusT * oneMinusT * a.startPoint.y + 2 * oneMinusT * t * cp.y + t * t * a.endPoint.y
                    )
                    if distanceToSegment(p: point, a: prev, b: cur) <= threshold {
                        return true
                    }
                    prev = cur
                }
            } else {
                let d = distanceToSegment(p: point, a: a.startPoint, b: a.endPoint)
                if d <= threshold { return true }
            }
            let headRadius = max(a.style.lineWidth * 3.5, 12) + tolerance
            return hypot(point.x - a.endPoint.x, point.y - a.endPoint.y) <= headRadius

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
            if hypot(point.x - n.position.x, point.y - n.position.y) <= (n.size / 2 + tolerance) {
                return true
            }
            if let lp = n.leadPoint {
                return distanceToSegment(p: point, a: n.position, b: lp) <= (2.0 + tolerance)
            }
            return false

        case .privacyRedaction(let pr):
            return pr.rect.insetBy(dx: -tolerance, dy: -tolerance).contains(point)

        case .calloutBubble(let cb):
            if cb.rect.insetBy(dx: -tolerance, dy: -tolerance).contains(point) {
                return true
            }
            let attachPoint = CGPoint(
                x: max(cb.rect.minX + 8, min(cb.rect.maxX - 8, cb.targetPoint.x)),
                y: cb.targetPoint.y < cb.rect.minY ? cb.rect.minY : (cb.targetPoint.y > cb.rect.maxY ? cb.rect.maxY : cb.rect.midY)
            )
            return distanceToSegment(p: point, a: attachPoint, b: cb.targetPoint) <= (4.0 + tolerance)
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

    /// Returns a new annotation shifted by the specified delta.
    func offsetBy(dx: CGFloat, dy: CGFloat) -> Annotation {
        switch self {
        case .rectangle(var r):
            r.rect.origin.x += dx; r.rect.origin.y += dy
            return .rectangle(r)
        case .ellipse(var e):
            e.rect.origin.x += dx; e.rect.origin.y += dy
            return .ellipse(e)
        case .line(var l):
            l.startPoint.x += dx; l.startPoint.y += dy
            l.endPoint.x += dx; l.endPoint.y += dy
            return .line(l)
        case .freehand(var f):
            f.points = f.points.map { CGPoint(x: $0.x + dx, y: $0.y + dy) }
            return .freehand(f)
        case .arrow(var a):
            a.startPoint.x += dx; a.startPoint.y += dy
            a.endPoint.x += dx; a.endPoint.y += dy
            if let cp = a.controlPoint {
                a.controlPoint = CGPoint(x: cp.x + dx, y: cp.y + dy)
            }
            return .arrow(a)
        case .highlight(var h):
            h.rect.origin.x += dx; h.rect.origin.y += dy
            return .highlight(h)
        case .mosaic(var m):
            m.rect.origin.x += dx; m.rect.origin.y += dy
            return .mosaic(m)
        case .text(var t):
            t.position.x += dx; t.position.y += dy
            return .text(t)
        case .numberLabel(var n):
            n.position.x += dx; n.position.y += dy
            if let lp = n.leadPoint {
                n.leadPoint = CGPoint(x: lp.x + dx, y: lp.y + dy)
            }
            return .numberLabel(n)
        case .privacyRedaction(var pr):
            pr.rect.origin.x += dx; pr.rect.origin.y += dy
            return .privacyRedaction(pr)
        case .calloutBubble(var cb):
            cb.rect.origin.x += dx; cb.rect.origin.y += dy
            cb.targetPoint.x += dx; cb.targetPoint.y += dy
            return .calloutBubble(cb)
        }
    }

    /// Returns a new annotation with updated color if applicable.
    func updatingColor(_ color: CodableColor) -> Annotation {
        switch self {
        case .rectangle(var r):
            r.style.color = color; return .rectangle(r)
        case .ellipse(var e):
            e.style.color = color; return .ellipse(e)
        case .line(var l):
            l.style.color = color; return .line(l)
        case .freehand(var f):
            f.style.color = color; return .freehand(f)
        case .arrow(var a):
            a.style.color = color; return .arrow(a)
        case .highlight(var h):
            h.color = color; return .highlight(h)
        case .mosaic:
            return self
        case .text(var t):
            t.style.color = color; return .text(t)
        case .numberLabel(var n):
            n.color = color; return .numberLabel(n)
        case .privacyRedaction(var pr):
            pr.fillColor = color; return .privacyRedaction(pr)
        case .calloutBubble(var cb):
            cb.bubbleColor = color; return .calloutBubble(cb)
        }
    }

    /// Returns a new annotation with an updated background fill color if applicable.
    func updatingFillColor(_ color: CodableColor) -> Annotation {
        switch self {
        case .rectangle(var r):
            r.fillColor = color; return .rectangle(r)
        case .ellipse(var e):
            e.fillColor = color; return .ellipse(e)
        default:
            return self
        }
    }

    /// Returns a new annotation with updated line width if applicable.
    func updatingLineWidth(_ width: CGFloat) -> Annotation {
        switch self {
        case .rectangle(var r):
            r.style.lineWidth = width; return .rectangle(r)
        case .ellipse(var e):
            e.style.lineWidth = width; return .ellipse(e)
        case .line(var l):
            l.style.lineWidth = width; return .line(l)
        case .freehand(var f):
            f.style.lineWidth = width; return .freehand(f)
        case .arrow(var a):
            a.style.lineWidth = width; return .arrow(a)
        default:
            return self
        }
    }

    /// Scales all geometric coordinates and visual dimensions of this annotation by the given factor (e.g. Retina scaleFactor).
    func scaled(by factor: CGFloat) -> Annotation {
        guard factor > 0 && abs(factor - 1.0) > 0.001 else { return self }
        let scaleRect: (CGRect) -> CGRect = { r in
            CGRect(
                x: r.origin.x * factor,
                y: r.origin.y * factor,
                width: r.size.width * factor,
                height: r.size.height * factor
            )
        }
        let scalePoint: (CGPoint) -> CGPoint = { p in
            CGPoint(x: p.x * factor, y: p.y * factor)
        }

        switch self {
        case .rectangle(var r):
            r.rect = scaleRect(r.rect)
            r.style.lineWidth *= factor
            return .rectangle(r)
        case .ellipse(var e):
            e.rect = scaleRect(e.rect)
            e.style.lineWidth *= factor
            return .ellipse(e)
        case .line(var l):
            l.startPoint = scalePoint(l.startPoint)
            l.endPoint = scalePoint(l.endPoint)
            l.style.lineWidth *= factor
            return .line(l)
        case .freehand(var f):
            f.points = f.points.map(scalePoint)
            f.style.lineWidth *= factor
            return .freehand(f)
        case .arrow(var a):
            a.startPoint = scalePoint(a.startPoint)
            a.endPoint = scalePoint(a.endPoint)
            if let cp = a.controlPoint {
                a.controlPoint = scalePoint(cp)
            }
            a.style.lineWidth *= factor
            return .arrow(a)
        case .highlight(var h):
            h.rect = scaleRect(h.rect)
            return .highlight(h)
        case .mosaic(var m):
            m.rect = scaleRect(m.rect)
            m.blockSize = max(4, min(128, Int(round(CGFloat(m.blockSize) * factor))))
            return .mosaic(m)
        case .text(var t):
            t.position = scalePoint(t.position)
            t.style.fontSize *= factor
            if let wrap = t.wrapWidth {
                t.wrapWidth = wrap * factor
            }
            return .text(t)
        case .numberLabel(var n):
            n.position = scalePoint(n.position)
            n.size = max(12, min(256, n.size * factor))
            if let lp = n.leadPoint {
                n.leadPoint = scalePoint(lp)
            }
            return .numberLabel(n)
        case .privacyRedaction(var pr):
            pr.rect = scaleRect(pr.rect)
            return .privacyRedaction(pr)
        case .calloutBubble(var cb):
            cb.rect = scaleRect(cb.rect)
            cb.targetPoint = scalePoint(cb.targetPoint)
            cb.style.fontSize *= factor
            return .calloutBubble(cb)
        }
    }
}

// MARK: - Interactive Control Handles for Resizing & Point Manipulation

enum AnnotationHandle: Equatable, Sendable {
    case topLeft
    case topRight
    case bottomLeft
    case bottomRight
    case startPoint
    case endPoint
    case controlPoint
    case leadPoint
    case targetPoint
    case leftEdge
    case rightEdge
    case topEdge
    case bottomEdge

    var isSpecialPoint: Bool {
        switch self {
        case .controlPoint, .leadPoint, .targetPoint:
            return true
        default:
            return false
        }
    }

    var cursor: NSCursor {
        switch self {
        case .topLeft, .bottomRight:
            return NSCursor.diagonalResizeCursor(northWestSouthEast: true)
        case .topRight, .bottomLeft:
            return NSCursor.diagonalResizeCursor(northWestSouthEast: false)
        case .leftEdge, .rightEdge:
            return .resizeLeftRight
        case .topEdge, .bottomEdge:
            return .resizeUpDown
        case .startPoint, .endPoint, .controlPoint, .leadPoint, .targetPoint:
            return .crosshair
        }
    }
}

/// Extra constraints applied while dragging an annotation control handle.
struct HandleDragOptions: Sendable {
    /// Hold ⌘ while resizing to lock the current width/height ratio
    var preserveAspectRatio: Bool = false

    static let standard = HandleDragOptions()
}

extension Annotation {
    /// Returns all interactive handles for this annotation in its coordinate space.
    func controlHandles(padding: CGFloat = 0) -> [(handle: AnnotationHandle, point: CGPoint)] {
        let cornerHandles: (CGRect) -> [(AnnotationHandle, CGPoint)] = { rect in
            let r = rect.insetBy(dx: -padding, dy: -padding)
            return [
                (.topLeft, CGPoint(x: r.minX, y: r.minY)),
                (.topRight, CGPoint(x: r.maxX, y: r.minY)),
                (.bottomLeft, CGPoint(x: r.minX, y: r.maxY)),
                (.bottomRight, CGPoint(x: r.maxX, y: r.maxY))
            ]
        }

        switch self {
        case .rectangle(let r):
            return cornerHandles(r.rect)
        case .ellipse(let e):
            return cornerHandles(e.rect)
        case .mosaic(let m):
            return cornerHandles(m.rect)
        case .highlight(let h):
            return cornerHandles(h.rect)
        case .privacyRedaction(let pr):
            return cornerHandles(pr.rect)
        case .freehand(let f):
            return cornerHandles(f.bounds)
        case .line(let l):
            return [
                (.startPoint, l.startPoint),
                (.endPoint, l.endPoint)
            ]
        case .arrow(let a):
            let controlPos = a.controlPoint ?? CGPoint(
                x: (a.startPoint.x + a.endPoint.x) / 2,
                y: (a.startPoint.y + a.endPoint.y) / 2
            )
            return [
                (.startPoint, a.startPoint),
                (.controlPoint, controlPos),
                (.endPoint, a.endPoint)
            ]
        case .numberLabel(let n):
            let leadPos = n.leadPoint ?? CGPoint(x: n.position.x + n.size / 2 + 8, y: n.position.y)
            let resizePos = CGPoint(
                x: n.position.x + (n.size / 2 + padding) * 0.7071,
                y: n.position.y + (n.size / 2 + padding) * 0.7071
            )
            return [
                (.leadPoint, leadPos),
                (.bottomRight, resizePos)
            ]
        case .calloutBubble(let cb):
            return cornerHandles(cb.rect) + [(.targetPoint, cb.targetPoint)]
        case .text(let t):
            let b = t.bounds.insetBy(dx: -padding, dy: -padding)
            return [
                (.topLeft, CGPoint(x: b.minX, y: b.minY)),
                (.topRight, CGPoint(x: b.maxX, y: b.minY)),
                (.bottomLeft, CGPoint(x: b.minX, y: b.maxY)),
                (.bottomRight, CGPoint(x: b.maxX, y: b.maxY)),
                (.topEdge, CGPoint(x: b.midX, y: b.minY)),
                (.bottomEdge, CGPoint(x: b.midX, y: b.maxY)),
                (.leftEdge, CGPoint(x: b.minX, y: b.midY)),
                (.rightEdge, CGPoint(x: b.maxX, y: b.midY))
            ]
        }
    }

    /// Tests if a point hits any of this annotation's control handles.
    func hitTestHandle(at point: CGPoint, padding: CGFloat = 0, tolerance: CGFloat = 10.0) -> AnnotationHandle? {
        for (handle, handlePoint) in controlHandles(padding: padding) {
            if hypot(point.x - handlePoint.x, point.y - handlePoint.y) <= tolerance {
                return handle
            }
        }
        return nil
    }

    /// Returns a modified annotation after dragging the specified handle to a new point.
    /// - Parameter options: Extra constraints, e.g. ⌘-held proportional resizing.
    func applyingHandleDrag(
        _ handle: AnnotationHandle,
        to point: CGPoint,
        options: HandleDragOptions = .standard
    ) -> Annotation {
        let resizedRect: (CGRect, CGFloat, CGFloat) -> CGRect = { rect, minW, minH in
            let anchor: CGPoint
            switch handle {
            case .topLeft:
                anchor = CGPoint(x: rect.maxX, y: rect.maxY)
            case .topRight:
                anchor = CGPoint(x: rect.minX, y: rect.maxY)
            case .bottomLeft:
                anchor = CGPoint(x: rect.maxX, y: rect.minY)
            case .bottomRight:
                anchor = CGPoint(x: rect.minX, y: rect.minY)
            default:
                return rect
            }
            var width = max(minW, abs(point.x - anchor.x))
            var height = max(minH, abs(point.y - anchor.y))

            // ⌘ held: lock the pre-drag width/height ratio
            if options.preserveAspectRatio, rect.width > 0.001, rect.height > 0.001 {
                let aspect = rect.width / rect.height
                if width / max(height, 0.001) > aspect {
                    height = width / aspect
                } else {
                    width = height * aspect
                }
            }

            let dirX: CGFloat = point.x >= anchor.x ? 1 : -1
            let dirY: CGFloat = point.y >= anchor.y ? 1 : -1
            let minX = dirX >= 0 ? anchor.x : anchor.x - width
            let minY = dirY >= 0 ? anchor.y : anchor.y - height
            return CGRect(x: minX, y: minY, width: width, height: height)
        }

        switch self {
        case .rectangle(var r):
            r.rect = resizedRect(r.rect, 6, 6)
            return .rectangle(r)
        case .ellipse(var e):
            e.rect = resizedRect(e.rect, 6, 6)
            return .ellipse(e)
        case .mosaic(var m):
            m.rect = resizedRect(m.rect, 10, 10)
            return .mosaic(m)
        case .highlight(var h):
            h.rect = resizedRect(h.rect, 6, 6)
            return .highlight(h)
        case .privacyRedaction(var pr):
            pr.rect = resizedRect(pr.rect, 6, 6)
            return .privacyRedaction(pr)
        case .freehand(var f):
            let oldBounds = f.bounds
            guard oldBounds.width >= 2, oldBounds.height >= 2 else { return self }
            let newBounds = resizedRect(oldBounds, 8, 8)
            f.points = f.points.map { p in
                let nx = (p.x - oldBounds.minX) / oldBounds.width
                let ny = (p.y - oldBounds.minY) / oldBounds.height
                return CGPoint(
                    x: newBounds.minX + nx * newBounds.width,
                    y: newBounds.minY + ny * newBounds.height
                )
            }
            return .freehand(f)
        case .line(var l):
            if handle == .startPoint {
                l.startPoint = point
            } else if handle == .endPoint {
                l.endPoint = point
            }
            return .line(l)
        case .arrow(var a):
            switch handle {
            case .startPoint:
                a.startPoint = point
            case .endPoint:
                a.endPoint = point
            case .controlPoint:
                a.controlPoint = point
            default:
                break
            }
            return .arrow(a)
        case .numberLabel(var n):
            switch handle {
            case .leadPoint:
                let dist = hypot(point.x - n.position.x, point.y - n.position.y)
                n.leadPoint = dist > (n.size / 2 + 4) ? point : nil
            case .bottomRight:
                let dist = hypot(point.x - n.position.x, point.y - n.position.y)
                n.size = max(16, min(256, dist * 2))
            default:
                break
            }
            return .numberLabel(n)
        case .calloutBubble(var cb):
            if handle == .targetPoint {
                cb.targetPoint = point
            } else {
                cb.rect = resizedRect(cb.rect, 30, 22)
            }
            return .calloutBubble(cb)
        case .text(var t):
            let oldBounds = t.bounds
            switch handle {
            case .topLeft, .topRight, .bottomLeft, .bottomRight:
                // Corner handle: scale font size (and wrap width) proportionally
                let anchor: CGPoint
                switch handle {
                case .topLeft:
                    anchor = CGPoint(x: oldBounds.maxX, y: oldBounds.maxY)
                case .topRight:
                    anchor = CGPoint(x: oldBounds.minX, y: oldBounds.maxY)
                case .bottomLeft:
                    anchor = CGPoint(x: oldBounds.maxX, y: oldBounds.minY)
                default:
                    anchor = CGPoint(x: oldBounds.minX, y: oldBounds.minY)
                }
                let oldDiagonal = hypot(oldBounds.width, oldBounds.height)
                let newDiagonal = hypot(abs(point.x - anchor.x), abs(point.y - anchor.y))
                let factor = max(0.05, newDiagonal / max(oldDiagonal, 1))
                t.style.fontSize = max(8, min(144, t.style.fontSize * factor))
                if let wrap = t.wrapWidth {
                    t.wrapWidth = max(t.style.fontSize, wrap * factor)
                }
                // Keep the anchor corner fixed after re-measuring
                let newBounds = t.bounds
                t.position.x += anchor.x - (handle == .topLeft || handle == .bottomLeft ? newBounds.maxX : newBounds.minX)
                t.position.y += anchor.y - (handle == .topLeft || handle == .topRight ? newBounds.maxY : newBounds.minY)
            case .leftEdge:
                // Left edge: change the wrap width, keeping the right edge fixed
                let right = oldBounds.maxX
                t.wrapWidth = max(t.style.fontSize, right - point.x)
                t.position.x = right - t.bounds.width
            case .rightEdge:
                // Right edge: change the wrap width, keeping the left edge fixed
                t.wrapWidth = max(t.style.fontSize, point.x - oldBounds.minX)
            case .topEdge, .bottomEdge:
                // Top/bottom edge: scale the font size from the vertical drag distance
                let anchorY = handle == .topEdge ? oldBounds.maxY : oldBounds.minY
                let newHeight = max(10, abs(point.y - anchorY))
                let factor = max(0.1, newHeight / max(oldBounds.height, 1))
                t.style.fontSize = max(8, min(144, t.style.fontSize * factor))
                if let wrap = t.wrapWidth {
                    t.wrapWidth = max(t.style.fontSize, wrap * factor)
                }
                // Keep the anchor edge fixed after re-measuring
                let newBounds = t.bounds
                t.position.y += anchorY - (handle == .topEdge ? newBounds.maxY : newBounds.minY)
            default:
                break
            }
            return .text(t)
        }
    }
}
