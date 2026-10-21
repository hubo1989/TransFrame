import SwiftUI
import AppKit
import CoreImage
import os

/// Cache for pixelated mosaic CGImages to avoid recomputing CIPixellate every frame during gestures
final class MosaicCache: @unchecked Sendable {
    static let shared = MosaicCache()
    private let cache = NSCache<NSString, CGImage>()

    init() {
        cache.countLimit = 64
    }

    func key(rect: CGRect, blockSize: Int, imageID: ObjectIdentifier) -> NSString {
        "\(imageID)-\(Int(rect.origin.x))_\(Int(rect.origin.y))_\(Int(rect.width))_\(Int(rect.height))_\(blockSize)" as NSString
    }

    func image(for key: NSString) -> CGImage? {
        cache.object(forKey: key)
    }

    func setImage(_ image: CGImage, for key: NSString) {
        cache.setObject(image, forKey: key)
    }

    func clear() {
        cache.removeAllObjects()
    }
}

/// SwiftUI Canvas view for drawing and displaying annotations.
/// Renders existing annotations and in-progress drawing.
struct AnnotationCanvas: View {
    // MARK: - Shared CIContext for performance
    
    private static let sharedCIContext = CIContext()
    private static let logger = Logger.ui
    
    // MARK: - Properties

    /// The annotations to display
    let annotations: [Annotation]

    /// The current in-progress annotation (being drawn)
    let currentAnnotation: Annotation?

    /// The size of the canvas (matches image size)
    let canvasSize: CGSize

    /// Scale factor for rendering (canvas to view)
    let scale: CGFloat

    /// Index of the selected annotation (nil = none selected)
    var selectedIndex: Int?

    /// The original image for mosaic effect
    var sourceImage: CGImage?

    // MARK: - Body

    var body: some View {
        Canvas { context, size in
            // Draw all completed annotations
            for (index, annotation) in annotations.enumerated() {
                drawAnnotation(annotation, in: &context, size: size)

                // Draw selection indicator if this annotation is selected
                if index == selectedIndex {
                    drawSelectionIndicator(for: annotation, in: &context, size: size)
                }
            }

            // Draw current in-progress annotation
            if let current = currentAnnotation {
                drawAnnotation(current, in: &context, size: size)
            }
        }
        .allowsHitTesting(false) // Pass through mouse events
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(Text(accessibilityDescription))
    }

    /// Generates an accessibility description of the annotations
    private var accessibilityDescription: String {
        let totalCount = annotations.count + (currentAnnotation != nil ? 1 : 0)
        if totalCount == 0 {
            return "No annotations"
        }

        var parts: [String] = []

        // Count each annotation type
        var rectangleCount = 0, freehandCount = 0, arrowCount = 0, textCount = 0
        var ellipseCount = 0, lineCount = 0, mosaicCount = 0, highlightCount = 0, numberLabelCount = 0
        var privacyRedactionCount = 0, calloutBubbleCount = 0

        // Count from existing annotations
        for annotation in annotations {
            switch annotation {
            case .rectangle: rectangleCount += 1
            case .freehand: freehandCount += 1
            case .arrow: arrowCount += 1
            case .text: textCount += 1
            case .ellipse: ellipseCount += 1
            case .line: lineCount += 1
            case .mosaic: mosaicCount += 1
            case .highlight: highlightCount += 1
            case .numberLabel: numberLabelCount += 1
            case .privacyRedaction: privacyRedactionCount += 1
            case .calloutBubble: calloutBubbleCount += 1
            }
        }
        
        // Also count currentAnnotation if present
        if let current = currentAnnotation {
            switch current {
            case .rectangle: rectangleCount += 1
            case .freehand: freehandCount += 1
            case .arrow: arrowCount += 1
            case .text: textCount += 1
            case .ellipse: ellipseCount += 1
            case .line: lineCount += 1
            case .mosaic: mosaicCount += 1
            case .highlight: highlightCount += 1
            case .numberLabel: numberLabelCount += 1
            case .privacyRedaction: privacyRedactionCount += 1
            case .calloutBubble: calloutBubbleCount += 1
            }
        }

        if rectangleCount > 0 { parts.append("\(rectangleCount) rectangle\(rectangleCount == 1 ? "" : "s")") }
        if ellipseCount > 0 { parts.append("\(ellipseCount) ellipse\(ellipseCount == 1 ? "" : "s")") }
        if lineCount > 0 { parts.append("\(lineCount) line\(lineCount == 1 ? "" : "s")") }
        if arrowCount > 0 { parts.append("\(arrowCount) arrow\(arrowCount == 1 ? "" : "s")") }
        if freehandCount > 0 { parts.append("\(freehandCount) drawing\(freehandCount == 1 ? "" : "s")") }
        if highlightCount > 0 { parts.append("\(highlightCount) highlight\(highlightCount == 1 ? "" : "s")") }
        if mosaicCount > 0 { parts.append("\(mosaicCount) mosaic\(mosaicCount == 1 ? "" : "s")") }
        if textCount > 0 { parts.append("\(textCount) text\(textCount == 1 ? "" : "s")") }
        if numberLabelCount > 0 { parts.append("\(numberLabelCount) number label\(numberLabelCount == 1 ? "" : "s")") }
        if privacyRedactionCount > 0 { parts.append("\(privacyRedactionCount) redaction\(privacyRedactionCount == 1 ? "" : "s")") }
        if calloutBubbleCount > 0 { parts.append("\(calloutBubbleCount) bubble\(calloutBubbleCount == 1 ? "" : "s")") }

        return "Annotations: \(parts.joined(separator: ", "))"
    }

