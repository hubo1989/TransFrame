import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

/// Shared image encoder for OCR/VLM analysis requests.
///
/// Captures come in at full Retina backing resolution. Sending them verbatim
/// multiplies upload size and vision-encoder prefill cost on OpenAI-compatible
/// endpoints, which — unlike the official OpenAI API — do not downscale large
/// images server-side. Screen text stays readable well below the limit, and
/// providers return normalized bounding boxes, so downsampling before upload
/// is safe for coordinate mapping.
enum AnalysisImageEncoder {
    /// Longest edge (pixels) allowed in an encoded analysis image.
    static let maxDimension = 2000

    /// An encoded image ready for upload.
    struct EncodedImage: Sendable {
        let data: Data
        let mimeType: String

        /// Base64 payload for `image_url`-style requests.
        var base64String: String { data.base64EncodedString() }

        /// Inline data URI (`data:image/jpeg;base64,...`).
        var dataURI: String { "data:\(mimeType);base64,\(base64String)" }
    }

    /// Encodes the image as JPEG, downsampling first when its longest edge
    /// exceeds `maxDimension`.
    static func jpeg(_ image: CGImage, quality: CGFloat = 0.85) throws -> EncodedImage {
        let source = downsampled(image)
        guard let data = source.jpegData(quality: quality), !data.isEmpty else {
            throw VLMProviderError.imageEncodingFailed
        }
        return EncodedImage(data: data, mimeType: "image/jpeg")
    }

    /// Encodes the image as PNG, downsampling first when its longest edge
    /// exceeds `maxDimension`.
    static func png(_ image: CGImage) throws -> EncodedImage {
        let source = downsampled(image)
        guard let data = Self.encodePNG(source), !data.isEmpty else {
            throw VLMProviderError.imageEncodingFailed
        }
        return EncodedImage(data: data, mimeType: "image/png")
    }

    /// Returns `image` unchanged when it is within limits, otherwise an
    /// aspect-preserving copy whose longest edge equals `maxDimension`.
    static func downsampled(_ image: CGImage, maxDimension limit: Int = AnalysisImageEncoder.maxDimension) -> CGImage {
        let longestEdge = max(image.width, image.height)
        guard limit > 0, longestEdge > limit else { return image }
        let scale = CGFloat(limit) / CGFloat(longestEdge)
        let size = CGSize(
            width: max(1, (CGFloat(image.width) * scale).rounded()),
            height: max(1, (CGFloat(image.height) * scale).rounded())
        )
        return image.resized(to: size) ?? image
    }

    private static func encodePNG(_ image: CGImage) -> Data? {
        guard let data = CFDataCreateMutable(nil, 0),
              let destination = CGImageDestinationCreateWithData(
                  data, UTType.png.identifier as CFString, 1, nil
              ) else {
            return nil
        }
        CGImageDestinationAddImage(destination, image, nil)
        guard CGImageDestinationFinalize(destination) else {
            return nil
        }
        return data as Data
    }
}
