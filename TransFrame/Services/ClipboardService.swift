import Foundation
import AppKit
import CoreGraphics

/// Service for copying screenshots to the system clipboard.
/// Uses NSPasteboard for compatibility with all macOS applications.
@MainActor
struct ClipboardService {
    // MARK: - Public API

    /// Copies an image with annotations to the system clipboard.
    /// - Parameters:
    ///   - image: The base image to copy
    ///   - annotations: Annotations to composite onto the image
    /// - Throws: TransFrameError.clipboardWriteFailed if the operation fails
    func copy(_ image: CGImage, annotations: [Annotation]) throws {
        // Composite annotations if any exist
        let finalImage: CGImage
        if annotations.isEmpty {
            finalImage = image
        } else {
            finalImage = try compositeAnnotations(annotations, onto: image)
        }

        // Convert to NSImage
        let nsImage = NSImage(
            cgImage: finalImage,
            size: NSSize(width: finalImage.width, height: finalImage.height)
        )

        // Write to pasteboard
        let pasteboard = NSPasteboard.general
        pasteboard.clearContents()

        // Write both PNG and TIFF for maximum compatibility
        guard pasteboard.writeObjects([nsImage]) else {
            throw TransFrameError.clipboardWriteFailed
        }
    }

    /// Copies an image (without annotations) to the system clipboard.
    /// - Parameter image: The image to copy
    /// - Throws: TransFrameError.clipboardWriteFailed if the operation fails
    func copy(_ image: CGImage) throws {
        try copy(image, annotations: [])
    }

    /// Checks if the clipboard currently contains an image.
    var hasImage: Bool {
        let pasteboard = NSPasteboard.general
        return pasteboard.canReadItem(withDataConformingToTypes: [
            NSPasteboard.PasteboardType.tiff.rawValue,
            NSPasteboard.PasteboardType.png.rawValue
        ])
    }

    /// Copies text to the system clipboard.
    /// - Parameter text: The text to copy
    /// - Throws: TransFrameError.clipboardWriteFailed if the operation fails
    func copyText(_ text: String) throws {
        let pasteboard = NSPasteboard.general
        pasteboard.clearContents()

        guard pasteboard.setString(text, forType: .string) else {
            throw TransFrameError.clipboardWriteFailed
        }
    }

    // MARK: - Annotation Compositing