    // MARK: - Drawing Methods

    /// Draws an annotation in the graphics context
    private func drawAnnotation(
        _ annotation: Annotation,
        in context: inout GraphicsContext,
        size: CGSize
    ) {
        switch annotation {
        case .rectangle(let rect):
            drawRectangle(rect, in: &context, size: size)
        case .ellipse(let ellipse):
            drawEllipse(ellipse, in: &context, size: size)
        case .line(let line):
            drawLine(line, in: &context, size: size)
        case .freehand(let freehand):
            drawFreehand(freehand, in: &context, size: size)
        case .arrow(let arrow):
            drawArrow(arrow, in: &context, size: size)
        case .highlight(let highlight):
            drawHighlight(highlight, in: &context, size: size)
        case .mosaic(let mosaic):
            drawMosaic(mosaic, in: &context, size: size)
        case .text(let text):
            drawText(text, in: &context, size: size)
        case .numberLabel(let label):
            drawNumberLabel(label, in: &context, size: size)
        case .privacyRedaction(let redaction):
            drawPrivacyRedaction(redaction, in: &context, size: size)
        case .calloutBubble(let bubble):
            drawCalloutBubble(bubble, in: &context, size: size)
        }
    }

    /// Draws a solid privacy redaction annotation
    private func drawPrivacyRedaction(
        _ annotation: PrivacyRedactionAnnotation,
        in context: inout GraphicsContext,
        size: CGSize
    ) {
        let scaledRect = scaleRect(annotation.rect)
        let path = Path(scaledRect)
        context.fill(path, with: .color(annotation.fillColor.color))
    }

    /// Draws a callout bubble annotation
    private func drawCalloutBubble(
        _ annotation: CalloutBubbleAnnotation,
        in context: inout GraphicsContext,
        size: CGSize
    ) {
        let scaledBubbleRect = scaleRect(annotation.rect)
        let scaledTarget = scalePoint(annotation.targetPoint)

        // Draw pointer tail line
        var tailPath = Path()
        tailPath.move(to: scaledTarget)
        let attachPoint = CGPoint(
            x: max(scaledBubbleRect.minX + 8 * scale, min(scaledBubbleRect.maxX - 8 * scale, scaledTarget.x)),
            y: scaledTarget.y < scaledBubbleRect.minY ? scaledBubbleRect.minY : scaledBubbleRect.maxY
        )
        tailPath.addLine(to: attachPoint)
        context.stroke(
            tailPath,
            with: .color(annotation.bubbleColor.color),
            style: SwiftUI.StrokeStyle(lineWidth: 2 * scale, lineCap: .round)
        )

        // Draw bubble background
        let bubblePath = Path(roundedRect: scaledBubbleRect, cornerRadius: 8 * scale)
        context.fill(bubblePath, with: .color(annotation.bubbleColor.color))

        // Draw text inside bubble
        let font = Font.system(size: max(10, annotation.style.fontSize * scale), weight: .medium)
        let text = Text(annotation.text)
            .font(font)
            .foregroundColor(annotation.style.color.color)

        context.draw(
            context.resolve(text),
            in: scaledBubbleRect.insetBy(dx: 6 * scale, dy: 6 * scale)
        )
    }

    /// Draws a rectangle annotation
    private func drawRectangle(
        _ annotation: RectangleAnnotation,
        in context: inout GraphicsContext,
        size: CGSize
    ) {
        let scaledRect = scaleRect(annotation.rect)
        let path = Path(scaledRect)

        if annotation.isFilled {
            // Filled rectangle - solid color to hide underlying content
            context.fill(
                path,
                with: .color(annotation.style.color.color)
            )
        } else {
            // Hollow rectangle - outline only
            context.stroke(
                path,
                with: .color(annotation.style.color.color),
                lineWidth: annotation.style.lineWidth * scale
            )
        }
    }

