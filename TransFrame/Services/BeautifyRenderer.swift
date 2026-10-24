import AppKit
import CoreGraphics
import CoreImage

/// Configuration for beautifying a screenshot with padding, rounded corners, soft shadows, and backgrounds.
struct BeautifyConfig: Sendable, Equatable {
    enum BackgroundStyle: Sendable, Equatable {
        case none
        case gradient(Preset)
        case solid(CodableColor)
        case desktopWallpaper

        enum Preset: String, Sendable, CaseIterable {
            case sunset = "Sunset"
            case ocean = "Ocean"
            case lavender = "Lavender"
            case mint = "Mint"
            case slate = "Slate"
            case aurora = "Aurora"

            var colors: [NSColor] {
                switch self {
                case .sunset:
                    return [
                        NSColor(srgbRed: 0.98, green: 0.45, blue: 0.41, alpha: 1.0),
                        NSColor(srgbRed: 0.99, green: 0.73, blue: 0.51, alpha: 1.0)
                    ]
                case .ocean:
                    return [
                        NSColor(srgbRed: 0.13, green: 0.43, blue: 0.88, alpha: 1.0),
                        NSColor(srgbRed: 0.31, green: 0.78, blue: 0.94, alpha: 1.0)
                    ]
                case .lavender:
                    return [
                        NSColor(srgbRed: 0.45, green: 0.35, blue: 0.85, alpha: 1.0),
                        NSColor(srgbRed: 0.82, green: 0.62, blue: 0.96, alpha: 1.0)
                    ]
                case .mint:
                    return [
                        NSColor(srgbRed: 0.16, green: 0.72, blue: 0.58, alpha: 1.0),
                        NSColor(srgbRed: 0.62, green: 0.89, blue: 0.75, alpha: 1.0)
                    ]
                case .slate:
                    return [
                        NSColor(srgbRed: 0.18, green: 0.22, blue: 0.28, alpha: 1.0),
                        NSColor(srgbRed: 0.35, green: 0.41, blue: 0.48, alpha: 1.0)
                    ]
                case .aurora:
                    return [
                        NSColor(srgbRed: 0.05, green: 0.58, blue: 0.70, alpha: 1.0),
                        NSColor(srgbRed: 0.48, green: 0.85, blue: 0.55, alpha: 1.0)
                    ]
                }
            }
        }
    }

    var isEnabled: Bool
    var padding: CGFloat
    var cornerRadius: CGFloat
    var shadowRadius: CGFloat
    var shadowOpacity: Float
    var backgroundStyle: BackgroundStyle

    static let `default` = BeautifyConfig(
        isEnabled: false,
        padding: 36,
        cornerRadius: 12,
        shadowRadius: 24,
        shadowOpacity: 0.35,
        backgroundStyle: .gradient(.ocean)
    )

    init(
        isEnabled: Bool = false,
        padding: CGFloat = 36,
        cornerRadius: CGFloat = 12,
        shadowRadius: CGFloat = 24,
        shadowOpacity: Float = 0.35,
        backgroundStyle: BackgroundStyle = .gradient(.ocean)
    ) {
        self.isEnabled = isEnabled
        self.padding = padding
        self.cornerRadius = cornerRadius
        self.shadowRadius = shadowRadius
        self.shadowOpacity = shadowOpacity
        self.backgroundStyle = backgroundStyle
    }
}

/// Hardware-accelerated CoreGraphics renderer that applies beautification packaging to screenshots.
final class BeautifyRenderer: @unchecked Sendable {
    static let shared = BeautifyRenderer()

    private init() {}

