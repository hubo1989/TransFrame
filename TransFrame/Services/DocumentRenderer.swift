import Foundation
import CoreGraphics
import AppKit
import CoreImage

/// Unified document and annotation renderer.
/// Ensures visual consistency across Export, Pinned Windows, and Canvas.
final class DocumentRenderer: @unchecked Sendable {
    static let shared = DocumentRenderer()
    private let ciContext = CIContext()

    private init() {}

    /// Renders a full CaptureDocument into an immutable CGImage.
    func render(document: CaptureDocument) -> CGImage? {
        let base = document.baseImage
        let originalWidth = CGFloat(base.width)
        let originalHeight = CGFloat(base.height)

        let cropRect = document.effectiveCropRect
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

        // Draw cropped base image
        context.saveGState()
        // In CGContext bottom-left origin:
        let drawX = -cropRect.origin.x
        let drawY = -(originalHeight - (cropRect.origin.y + cropRect.height))
        context.draw(base, in: CGRect(x: drawX, y: drawY, width: originalWidth, height: originalHeight))
        context.restoreGState()

        // Render annotations transformed by crop origin
        let shiftTransform = CGAffineTransform(translationX: -cropRect.origin.x, y: -cropRect.origin.y)
        for annotation in document.annotations {
            renderAnnotation(
                annotation,
                in: context,
                targetHeight: CGFloat(targetHeight),
                offsetTransform: shiftTransform,
                sourceImage: base
            )
        }

        return context.makeImage()
    }

    /// Renders an annotation into a CGContext.
    func renderAnnotation(
        _ annotation: Annotation,
        in context: CGContext,
        targetHeight: CGFloat,
        offsetTransform: CGAffineTransform = .identity,
        sourceImage: CGImage? = nil
    ) {
        switch annotation {
        case .rectangle(let rect):
            renderRectangle(rect, in: context, targetHeight: targetHeight, transform: offsetTransform)
        case .ellipse(let ellipse):
            renderEllipse(ellipse, in: context, targetHeight: targetHeight, transform: offsetTransform)
        case .line(let line):
            renderLine(line, in: context, targetHeight: targetHeight, transform: offsetTransform)
        case .freehand(let freehand):
            renderFreehand(freehand, in: context, targetHeight: targetHeight, transform: offsetTransform)
        case .arrow(let arrow):
            renderArrow(arrow, in: context, targetHeight: targetHeight, transform: offsetTransform)
        case .highlight(let highlight):
            renderHighlight(highlight, in: context, targetHeight: targetHeight, transform: offsetTransform)
        case .mosaic(let mosaic):
            renderMosaic(mosaic, in: context, targetHeight: targetHeight, transform: offsetTransform, sourceImage: sourceImage)
        case .text(let text):
            renderText(text, in: context, targetHeight: targetHeight, transform: offsetTransform)
        case .numberLabel(let numberLabel):
            renderNumberLabel(numberLabel, in: context, targetHeight: targetHeight, transform: offsetTransform)
        case .privacyRedaction(let redaction):
            renderPrivacyRedaction(redaction, in: context, targetHeight: targetHeight, transform: offsetTransform)
        case .calloutBubble(let bubble):
            renderCalloutBubble(bubble, in: context, targetHeight: targetHeight, transform: offsetTransform)
        }
    }

    // MARK: - Specific Renderers

    private func renderPrivacyRedaction(
        _ annotation: PrivacyRedactionAnnotation,
        in context: CGContext,
        targetHeight: CGFloat,
        transform: CGAffineTransform
    ) {
        let transformedRect = annotation.rect.applying(transform)
        let cgRect = CGRect(
            x: transformedRect.origin.x,
            y: targetHeight - transformedRect.origin.y - transformedRect.height,
            width: transformedRect.width,
            height: transformedRect.height
        )

        context.saveGState()
        context.setFillColor(annotation.fillColor.cgColor)
        context.fill(cgRect)
        context.restoreGState()
    }