    /// Draws a freehand annotation
    private func drawFreehand(
        _ annotation: FreehandAnnotation,
        in context: inout GraphicsContext,
        size: CGSize
    ) {
        guard annotation.points.count >= 2 else { return }

        var path = Path()
        let scaledPoints = annotation.points.map { scalePoint($0) }

        path.move(to: scaledPoints[0])
        for point in scaledPoints.dropFirst() {
            path.addLine(to: point)
        }

        context.stroke(
            path,
            with: .color(annotation.style.color.color),
            style: SwiftUI.StrokeStyle(
                lineWidth: annotation.style.lineWidth * scale,
                lineCap: .round,
                lineJoin: .round
            )
        )
    }

    /// Draws an arrow annotation
    private func drawArrow(
        _ annotation: ArrowAnnotation,
        in context: inout GraphicsContext,
        size: CGSize
    ) {
        let scaledStart = scalePoint(annotation.startPoint)
        let scaledEnd = scalePoint(annotation.endPoint)
        let lineWidth = annotation.style.lineWidth * scale
        let color = annotation.style.color.color

        // Draw the main line
        var linePath = Path()
        linePath.move(to: scaledStart)
        linePath.addLine(to: scaledEnd)

        context.stroke(
            linePath,
            with: .color(color),
            style: SwiftUI.StrokeStyle(
                lineWidth: lineWidth,
                lineCap: .round,
                lineJoin: .round
            )
        )

        // Draw the arrowhead
        let arrowHeadLength = max(lineWidth * 4, 12 * scale)
        let arrowHeadAngle: CGFloat = .pi / 6 // 30 degrees

        // Calculate the angle of the line
        let dx = scaledEnd.x - scaledStart.x
        let dy = scaledEnd.y - scaledStart.y
        let angle = atan2(dy, dx)

        // Calculate arrowhead points
        let arrowPoint1 = CGPoint(
            x: scaledEnd.x - arrowHeadLength * cos(angle - arrowHeadAngle),
            y: scaledEnd.y - arrowHeadLength * sin(angle - arrowHeadAngle)
        )
        let arrowPoint2 = CGPoint(
            x: scaledEnd.x - arrowHeadLength * cos(angle + arrowHeadAngle),
            y: scaledEnd.y - arrowHeadLength * sin(angle + arrowHeadAngle)
        )

        // Draw filled arrowhead
        var arrowHeadPath = Path()
        arrowHeadPath.move(to: scaledEnd)
        arrowHeadPath.addLine(to: arrowPoint1)
        arrowHeadPath.addLine(to: arrowPoint2)
        arrowHeadPath.closeSubpath()

        context.fill(arrowHeadPath, with: .color(color))
    }

    /// Draws a text annotation
    private func drawText(
        _ annotation: TextAnnotation,
        in context: inout GraphicsContext,
        size: CGSize
    ) {
        guard !annotation.content.isEmpty else { return }

        let scaledPoint = scalePoint(annotation.position)
        let scaledFontSize = annotation.style.fontSize * scale

        let text = Text(annotation.content)
            .font(annotation.style.fontName == ".AppleSystemUIFont"
                  ? .system(size: scaledFontSize)
                  : .custom(annotation.style.fontName, size: scaledFontSize))
            .foregroundColor(annotation.style.color.color)

        context.draw(
            context.resolve(text),
            at: scaledPoint,
            anchor: .topLeading
        )
    }

    /// Draws an ellipse annotation
    private func drawEllipse(
        _ annotation: EllipseAnnotation,
        in context: inout GraphicsContext,
        size: CGSize
    ) {
        let scaledRect = scaleRect(annotation.rect)
        let path = Path(ellipseIn: scaledRect)
        
        if annotation.isFilled {
            // Filled ellipse - solid color to hide underlying content
            context.fill(
                path,
                with: .color(annotation.style.color.color)
            )
        } else {
            // Hollow ellipse - outline only
            context.stroke(
                path,
                with: .color(annotation.style.color.color),
                lineWidth: annotation.style.lineWidth * scale
            )
        }
    }

