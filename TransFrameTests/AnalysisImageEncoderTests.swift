import CoreGraphics
import XCTest
@testable import TransFrame

/// Tests for the shared analysis image encoder: Retina captures must be
/// downsampled before upload, small images must pass through untouched.
final class AnalysisImageEncoderTests: XCTestCase {

    func testDownsampleShrinksLongestEdgeAndKeepsAspectRatio() {
        let image = Self.makeImage(width: 4000, height: 3000)
        let downsampled = AnalysisImageEncoder.downsampled(image)

        XCTAssertEqual(downsampled.width, 2000, "longest edge must cap at maxDimension")
        XCTAssertEqual(downsampled.height, 1500, "aspect ratio must be preserved")
    }

    func testDownsampleLeavesSmallImagesUnchanged() {
        let image = Self.makeImage(width: 800, height: 600)
        let downsampled = AnalysisImageEncoder.downsampled(image, maxDimension: 2000)

        XCTAssertTrue(downsampled === image, "images within the limit must not be re-encoded")
    }

    func testDownsampleHandlesExtremeAspectRatios() {
        // 3000px wide strip capped to 2000 -> height stays >= 1
        let strip = Self.makeImage(width: 3000, height: 8)
        let downsampled = AnalysisImageEncoder.downsampled(strip)

        XCTAssertEqual(downsampled.width, 2000)
        XCTAssertGreaterThanOrEqual(downsampled.height, 1)
    }

    func testJPEGEncodingProducesDataURI() throws {
        let image = Self.makeImage(width: 640, height: 480)
        let encoded = try AnalysisImageEncoder.jpeg(image)

        XCTAssertFalse(encoded.data.isEmpty)
        XCTAssertEqual(encoded.mimeType, "image/jpeg")
        XCTAssertTrue(encoded.dataURI.hasPrefix("data:image/jpeg;base64,"))
        XCTAssertTrue(encoded.dataURI.hasSuffix(encoded.base64String))
    }

    func testPNGEncodingProducesPNGSignature() throws {
        let image = Self.makeImage(width: 640, height: 480)
        let encoded = try AnalysisImageEncoder.png(image)

        XCTAssertGreaterThan(encoded.data.count, 8)
        XCTAssertEqual(Array(encoded.data.prefix(4)), [0x89, 0x50, 0x4E, 0x47], "PNG magic bytes expected")
        XCTAssertEqual(encoded.mimeType, "image/png")
    }

    // MARK: - Utilities

    private static func makeImage(width: Int, height: Int) -> CGImage {
        let context = CGContext(
            data: nil,
            width: width,
            height: height,
            bitsPerComponent: 8,
            bytesPerRow: 0,
            space: CGColorSpace(name: CGColorSpace.sRGB)!,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        )!
        context.setFillColor(CGColor(red: 0.2, green: 0.4, blue: 0.6, alpha: 1))
        context.fill(CGRect(x: 0, y: 0, width: width, height: height))
        return context.makeImage()!
    }
}