    private func renderCalloutBubble(
        _ annotation: CalloutBubbleAnnotation,
        in context: CGContext,
        targetHeight: CGFloat,
        transform: CGAffineTransform
    ) {
        let bubbleRect = annotation.rect.applying(transform)
        let targetPoint = annotation.targetPoint.applying(transform)

        let cgBubbleRect = CGRect(
            x: bubbleRect.origin.x,
            y: targetHeight - bubbleRect.origin.y - bubbleRect.height,
            width: bubbleRect.width,
            height: bubbleRect.height
        )
        let cgTarget = CGPoint(
            x: targetPoint.x,
            y: targetHeight - targetPoint.y
        )

        context.saveGState()

        // 1. Draw pointer tail from bubble to target point
        let tailPath = CGMutablePath()
        tailPath.move(to: cgTarget)
        // Find attachment point
        let attachPoint = CGPoint(
            x: max(cgBubbleRect.minX + 8, min(cgBubbleRect.maxX - 8, cgTarget.x)),
            y: cgTarget.y < cgBubbleRect.minY ? cgBubbleRect.minY : cgBubbleRect.maxY
        )
        tailPath.addLine(to: attachPoint)
        context.setStrokeColor(annotation.bubbleColor.cgColor)
        context.setLineWidth(2.0)
        context.addPath(tailPath)
        context.strokePath()

        // 2. Draw bubble body
        context.setFillColor(annotation.bubbleColor.cgColor)
        let path = CGPath(roundedRect: cgBubbleRect, cornerWidth: 8, cornerHeight: 8, transform: nil)
        context.addPath(path)
        context.fillPath()

        // 3. Draw text inside bubble
        let fontSize = max(10, annotation.style.fontSize)
        let font = NSFont.systemFont(ofSize: fontSize, weight: .medium)
        let attributes: [NSAttributedString.Key: Any] = [
            .font: font,
            .foregroundColor: NSColor(cgColor: annotation.style.color.cgColor) ?? .black
        ]
        let attributedString = NSAttributedString(string: annotation.text, attributes: attributes)
        let textPadding: CGFloat = 6.0
        let textRect = cgBubbleRect.insetBy(dx: textPadding, dy: textPadding)

        let framesetter = CTFramesetterCreateWithAttributedString(attributedString)
        let textPath = CGPath(rect: textRect, transform: nil)
        let frame = CTFramesetterCreateFrame(framesetter, CFRangeMake(0, attributedString.length), textPath, nil)
        CTFrameDraw(frame, context)

        context.restoreGState()
    }

    private func renderRectangle(
        _ annotation: RectangleAnnotation,
        in context: CGContext,
        targetHeight: CGFloat,
        transform: CGAffineTransform
    ) {
        let transformedRect = annotation.rect.applying(transform)
        let rect = CGRect(
            x: transformedRect.origin.x,
            y: targetHeight - transformedRect.origin.y - transformedRect.height,
            width: transformedRect.width,
            height: transformedRect.height
        )

        context.saveGState()
        if annotation.isFilled {
            context.setFillColor(annotation.style.color.cgColor)
            context.fill(rect)
        } else {
            context.setStrokeColor(annotation.style.color.cgColor)
            context.setLineWidth(annotation.style.lineWidth)
            context.stroke(rect)
        }
        context.restoreGState()
    }

    private func renderEllipse(
        _ annotation: EllipseAnnotation,
        in context: CGContext,
        targetHeight: CGFloat,
        transform: CGAffineTransform
    ) {
        let transformedRect = annotation.rect.applying(transform)
        let rect = CGRect(
            x: transformedRect.origin.x,
            y: targetHeight - transformedRect.origin.y - transformedRect.height,
            width: transformedRect.width,
            height: transformedRect.height
        )

        context.saveGState()
        if annotation.isFilled {
            context.setFillColor(annotation.style.color.cgColor)
            context.fillEllipse(in: rect)
        } else {
            context.setStrokeColor(annotation.style.color.cgColor)
            context.setLineWidth(annotation.style.lineWidth)
            context.strokeEllipse(in: rect)
        }
        context.restoreGState()
    }