    /// Draws a line annotation
    private func drawLine(
        _ annotation: LineAnnotation,
        in context: inout GraphicsContext,
        size: CGSize
    ) {
        let scaledStart = scalePoint(annotation.startPoint)
        let scaledEnd = scalePoint(annotation.endPoint)

        var path = Path()
        path.move(to: scaledStart)
        path.addLine(to: scaledEnd)

        context.stroke(
            path,
            with: .color(annotation.style.color.color),
            lineWidth: annotation.style.lineWidth * scale
        )
    }

    /// Draws a highlight annotation
    private func drawHighlight(
        _ annotation: HighlightAnnotation,
        in context: inout GraphicsContext,
        size: CGSize
    ) {
        let scaledRect = scaleRect(annotation.rect)
        let path = Path(scaledRect)
        context.fill(
            path,
            with: .color(annotation.color.color.opacity(annotation.opacity))
        )
    }

    /// Draws a mosaic annotation (pixelation effect)
    private func drawMosaic(
        _ annotation: MosaicAnnotation,
        in context: inout GraphicsContext,
        size: CGSize
    ) {
        let scaledRect = scaleRect(annotation.rect)
        // Use blockSize directly, with small minimum to ensure visibility
        let blockSize: CGFloat = max(2, CGFloat(annotation.blockSize) * scale)

        // If we have source image, do real pixelation
        if let cgImage = sourceImage {
            let imageWidth = CGFloat(cgImage.width)
            let imageHeight = CGFloat(cgImage.height)

            // Convert scaled rect back to image coordinates
            let imageRect = CGRect(
                x: scaledRect.origin.x / scale,
                y: scaledRect.origin.y / scale,
                width: scaledRect.size.width / scale,
                height: scaledRect.size.height / scale
            )

            guard imageRect.width > 0 && imageRect.height > 0 else { return }

            let cacheKey = MosaicCache.shared.key(
                rect: imageRect,
                blockSize: annotation.blockSize,
                imageID: ObjectIdentifier(cgImage)
            )

            let outputCGImage: CGImage?
            if let cached = MosaicCache.shared.image(for: cacheKey) {
                outputCGImage = cached
            } else if let pixelatedCI = createPixelatedImage(
                from: cgImage,
                rect: imageRect,
                blockSize: blockSize / scale,
                canvasSize: CGSize(width: imageWidth, height: imageHeight)
            ), let rendered = Self.sharedCIContext.createCGImage(pixelatedCI, from: pixelatedCI.extent) {
                MosaicCache.shared.setImage(rendered, for: cacheKey)
                outputCGImage = rendered
            } else {
                outputCGImage = nil
            }

            if let outputCGImage {
                let nsImage = NSImage(cgImage: outputCGImage, size: NSSize(width: outputCGImage.width, height: outputCGImage.height))
                context.draw(Image(nsImage: nsImage), in: scaledRect)
                return
            }
        }

        // Fallback: draw colored blocks (improved version)
        var x = scaledRect.origin.x
        while x < scaledRect.origin.x + scaledRect.size.width {
            var y = scaledRect.origin.y
            while y < scaledRect.origin.y + scaledRect.size.height {
                let blockRect = CGRect(
                    x: x,
                    y: y,
                    width: min(blockSize, scaledRect.origin.x + scaledRect.size.width - x),
                    height: min(blockSize, scaledRect.origin.y + scaledRect.size.height - y)
                )
                let path = Path(blockRect)
                // Alternate colors for checkerboard pattern
                let isEven = Int(x / blockSize).isMultiple(of: 2) == Int(y / blockSize).isMultiple(of: 2)
                context.fill(path, with: .color(isEven ? .gray.opacity(0.6) : .gray.opacity(0.4)))
                y += blockSize
            }
            x += blockSize
        }
    }

    /// Creates a pixelated CIImage from the source image in the specified rect
    private func createPixelatedImage(
        from cgImage: CGImage,
        rect: CGRect,
        blockSize: CGFloat,
        canvasSize: CGSize
    ) -> CIImage? {
        let ciImage = CIImage(cgImage: cgImage)

        // Apply pixelation using CIPixellate filter
        guard let pixellateFilter = CIFilter(name: "CIPixellate") else {
            Self.logger.warning("CIPixellate filter not available, falling back to gray block")
            return nil
        }
        pixellateFilter.setValue(ciImage, forKey: kCIInputImageKey)
        pixellateFilter.setValue(max(1, blockSize), forKey: kCIInputScaleKey)

        guard let outputImage = pixellateFilter.outputImage else {
            Self.logger.warning("Pixellation failed, falling back to gray block")
            return nil
        }

        return outputImage.cropped(to: CGRect(
            x: rect.origin.x,
            y: canvasSize.height - rect.origin.y - rect.size.height,
            width: rect.size.width,
            height: rect.size.height
        ))
    }

