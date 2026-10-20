import Foundation
import CoreGraphics
import AppKit
import CoreImage

// MARK: - Annotation Rendering

extension ImageExporter {
    // MARK: - Shared CIContext for performance
    
    private static let sharedCIContext = CIContext()
    
    // MARK: - Annotation Rendering Methods
    /// Renders a single annotation into a graphics context.
    /// - Parameters:
    ///   - annotation: The annotation to render
    ///   - context: The graphics context
    ///   - imageHeight: The image height (for coordinate transformation)
    func renderAnnotation(
        _ annotation: Annotation,
        in context: CGContext,
        imageHeight: CGFloat,
        sourceImage: CGImage? = nil
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
            renderMosaic(mosaic, in: context, imageHeight: imageHeight, sourceImage: sourceImage)
        case .text(let text):
            renderText(text, in: context, imageHeight: imageHeight)
        case .numberLabel(let numberLabel):
            renderNumberLabel(numberLabel, in: context, imageHeight: imageHeight)
        }
    }

    /// Renders a rectangle annotation.
    func renderRectangle(
        _ annotation: RectangleAnnotation,
        in context: CGContext,
        imageHeight: CGFloat
    ) {
        // Transform from SwiftUI coordinates (origin top-left) to CG coordinates (origin bottom-left)
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
    func renderFreehand(
        _ annotation: FreehandAnnotation,
        in context: CGContext,
        imageHeight: CGFloat
    ) {
        guard annotation.points.count >= 2 else { return }

        context.setStrokeColor(annotation.style.color.cgColor)
        context.setLineWidth(annotation.style.lineWidth)

        // Transform points and draw path
        context.beginPath()
        let firstPoint = annotation.points[0]
        context.move(to: CGPoint(x: firstPoint.x, y: imageHeight - firstPoint.y))

        for point in annotation.points.dropFirst() {
            context.addLine(to: CGPoint(x: point.x, y: imageHeight - point.y))
        }

        context.strokePath()
    }

    /// Renders an arrow annotation.
    func renderArrow(
        _ annotation: ArrowAnnotation,
        in context: CGContext,
        imageHeight: CGFloat
    ) {
        // Transform from SwiftUI coordinates (origin top-left) to CG coordinates (origin bottom-left)
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
    func renderText(
        _ annotation: TextAnnotation,
        in context: CGContext,
        imageHeight: CGFloat
    ) {
        guard !annotation.content.isEmpty else { return }

        // Create attributed string
        let font = NSFont(name: annotation.style.fontName, size: annotation.style.fontSize)
            ?? NSFont.systemFont(ofSize: annotation.style.fontSize)

        let attributes: [NSAttributedString.Key: Any] = [
            .font: font,
            .foregroundColor: annotation.style.color.nsColor
        ]

        let attributedString = NSAttributedString(string: annotation.content, attributes: attributes)

        // Draw text at position (transform Y coordinate)
        let position = CGPoint(
            x: annotation.position.x,
            y: imageHeight - annotation.position.y - annotation.style.fontSize
        )

        // Save context state
        context.saveGState()

        // Create line and draw
        let line = CTLineCreateWithAttributedString(attributedString)
        context.textPosition = position
        CTLineDraw(line, context)

        // Restore context state
        context.restoreGState()
    }

    /// Renders an ellipse annotation.
    func renderEllipse(
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
    func renderLine(
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
    func renderHighlight(
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
        
        // Convert color to sRGB color space to safely extract RGB components
        let color = annotation.color.cgColor
        let srgbColor: CGColor
        if let srgbColorSpace = CGColorSpace(name: CGColorSpace.sRGB),
           let converted = color.converted(to: srgbColorSpace, intent: .defaultIntent, options: nil) {
            srgbColor = converted
        } else {
            // Fallback to original color if sRGB conversion fails
            srgbColor = color
        }
        
        // Safely extract RGB components with fallbacks
        let components = srgbColor.components ?? [1, 1, 1, 1]
        let red = components.count > 0 ? components[0] : 1
        let green = components.count > 1 ? components[1] : 1
        let blue = components.count > 2 ? components[2] : 0
        
        let alphaColor = CGColor(
            red: red,
            green: green,
            blue: blue,
            alpha: annotation.opacity
        )
        context.setFillColor(alphaColor)
        context.fill(rect)
    }

    /// Renders a mosaic annotation.
    func renderMosaic(
        _ annotation: MosaicAnnotation,
        in context: CGContext,
        imageHeight: CGFloat,
        sourceImage: CGImage? = nil
    ) {
        let rect = CGRect(
            x: annotation.rect.origin.x,
            y: imageHeight - annotation.rect.origin.y - annotation.rect.height,
            width: annotation.rect.width,
            height: annotation.rect.height
        )
        
        // Try to use real pixelation if source image is available
        if let cgImage = sourceImage ?? context.makeImage() {
            let imageSize = CGFloat(cgImage.height)
            // Convert rect to image coordinates (origin at bottom-left for Core Image)
            let imageRect = CGRect(
                x: annotation.rect.origin.x,
                y: imageSize - annotation.rect.origin.y - annotation.rect.height,
                width: annotation.rect.width,
                height: annotation.rect.height
            )
            
            // Create pixelated version
            if let pixelatedCGImage = createPixelatedImage(
                from: cgImage,
                rect: imageRect,
                blockSize: CGFloat(annotation.blockSize)
            ) {
                // Draw the pixelated image
                context.draw(pixelatedCGImage, in: rect)
                return
            }
        }
        
        // Fallback: draw mosaic blocks (same as preview fallback)
        let blockSize = CGFloat(annotation.blockSize)
        for y in stride(from: rect.minY, to: rect.maxY, by: blockSize) {
            for x in stride(from: rect.minX, to: rect.maxX, by: blockSize) {
                let blockRect = CGRect(
                    x: x,
                    y: y,
                    width: min(blockSize, rect.maxX - x),
                    height: min(blockSize, rect.maxY - y)
                )
                // Use alternating gray for mosaic effect
                let gray: CGFloat = ((Int(x / blockSize) + Int(y / blockSize)) % 2 == 0) ? 0.5 : 0.55
                context.setFillColor(CGColor(gray: gray, alpha: 1.0))
                context.fill(blockRect)
            }
        }
    }
    
    /// Creates a pixelated CGImage from the source image in the specified rect
    private func createPixelatedImage(
        from cgImage: CGImage,
        rect: CGRect,
        blockSize: CGFloat
    ) -> CGImage? {
        let ciImage = CIImage(cgImage: cgImage)
        
        // Apply pixelation using CIPixellate filter
        guard let pixellateFilter = CIFilter(name: "CIPixellate") else {
            return nil
        }
        
        pixellateFilter.setValue(ciImage, forKey: kCIInputImageKey)
        pixellateFilter.setValue(blockSize, forKey: kCIInputScaleKey)
        
        guard let pixelatedCI = pixellateFilter.outputImage else {
            return nil
        }
        
        // Crop to the rect we want to pixelate
        let croppedCI = pixelatedCI.cropped(to: rect)
        
        // Create CGImage from CIImage
        return Self.sharedCIContext.createCGImage(croppedCI, from: croppedCI.extent)
    }

    /// Renders a number label annotation.
    func renderNumberLabel(
        _ annotation: NumberLabelAnnotation,
        in context: CGContext,
        imageHeight: CGFloat
    ) {
        let center = CGPoint(
            x: annotation.position.x,
            y: imageHeight - annotation.position.y
        )
        let radius = annotation.size / 2

        // Draw filled circle
        context.setFillColor(annotation.color.cgColor)
        context.fillEllipse(in: CGRect(
            x: center.x - radius,
            y: center.y - radius,
            width: annotation.size,
            height: annotation.size
        ))

        // Draw number text
        let font = NSFont.systemFont(ofSize: annotation.size * 0.6, weight: .bold)
        let textColor: NSColor = .white
        let attributes: [NSAttributedString.Key: Any] = [
            .font: font,
            .foregroundColor: textColor
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