    private func renderLine(
        _ annotation: LineAnnotation,
        in context: CGContext,
        targetHeight: CGFloat,
        transform: CGAffineTransform
    ) {
        let start = annotation.startPoint.applying(transform)
        let end = annotation.endPoint.applying(transform)

        context.saveGState()
        context.setStrokeColor(annotation.style.color.cgColor)
        context.setLineWidth(annotation.style.lineWidth)
        context.setLineCap(.round)

        context.beginPath()
        context.move(to: CGPoint(x: start.x, y: targetHeight - start.y))
        context.addLine(to: CGPoint(x: end.x, y: targetHeight - end.y))
        context.strokePath()
        context.restoreGState()
    }

    private func renderFreehand(
        _ annotation: FreehandAnnotation,
        in context: CGContext,
        targetHeight: CGFloat,
        transform: CGAffineTransform
    ) {
        let points = annotation.points.map { $0.applying(transform) }
        guard points.count >= 2 else { return }

        context.saveGState()
        context.setStrokeColor(annotation.style.color.cgColor)
        context.setLineWidth(annotation.style.lineWidth)
        context.setLineCap(.round)
        context.setLineJoin(.round)

        context.beginPath()
        let first = points[0]
        context.move(to: CGPoint(x: first.x, y: targetHeight - first.y))

        if points.count == 2 {
            let p1 = points[1]
            context.addLine(to: CGPoint(x: p1.x, y: targetHeight - p1.y))
        } else {
            // Quadratic Bezier smoothing through midpoints
            for i in 1..<(points.count - 1) {
                let current = points[i]
                let next = points[i + 1]
                let midPoint = CGPoint(x: (current.x + next.x) / 2.0, y: (current.y + next.y) / 2.0)
                context.addQuadCurve(
                    to: CGPoint(x: midPoint.x, y: targetHeight - midPoint.y),
                    control: CGPoint(x: current.x, y: targetHeight - current.y)
                )
            }
            if let last = points.last {
                context.addLine(to: CGPoint(x: last.x, y: targetHeight - last.y))
            }
        }

        context.strokePath()
        context.restoreGState()
    }

    private func renderArrow(
        _ annotation: ArrowAnnotation,
        in context: CGContext,
        targetHeight: CGFloat,
        transform: CGAffineTransform
    ) {
        let start = annotation.startPoint.applying(transform)
        let end = annotation.endPoint.applying(transform)

        let cgStart = CGPoint(x: start.x, y: targetHeight - start.y)
        let cgEnd = CGPoint(x: end.x, y: targetHeight - end.y)

        context.saveGState()
        context.setStrokeColor(annotation.style.color.cgColor)
        context.setFillColor(annotation.style.color.cgColor)
        context.setLineWidth(annotation.style.lineWidth)
        context.setLineCap(.round)

        // Draw shaft
        context.beginPath()
        context.move(to: cgStart)
        context.addLine(to: cgEnd)
        context.strokePath()

        // Draw arrowhead
        let dx = cgEnd.x - cgStart.x
        let dy = cgEnd.y - cgStart.y
        let angle = atan2(dy, dx)
        let arrowLength = max(annotation.style.lineWidth * 4, 14.0)
        let arrowAngle = CGFloat.pi / 6

        let p1 = CGPoint(
            x: cgEnd.x - arrowLength * cos(angle - arrowAngle),
            y: cgEnd.y - arrowLength * sin(angle - arrowAngle)
        )
        let p2 = CGPoint(
            x: cgEnd.x - arrowLength * cos(angle + arrowAngle),
            y: cgEnd.y - arrowLength * sin(angle + arrowAngle)
        )

        context.beginPath()
        context.move(to: cgEnd)
        context.addLine(to: p1)
        context.addLine(to: p2)
        context.closePath()
        context.fillPath()

        context.restoreGState()
    }

