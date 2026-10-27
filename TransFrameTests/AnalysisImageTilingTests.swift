import CoreGraphics
import XCTest
@testable import TransFrame

final class AnalysisImageTilingTests: XCTestCase {
    func testSmallImageDoesNotTile() {
        let image = Self.makeImage(width: 800, height: 600)
        let tiles = AnalysisImageTiler.tiles(for: image)
        XCTAssertEqual(tiles.count, 1)
        XCTAssertTrue(tiles[0].image === image)
        XCTAssertEqual(tiles[0].pixelRect, CGRect(x: 0, y: 0, width: 800, height: 600))
    }

    func testLongImageTilesKeepSourcePixelsAndCoverWholeImage() {
        let image = Self.makeImage(width: 120, height: 500)
        let tiles = AnalysisImageTiler.tiles(for: image, maxDimension: 100, overlap: 10)
        XCTAssertEqual(tiles.map(\.pixelRect), [
            CGRect(x: 0, y: 0, width: 120, height: 100),
            CGRect(x: 0, y: 90, width: 120, height: 100),
            CGRect(x: 0, y: 180, width: 120, height: 100),
            CGRect(x: 0, y: 270, width: 120, height: 100),
            CGRect(x: 0, y: 360, width: 120, height: 100),
            CGRect(x: 0, y: 400, width: 120, height: 100)
        ])
        XCTAssertEqual(tiles.map(\.image.width), Array(repeating: 120, count: 6))
    }

    func testWideAndTallImagesTileBothAxes() {
        let image = Self.makeImage(width: 250, height: 250)
        let tiles = AnalysisImageTiler.tiles(for: image, maxDimension: 100, overlap: 20)
        XCTAssertEqual(tiles.count, 9)
        XCTAssertEqual(tiles.last?.pixelRect, CGRect(x: 150, y: 150, width: 100, height: 100))
    }

    func testNormalizedCoordinatesMapToOriginalImageAndRemoveOverlapDuplicates() throws {
        let image = Self.makeImage(width: 100, height: 250)
        let tiles = AnalysisImageTiler.tiles(for: image, maxDimension: 100, overlap: 20)
        let results = tiles.enumerated().map { index, tile in
            let segments = index < 2
                ? [TextSegment(text: "Edit", boundingBox: CGRect(x: 0.2, y: index == 0 ? 0.8 : 0.0, width: 0.5, height: 0.2), confidence: 1)]
                : []
            return ScreenAnalysisResult(
                segments: segments, imageSize: CGSize(width: tile.image.width, height: tile.image.height)
            )
        }
        let merged = try AnalysisImageTiler.merging(tiles.map(\.pixelRect), results: results)
        XCTAssertEqual(merged.imageSize, CGSize(width: 100, height: 250))
        XCTAssertEqual(merged.segments.count, 1)
        XCTAssertEqual(merged.segments[0].boundingBox.minX, 0.2, accuracy: 0.0001)
        XCTAssertEqual(merged.segments[0].boundingBox.minY, 0.32, accuracy: 0.0001)
        XCTAssertEqual(merged.segments[0].boundingBox.width, 0.5, accuracy: 0.0001)
        XCTAssertEqual(merged.segments[0].boundingBox.height, 0.08, accuracy: 0.0001)
    }

    func testIncompleteOrOutOfBoundsTileIsRejected() {
        let image = Self.makeImage(width: 100, height: 250)
        let tiles = AnalysisImageTiler.tiles(for: image, maxDimension: 100, overlap: 20)
        let valid = ScreenAnalysisResult(
            segments: [TextSegment(text: "text", boundingBox: CGRect(x: 0.2, y: 0.2, width: 0.2, height: 0.2), confidence: 1)],
            imageSize: CGSize(width: 100, height: 100)
        )
        XCTAssertThrowsError(try AnalysisImageTiler.merging([tiles[0].pixelRect], results: []))
        XCTAssertThrowsError(try AnalysisImageTiler.merging(
            [tiles[0].pixelRect], results: [ScreenAnalysisResult.empty(imageSize: .zero)]
        ))
        XCTAssertNoThrow(try AnalysisImageTiler.merging([tiles[0].pixelRect], results: [valid]))
    }

    private static func makeImage(width: Int, height: Int) -> CGImage {
        let context = CGContext(
            data: nil, width: width, height: height, bitsPerComponent: 8,
            bytesPerRow: 0, space: CGColorSpace(name: CGColorSpace.sRGB)!,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        )!
        context.setFillColor(CGColor(red: 0.2, green: 0.4, blue: 0.6, alpha: 1))
        context.fill(CGRect(x: 0, y: 0, width: width, height: height))
        return context.makeImage()!
    }
}