    /// Draws a number label annotation
    private func drawNumberLabel(
        _ annotation: NumberLabelAnnotation,
        in context: inout GraphicsContext,
        size: CGSize
    ) {
        let scaledPoint = scalePoint(annotation.position)
        let scaledSize = annotation.size * scale
        let scaledRadius = scaledSize / 2

        // Draw circle background
        let circleRect = CGRect(
            x: scaledPoint.x - scaledRadius,
            y: scaledPoint.y - scaledRadius,
            width: scaledRadius * 2,
            height: scaledRadius * 2
        )
        let circlePath = Path(ellipseIn: circleRect)
        context.fill(circlePath, with: .color(annotation.color.color))

        // Draw number text
        let text = Text("\(annotation.number)")
            .font(.system(size: scaledRadius * 1.2, weight: .bold))
            .foregroundColor(.white)

        context.draw(
            context.resolve(text),
            at: scaledPoint,
            anchor: .center
        )
    }

    /// Draws a selection indicator around an annotation
    private func drawSelectionIndicator(
        for annotation: Annotation,
        in context: inout GraphicsContext,
        size: CGSize
    ) {
        let bounds = annotation.bounds
        let scaledBounds = scaleRect(bounds)

        // Add padding around the bounds
        let padding: CGFloat = 4 * scale
        let selectionRect = scaledBounds.insetBy(dx: -padding, dy: -padding)

        // Draw selection border (dashed blue line)
        let borderPath = Path(roundedRect: selectionRect, cornerRadius: 3 * scale)
        context.stroke(
            borderPath,
            with: .color(.accentColor),
            style: SwiftUI.StrokeStyle(
                lineWidth: 2,
                lineCap: .round,
                dash: [6, 4]
            )
        )

        // Draw corner handles
        let handleSize: CGFloat = 8
        let handlePositions = [
            CGPoint(x: selectionRect.minX, y: selectionRect.minY), // Top-left
            CGPoint(x: selectionRect.maxX, y: selectionRect.minY), // Top-right
            CGPoint(x: selectionRect.minX, y: selectionRect.maxY), // Bottom-left
            CGPoint(x: selectionRect.maxX, y: selectionRect.maxY)  // Bottom-right
        ]

        for position in handlePositions {
            let handleRect = CGRect(
                x: position.x - handleSize / 2,
                y: position.y - handleSize / 2,
                width: handleSize,
                height: handleSize
            )
            let handlePath = Path(ellipseIn: handleRect)

            // White fill with blue border
            context.fill(handlePath, with: .color(.white))
            context.stroke(handlePath, with: .color(.accentColor), lineWidth: 2)
        }
    }

    // MARK: - Coordinate Transformation

    /// Scales a point from image coordinates to view coordinates
    private func scalePoint(_ point: CGPoint) -> CGPoint {
        CGPoint(x: point.x * scale, y: point.y * scale)
    }

    /// Scales a rect from image coordinates to view coordinates
    private func scaleRect(_ rect: CGRect) -> CGRect {
        CGRect(
            x: rect.origin.x * scale,
            y: rect.origin.y * scale,
            width: rect.width * scale,
            height: rect.height * scale
        )
    }
}

// MARK: - Preview

#if DEBUG
#Preview {
    let annotations: [Annotation] = [
        .rectangle(RectangleAnnotation(
            rect: CGRect(x: 50, y: 50, width: 100, height: 80),
            style: .default,
            isFilled: false
        )),
        .rectangle(RectangleAnnotation(
            rect: CGRect(x: 200, y: 50, width: 100, height: 80),
            style: StrokeStyle(color: CodableColor(.blue), lineWidth: 3.0),
            isFilled: true
        )),
        .freehand(FreehandAnnotation(
            points: [
                CGPoint(x: 350, y: 100),
                CGPoint(x: 400, y: 150),
                CGPoint(x: 450, y: 120),
                CGPoint(x: 500, y: 180)
            ],
            style: StrokeStyle(color: CodableColor(.green), lineWidth: 3.0)
        )),
        .text(TextAnnotation(
            position: CGPoint(x: 100, y: 200),
            content: "Hello World",
            style: .default
        ))
    ]

    return AnnotationCanvas(
        annotations: annotations,
        currentAnnotation: nil,
        canvasSize: CGSize(width: 800, height: 600),
        scale: 1.0,
        selectedIndex: 0 // Show first annotation selected for preview
    )
    .frame(width: 800, height: 600)
    .background(Color.gray.opacity(0.2))
}
#endif
