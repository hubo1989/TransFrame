import Foundation
import CoreGraphics
import AppKit

/// Tool for drawing rectangle annotations.
/// User drags to define opposite corners of the rectangle.
/// Hold ⌘ while dragging to constrain to a square.
@MainActor
struct RectangleTool: AnnotationTool {
    // MARK: - Properties

    let toolType: AnnotationToolType = .rectangle

    var strokeStyle: StrokeStyle = .default

    var textStyle: TextStyle = .default

    /// Whether to create filled (solid) rectangles
    var isFilled: Bool = false

    /// Background fill color for filled rectangles (nil = stroke color)
    var fillColor: CodableColor? = nil

    private var drawingState = DrawingState()

    // MARK: - AnnotationTool Conformance

    var isActive: Bool {
        drawingState.isDrawing
    }

    var currentAnnotation: Annotation? {
        guard isActive else { return nil }
        let rect = calculateRect()
        guard rect.width > 0 && rect.height > 0 else { return nil }
        return .rectangle(RectangleAnnotation(rect: rect, style: strokeStyle, isFilled: isFilled, fillColor: fillColor))
    }

    mutating func beginDrawing(at point: CGPoint) {
        drawingState = DrawingState(startPoint: point)
        drawingState.isDrawing = true
    }

    mutating func continueDrawing(to point: CGPoint) {
        guard isActive else { return }
        // Only need start and current point for rectangles
        if drawingState.points.count > 1 {
            drawingState.points[1] = point
        } else {
            drawingState.points.append(point)
        }
    }

    mutating func endDrawing(at point: CGPoint) -> Annotation? {
        guard isActive else { return nil }

        continueDrawing(to: point)
        let rect = calculateRect()
        drawingState.reset()

        // Only create annotation if it has meaningful size
        guard rect.width >= 2 && rect.height >= 2 else { return nil }

        return .rectangle(RectangleAnnotation(rect: rect, style: strokeStyle, isFilled: isFilled, fillColor: fillColor))
    }

    mutating func cancelDrawing() {
        drawingState.reset()
    }

    // MARK: - Private Methods

    private func calculateRect() -> CGRect {
        guard drawingState.points.count >= 2 else {
            return .zero
        }

        let start = drawingState.startPoint
        guard let end = drawingState.points.last else {
            return .zero
        }

        var minX = min(start.x, end.x)
        var minY = min(start.y, end.y)
        var maxX = max(start.x, end.x)
        var maxY = max(start.y, end.y)

        // ⌘ held: constrain width and height to a 1:1 square
        if NSEvent.modifierFlags.contains(.command) {
            let side = max(maxX - minX, maxY - minY)
            if end.x >= start.x { maxX = start.x + side } else { minX = start.x - side }
            if end.y >= start.y { maxY = start.y + side } else { minY = start.y - side }
        }

        return CGRect(x: minX, y: minY, width: maxX - minX, height: maxY - minY)
    }
}