    private func renderHighlight(
        _ annotation: HighlightAnnotation,
        in context: CGContext,
        targetHeight: CGFloat,
        transform: CGAffineTransform
    ) {
        let transformedRect = annotation.rect.applying(transform)
        let rect = CGRect(
            x: transformedRect.origin.x,
            y: targetHeight - transformedRect.origin.y - transformedRect.height,
            width: transformedRect.width,
            height: transformedRect.height
        )

        context.saveGState()
        context.setFillColor(annotation.color.cgColor.copy(alpha: annotation.opacity) ?? annotation.color.cgColor)
        context.fill(rect)
        context.restoreGState()
    }

    private func renderMosaic(
        _ annotation: MosaicAnnotation,
        in context: CGContext,
        targetHeight: CGFloat,
        transform: CGAffineTransform,
        sourceImage: CGImage?
    ) {
        let transformedRect = annotation.rect.applying(transform)
        let cgRect = CGRect(
            x: transformedRect.origin.x,
            y: targetHeight - transformedRect.origin.y - transformedRect.height,
            width: transformedRect.width,
            height: transformedRect.height
        )

        guard let sourceImage else {
            context.saveGState()
            context.setFillColor(NSColor.gray.withAlphaComponent(0.5).cgColor)
            context.fill(cgRect)
            context.restoreGState()
            return
        }

        let ciImage = CIImage(cgImage: sourceImage)
        let filter = CIFilter(name: "CIPixellate")
        filter?.setValue(ciImage, forKey: kCIInputImageKey)
        filter?.setValue(max(2, annotation.blockSize), forKey: kCIInputScaleKey)

        if let output = filter?.outputImage?.cropped(to: cgRect),
           let pixelatedCG = ciContext.createCGImage(output, from: cgRect) {
            context.saveGState()
            context.draw(pixelatedCG, in: cgRect)
            context.restoreGState()
        } else {
            context.saveGState()
            context.setFillColor(NSColor.gray.withAlphaComponent(0.5).cgColor)
            context.fill(cgRect)
            context.restoreGState()
        }
    }

    private func renderText(
        _ annotation: TextAnnotation,
        in context: CGContext,
        targetHeight: CGFloat,
        transform: CGAffineTransform
    ) {
        let pos = annotation.position.applying(transform)
        let fontSize = max(10, annotation.style.fontSize)
        let font = NSFont.systemFont(ofSize: fontSize, weight: .regular)
        let attributes: [NSAttributedString.Key: Any] = [
            .font: font,
            .foregroundColor: NSColor(cgColor: annotation.style.color.cgColor) ?? .black
        ]
        let attrString = NSAttributedString(string: annotation.content, attributes: attributes)
        let line = CTLineCreateWithAttributedString(attrString)

        context.saveGState()
        context.textPosition = CGPoint(x: pos.x, y: targetHeight - pos.y - fontSize)
        CTLineDraw(line, context)
        context.restoreGState()
    }

    private func renderNumberLabel(
        _ annotation: NumberLabelAnnotation,
        in context: CGContext,
        targetHeight: CGFloat,
        transform: CGAffineTransform
    ) {
        let pos = annotation.position.applying(transform)
        let radius = annotation.size / 2.0
        let rect = CGRect(
            x: pos.x - radius,
            y: targetHeight - pos.y - radius,
            width: annotation.size,
            height: annotation.size
        )

        context.saveGState()
        context.setFillColor(annotation.color.cgColor)
        context.fillEllipse(in: rect)

        let font = NSFont.boldSystemFont(ofSize: radius * 1.1)
        let attributes: [NSAttributedString.Key: Any] = [
            .font: font,
            .foregroundColor: NSColor.white
        ]
        let attrString = NSAttributedString(string: "\(annotation.number)", attributes: attributes)
        let line = CTLineCreateWithAttributedString(attrString)
        let lineBounds = CTLineGetBoundsWithOptions(line, .useOpticalBounds)

        context.textPosition = CGPoint(
            x: rect.midX - lineBounds.midX,
            y: rect.midY - lineBounds.midY
        )
        CTLineDraw(line, context)
        context.restoreGState()
    }
}
