import Foundation
import CoreGraphics
import AppKit

/// Tool for drawing ellipse annotations.
/// User drags to define the bounding rectangle of the ellipse.
/// Hold ⌘ while dragging to constrain to a circle.
@MainActor
struct EllipseTool: AnnotationTool {
    // MARK: - Properties

    let toolType: AnnotationToolType = .ellipse

    var strokeStyle: StrokeStyle = .default

    var textStyle: TextStyle = .default

    /// Whether to create filled (solid) ellipses
    var isFilled: Bool = false

    /// Background fill color for filled ellipses (nil = stroke color)
    var fillColor: CodableColor? = nil

    private var drawingState = DrawingState()

    // MARK: - AnnotationTool Conformance

    var isActive: Bool {
        drawingState.isDrawing
    }

    var currentAnnotation: Annotation? {
        guard isActive else { return nil }
        let rect = calculatedRect(constrainSquare: NSEvent.modifierFlags.contains(.command))
        guard rect.width > 0 && rect.height > 0 else { return nil }
        return .ellipse(EllipseAnnotation(rect: rect, style: strokeStyle, isFilled: isFilled, fillColor: fillColor))
    }

    mutating func beginDrawing(at point: CGPoint) {
        drawingState = DrawingState(startPoint: point)
        drawingState.isDrawing = true
    }

    mutating func continueDrawing(to point: CGPoint) {
        guard isActive else { return }
        if drawingState.points.count > 1 {
            drawingState.points[1] = point
        } else {
            drawingState.points.append(point)
        }
    }

    mutating func endDrawing(at point: CGPoint) -> Annotation? {
        guard isActive else { return nil }

        continueDrawing(to: point)
        let rect = calculatedRect(constrainSquare: NSEvent.modifierFlags.contains(.command))
        drawingState.reset()

        guard rect.width >= 5 && rect.height >= 5 else { return nil }

        return .ellipse(EllipseAnnotation(rect: rect, style: strokeStyle, isFilled: isFilled, fillColor: fillColor))
    }

    mutating func cancelDrawing() {
        drawingState.reset()
    }

    // MARK: - Internal Methods

    /// Current drag rect. `constrainSquare` mirrors the ⌘ state and is
    /// injectable so tests can exercise the 1:1 circle constraint without synthesizing keys.
    func calculatedRect(constrainSquare: Bool) -> CGRect {
        guard drawingState.points.count >= 2 else { return .zero }

        let start = drawingState.startPoint
        guard let end = drawingState.points.last else { return .zero }

        if constrainSquare {
            return GeometryConstraint.squareRect(from: start, to: end)
        }

        let minX = min(start.x, end.x)
        let minY = min(start.y, end.y)
        let maxX = max(start.x, end.x)
        let maxY = max(start.y, end.y)
        return CGRect(x: minX, y: minY, width: maxX - minX, height: maxY - minY)
    }
}