    /// Renders the input image wrapped with background, shadow, padding, and corner radius.
    func render(image: CGImage, config: BeautifyConfig, screen: NSScreen? = nil) -> CGImage? {
        guard config.isEnabled else { return image }

        let originalWidth = CGFloat(image.width)
        let originalHeight = CGFloat(image.height)
        let padding = config.padding
        let targetWidth = originalWidth + padding * 2
        let targetHeight = originalHeight + padding * 2

        guard let colorSpace = image.colorSpace ?? CGColorSpace(name: CGColorSpace.sRGB) else { return nil }
        guard let context = CGContext(
            data: nil,
            width: Int(targetWidth),
            height: Int(targetHeight),
            bitsPerComponent: 8,
            bytesPerRow: 0,
            space: colorSpace,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ) else {
            return nil
        }

        let fullRect = CGRect(x: 0, y: 0, width: targetWidth, height: targetHeight)

        // 1. Draw Background
        drawBackground(config.backgroundStyle, in: context, rect: fullRect, screen: screen)

        // 2. Draw Rounded Image with Shadow
        let imageRect = CGRect(x: padding, y: padding, width: originalWidth, height: originalHeight)
        context.saveGState()

        // Shadow configuration
        if config.shadowOpacity > 0 && config.shadowRadius > 0 {
            let shadowColor = NSColor.black.withAlphaComponent(CGFloat(config.shadowOpacity)).cgColor
            context.setShadow(
                offset: CGSize(width: 0, height: -config.shadowRadius * 0.4),
                blur: config.shadowRadius,
                color: shadowColor
            )
        }

        // Rounded clipping path
        let path = CGPath(
            roundedRect: imageRect,
            cornerWidth: config.cornerRadius,
            cornerHeight: config.cornerRadius,
            transform: nil
        )
        context.addPath(path)
        context.clip()

        // Draw original image inside the rounded rect
        context.draw(image, in: imageRect)
        context.restoreGState()

        return context.makeImage()
    }

    private func drawBackground(_ style: BeautifyConfig.BackgroundStyle, in context: CGContext, rect: CGRect, screen: NSScreen?) {
        switch style {
        case .none:
            break

        case .solid(let color):
            context.setFillColor(color.cgColor)
            context.fill(rect)

        case .gradient(let preset):
            let colors = preset.colors.map { $0.cgColor } as CFArray
            let colorSpace = context.colorSpace ?? CGColorSpace(name: CGColorSpace.sRGB)!
            if let gradient = CGGradient(colorsSpace: colorSpace, colors: colors, locations: [0.0, 1.0]) {
                context.drawLinearGradient(
                    gradient,
                    start: CGPoint(x: rect.minX, y: rect.maxY),
                    end: CGPoint(x: rect.maxX, y: rect.minY),
                    options: []
                )
            }

        case .desktopWallpaper:
            if let wallpaperImage = fetchBlurredWallpaper(for: screen) {
                context.draw(wallpaperImage, in: rect)
            } else {
                // Fallback to default gradient
                drawBackground(.gradient(.slate), in: context, rect: rect, screen: screen)
            }
        }
    }

    private func fetchBlurredWallpaper(for screen: NSScreen?) -> CGImage? {
        let wallpaperURL: URL?
        if let screen {
            wallpaperURL = NSWorkspace.shared.desktopImageURL(for: screen)
        } else if Thread.isMainThread {
            wallpaperURL = MainActor.assumeIsolated {
                guard let mainScreen = NSScreen.main else { return nil }
                return NSWorkspace.shared.desktopImageURL(for: mainScreen)
            }
        } else {
            wallpaperURL = nil
        }

        guard let wallpaperURL,
              let nsImage = NSImage(contentsOf: wallpaperURL) else {
            return nil
        }
        var proposed = NSRect(origin: .zero, size: nsImage.size)
        guard let cg = nsImage.cgImage(forProposedRect: &proposed, context: nil, hints: nil) else {
            return nil
        }

        // Apply slight Gaussian blur
        let ciImage = CIImage(cgImage: cg)
        guard let filter = CIFilter(name: "CIGaussianBlur") else { return cg }
        filter.setValue(ciImage, forKey: kCIInputImageKey)
        filter.setValue(30.0, forKey: kCIInputRadiusKey)
        guard let output = filter.outputImage else { return cg }

        let ciContext = CIContext()
        return ciContext.createCGImage(output, from: ciImage.extent) ?? cg
    }
}