    /// Composites annotations onto an image.
    /// - Parameters:
    ///   - annotations: The annotations to draw
    ///   - image: The base image
    /// - Returns: A new CGImage with annotations rendered
    /// - Throws: TransFrameError if compositing fails
    private func compositeAnnotations(
        _ annotations: [Annotation],
        onto image: CGImage
    ) throws -> CGImage {
        let width = image.width
        let height = image.height

        // Create drawing context
        guard let colorSpace = image.colorSpace ?? CGColorSpace(name: CGColorSpace.sRGB),
              let context = CGContext(
                  data: nil,
                  width: width,
                  height: height,
                  bitsPerComponent: 8,
                  bytesPerRow: 0,
                  space: colorSpace,
                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
              ) else {
            throw TransFrameError.clipboardWriteFailed
        }

        // Draw base image
        context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))

        // Configure for drawing annotations
        context.setLineCap(.round)
        context.setLineJoin(.round)

        // Draw each annotation
        for annotation in annotations {
            renderAnnotation(annotation, in: context, imageHeight: CGFloat(height))
        }

        // Create final image
        guard let result = context.makeImage() else {
            throw TransFrameError.clipboardWriteFailed
        }

        return result
    }

    /// Renders a single annotation into a graphics context.
    private func renderAnnotation(
        _ annotation: Annotation,
        in context: CGContext,
        imageHeight: CGFloat
    ) {
        switch annotation {
        case .rectangle(let rect):
            renderRectangle(rect, in: context, imageHeight: imageHeight)
        case .ellipse(let ellipse):
            renderEllipse(ellipse, in: context, imageHeight: imageHeight)
        case .line(let line):
            renderLine(line, in: context, imageHeight: imageHeight)
        case .freehand(let freehand):
            renderFreehand(freehand, in: context, imageHeight: imageHeight)
        case .arrow(let arrow):
            renderArrow(arrow, in: context, imageHeight: imageHeight)
        case .highlight(let highlight):
            renderHighlight(highlight, in: context, imageHeight: imageHeight)
        case .mosaic(let mosaic):
            renderMosaic(mosaic, in: context, imageHeight: imageHeight)
        case .text(let text):
            renderText(text, in: context, imageHeight: imageHeight)
        case .numberLabel(let label):
            renderNumberLabel(label, in: context, imageHeight: imageHeight)
        case .privacyRedaction(let redaction):
            renderPrivacyRedaction(redaction, in: context, imageHeight: imageHeight)
        case .calloutBubble(let bubble):
            renderCalloutBubble(bubble, in: context, imageHeight: imageHeight)
        }
    }

    /// Renders a solid privacy redaction annotation.
    private func renderPrivacyRedaction(
        _ annotation: PrivacyRedactionAnnotation,
        in context: CGContext,
        imageHeight: CGFloat
    ) {
        let rect = CGRect(
            x: annotation.rect.origin.x,
            y: imageHeight - annotation.rect.origin.y - annotation.rect.height,
            width: annotation.rect.width,
            height: annotation.rect.height
        )
        context.saveGState()
        context.setFillColor(annotation.fillColor.nsColor.cgColor)
        context.fill(rect)
        context.restoreGState()
    }

    /// Renders a callout bubble annotation.
    private func renderCalloutBubble(
        _ annotation: CalloutBubbleAnnotation,
        in context: CGContext,
        imageHeight: CGFloat
    ) {
        let rect = CGRect(
            x: annotation.rect.origin.x,
            y: imageHeight - annotation.rect.origin.y - annotation.rect.height,
            width: annotation.rect.width,
            height: annotation.rect.height
        )
        context.saveGState()
        context.setFillColor(annotation.bubbleColor.nsColor.cgColor)
        let path = CGPath(roundedRect: rect, cornerWidth: 8, cornerHeight: 8, transform: nil)
        context.addPath(path)
        context.fillPath()
        context.restoreGState()
    }

    /// Renders a rectangle annotation.
    private func renderRectangle(
        _ annotation: RectangleAnnotation,
        in context: CGContext,
        imageHeight: CGFloat
    ) {
        let rect = CGRect(
            x: annotation.rect.origin.x,
            y: imageHeight - annotation.rect.origin.y - annotation.rect.height,
            width: annotation.rect.width,
            height: annotation.rect.height
        )

        if annotation.isFilled {
            // Filled rectangle - solid color to hide underlying content
            context.setFillColor(annotation.style.color.cgColor)
            context.fill(rect)
        } else {
            // Hollow rectangle - outline only
            context.setStrokeColor(annotation.style.color.cgColor)
            context.setLineWidth(annotation.style.lineWidth)
            context.stroke(rect)
        }
    }

    /// Renders a freehand annotation.
    private func renderFreehand(
        _ annotation: FreehandAnnotation,
        in context: CGContext,
        imageHeight: CGFloat
    ) {
        guard annotation.points.count >= 2 else { return }

        context.setStrokeColor(annotation.style.color.cgColor)
        context.setLineWidth(annotation.style.lineWidth)

        context.beginPath()
        let firstPoint = annotation.points[0]
        context.move(to: CGPoint(x: firstPoint.x, y: imageHeight - firstPoint.y))

        for point in annotation.points.dropFirst() {
            context.addLine(to: CGPoint(x: point.x, y: imageHeight - point.y))
        }

        context.strokePath()
    }

    /// Renders an arrow annotation.
    private func renderArrow(
        _ annotation: ArrowAnnotation,
        in context: CGContext,
        imageHeight: CGFloat
    ) {
        let start = CGPoint(x: annotation.startPoint.x, y: imageHeight - annotation.startPoint.y)
        let end = CGPoint(x: annotation.endPoint.x, y: imageHeight - annotation.endPoint.y)
        let lineWidth = annotation.style.lineWidth

        context.setStrokeColor(annotation.style.color.cgColor)
        context.setFillColor(annotation.style.color.cgColor)
        context.setLineWidth(lineWidth)
        context.setLineCap(.round)
        context.setLineJoin(.round)

        // Draw the main line
        context.beginPath()
        context.move(to: start)
        context.addLine(to: end)
        context.strokePath()

        // Draw the arrowhead
        let arrowHeadLength = max(lineWidth * 4, 12)
        let arrowHeadAngle: CGFloat = .pi / 6

        let dx = end.x - start.x
        let dy = end.y - start.y
        let angle = atan2(dy, dx)

        let arrowPoint1 = CGPoint(
            x: end.x - arrowHeadLength * cos(angle - arrowHeadAngle),
            y: end.y - arrowHeadLength * sin(angle - arrowHeadAngle)
        )
        let arrowPoint2 = CGPoint(
            x: end.x - arrowHeadLength * cos(angle + arrowHeadAngle),
            y: end.y - arrowHeadLength * sin(angle + arrowHeadAngle)
        )

        context.beginPath()
        context.move(to: end)
        context.addLine(to: arrowPoint1)
        context.addLine(to: arrowPoint2)
        context.closePath()
        context.fillPath()
    }

    /// Renders a text annotation.
    private func renderText(
        _ annotation: TextAnnotation,
        in context: CGContext,
        imageHeight: CGFloat
    ) {
        guard !annotation.content.isEmpty else { return }

        let font = NSFont(name: annotation.style.fontName, size: annotation.style.fontSize)
            ?? NSFont.systemFont(ofSize: annotation.style.fontSize)

        let attributes: [NSAttributedString.Key: Any] = [
            .font: font,
            .foregroundColor: annotation.style.color.nsColor
        ]

        let attributedString = NSAttributedString(string: annotation.content, attributes: attributes)
        let position = CGPoint(
            x: annotation.position.x,
            y: imageHeight - annotation.position.y - annotation.style.fontSize
        )

        context.saveGState()
        let line = CTLineCreateWithAttributedString(attributedString)
        context.textPosition = position
        CTLineDraw(line, context)
        context.restoreGState()
    }

    /// Renders an ellipse annotation.
    private func renderEllipse(
        _ annotation: EllipseAnnotation,
        in context: CGContext,
        imageHeight: CGFloat
    ) {
        let rect = CGRect(
            x: annotation.rect.origin.x,
            y: imageHeight - annotation.rect.origin.y - annotation.rect.height,
            width: annotation.rect.width,
            height: annotation.rect.height
        )

        if annotation.isFilled {
            context.setFillColor(annotation.style.color.cgColor)
            context.fillEllipse(in: rect)
        } else {
            context.setStrokeColor(annotation.style.color.cgColor)
            context.setLineWidth(annotation.style.lineWidth)
            context.strokeEllipse(in: rect)
        }
    }

    /// Renders a line annotation.
    private func renderLine(
        _ annotation: LineAnnotation,
        in context: CGContext,
        imageHeight: CGFloat
    ) {
        let start = CGPoint(x: annotation.startPoint.x, y: imageHeight - annotation.startPoint.y)
        let end = CGPoint(x: annotation.endPoint.x, y: imageHeight - annotation.endPoint.y)

        context.setStrokeColor(annotation.style.color.cgColor)
        context.setLineWidth(annotation.style.lineWidth)
        context.setLineCap(.round)

        context.beginPath()
        context.move(to: start)
        context.addLine(to: end)
        context.strokePath()
    }

    /// Renders a highlight annotation.
    private func renderHighlight(
        _ annotation: HighlightAnnotation,
        in context: CGContext,
        imageHeight: CGFloat
    ) {
        let rect = CGRect(
            x: annotation.rect.origin.x,
            y: imageHeight - annotation.rect.origin.y - annotation.rect.height,
            width: annotation.rect.width,
            height: annotation.rect.height
        )

        let color = annotation.color.cgColor
        let alphaColor = CGColor(
            red: color.components?[0] ?? 1,
            green: color.components?[1] ?? 1,
            blue: color.components?[2] ?? 0,
            alpha: annotation.opacity
        )
        context.setFillColor(alphaColor)
        context.fill(rect)
    }

    /// Renders a mosaic annotation.
    private func renderMosaic(
        _ annotation: MosaicAnnotation,
        in context: CGContext,
        imageHeight: CGFloat
    ) {
        let rect = CGRect(
            x: annotation.rect.origin.x,
            y: imageHeight - annotation.rect.origin.y - annotation.rect.height,
            width: annotation.rect.width,
            height: annotation.rect.height
        )
        let blockSize = CGFloat(annotation.blockSize)

        for y in stride(from: rect.minY, to: rect.maxY, by: blockSize) {
            for x in stride(from: rect.minX, to: rect.maxX, by: blockSize) {
                let blockRect = CGRect(
                    x: x,
                    y: y,
                    width: min(blockSize, rect.maxX - x),
                    height: min(blockSize, rect.maxY - y)
                )
                let gray: CGFloat = ((Int(x / blockSize) + Int(y / blockSize)) % 2 == 0) ? 0.5 : 0.55
                context.setFillColor(CGColor(gray: gray, alpha: 1.0))
                context.fill(blockRect)
            }
        }
    }

    /// Renders a number label annotation.
    private func renderNumberLabel(
        _ annotation: NumberLabelAnnotation,
        in context: CGContext,
        imageHeight: CGFloat
    ) {
        let center = CGPoint(
            x: annotation.position.x,
            y: imageHeight - annotation.position.y
        )
        let radius = annotation.size / 2

        context.setFillColor(annotation.color.cgColor)
        context.fillEllipse(in: CGRect(
            x: center.x - radius,
            y: center.y - radius,
            width: annotation.size,
            height: annotation.size
        ))

        let font = NSFont.systemFont(ofSize: annotation.size * 0.6, weight: .bold)
        let attributes: [NSAttributedString.Key: Any] = [
            .font: font,
            .foregroundColor: NSColor.white
        ]
        let text = "\(annotation.number)"
        let attributedString = NSAttributedString(string: text, attributes: attributes)
        let textSize = attributedString.size()

        context.saveGState()
        let line = CTLineCreateWithAttributedString(attributedString)
        context.textPosition = CGPoint(
            x: center.x - textSize.width / 2,
            y: center.y - textSize.height / 2
        )
        CTLineDraw(line, context)
        context.restoreGState()
    }
}

// MARK: - Shared Instance

extension ClipboardService {
    /// Shared instance for convenience
    @MainActor static let shared = ClipboardService()
}
