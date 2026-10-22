import Foundation
import CoreGraphics
import os

/// Detects sensitive privacy regions (phone numbers, passwords, amounts,
/// faces, keys, emails, IDs) in an image using the configured online VLM
/// provider, returning rects in image pixel coordinates (top-left origin).
final class SmartPrivacyRedactionService: Sendable {
    static let shared = SmartPrivacyRedactionService()

    private let logger = Logger.ui

    enum PrivacyDetectionError: LocalizedError {
        case providerNotConfigured

        var errorDescription: String? {
            switch self {
            case .providerNotConfigured:
                return String(localized: "smartMosaic.error.notConfigured")
            }
        }
    }

    private init() {}

    /// Whether the current VLM provider has an API key configured.
    @MainActor
    var isConfigured: Bool {
        let settings = AppSettings.shared
        let provider = settings.vlmProvider
        if !provider.requiresAPIKey { return true }
        return !settings.vlmAPIKey(for: provider).trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    /// Detects privacy regions and returns rects in image pixel coordinates.
    /// - Parameter image: The image to analyze.
    /// - Returns: Pixel-space rects (top-left origin), possibly empty.
    func detectPrivacyRegions(in image: CGImage) async throws -> [CGRect] {
        let engine = ScreenCoderEngine.shared
        guard let provider = try? await engine.currentProvider() else {
            throw PrivacyDetectionError.providerNotConfigured
        }
        guard await provider.isAvailable else {
            throw PrivacyDetectionError.providerNotConfigured
        }

        logger.info("Smart privacy detection started via \(provider.name)")
        let result = try await provider.analyze(image: image, prompt: .privacy)

        let width = CGFloat(image.width)
        let height = CGFloat(image.height)
        let rects = result.segments
            .filter { $0.confidence >= 0.5 }
            .compactMap { segment -> CGRect? in
                let rect = CGRect(
                    x: segment.boundingBox.origin.x * width,
                    y: segment.boundingBox.origin.y * height,
                    width: segment.boundingBox.width * width,
                    height: segment.boundingBox.height * height
                )
                // Discard degenerate boxes
                guard rect.width >= 4, rect.height >= 4 else { return nil }
                return rect.integral
            }
        logger.info("Smart privacy detection found \(rects.count) regions")
        return rects
    }
}
