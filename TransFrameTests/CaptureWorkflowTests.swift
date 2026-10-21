import XCTest
import CoreGraphics
import AppKit
@testable import TransFrame

final class CaptureWorkflowTests: XCTestCase {

    /// Helper to create a solid RGB CGImage of given dimensions and color
    private func createTestImage(width: Int, height: Int, color: NSColor) -> CGImage {
        let colorSpace = CGColorSpace(name: CGColorSpace.sRGB)!
        let bitmapInfo = CGImageAlphaInfo.premultipliedLast.rawValue
        let context = CGContext(
            data: nil,
            width: width,
            height: height,
            bitsPerComponent: 8,
            bytesPerRow: width * 4,
            space: colorSpace,
            bitmapInfo: bitmapInfo
        )!
        context.setFillColor(color.cgColor)
        context.fill(CGRect(x: 0, y: 0, width: width, height: height))
        return context.makeImage()!
    }

    func testCaptureDocumentCropTransformAndEffectiveRect() {
        let baseImage = createTestImage(width: 200, height: 200, color: .white)
        let quartzRect = QuartzGlobalRect(CGRect(x: 100, y: 100, width: 200, height: 200))

        var doc = CaptureDocument(
            baseImage: baseImage,
            sourceScreenRect: quartzRect
        )

        // 1. Initial document effective crop rect is full base image
        XCTAssertEqual(doc.effectiveCropRect, CGRect(x: 0, y: 0, width: 200, height: 200))
        XCTAssertEqual(doc.editRevision, 1)

        // 2. Apply non-destructive crop
        let crop = CGRect(x: 20, y: 30, width: 100, height: 80)
        doc.cropTransform = crop
        XCTAssertEqual(doc.effectiveCropRect, crop)
    }

    func testDocumentRendererAndAnalysisInputBuilderBurnInRedaction() {
        // Create an all-white 100x100 base image
        let baseImage = createTestImage(width: 100, height: 100, color: .white)
        let quartzRect = QuartzGlobalRect(CGRect(x: 0, y: 0, width: 100, height: 100))

        // Add a solid black privacy redaction in the center (25, 25, 50, 50)
        let redaction = PrivacyRedactionAnnotation(
            rect: CGRect(x: 25, y: 25, width: 50, height: 50),
            fillColor: CodableColor(.black)
        )

        let doc = CaptureDocument(
            baseImage: baseImage,
            sourceScreenRect: quartzRect,
            annotations: [.privacyRedaction(redaction)]
        )

        // Build sanitized analysis input image
        guard let sanitizedImage = AnalysisInputBuilder.shared.buildInputImage(for: doc) else {
            XCTFail("Failed to build sanitized analysis image")
            return
        }

        XCTAssertEqual(sanitizedImage.width, 100)
        XCTAssertEqual(sanitizedImage.height, 100)

        // Read pixel data at center (50, 50)
        guard let pixelData = sanitizedImage.dataProvider?.data,
              let dataPtr = CFDataGetBytePtr(pixelData) else {
            XCTFail("Failed to inspect sanitized image pixel data")
            return
        }

        let bytesPerRow = sanitizedImage.bytesPerRow
        let centerOffset = 50 * bytesPerRow + 50 * 4

        let r = dataPtr[centerOffset]
        let g = dataPtr[centerOffset + 1]
        let b = dataPtr[centerOffset + 2]

        // Center must be burned in black (RGB near 0, 0, 0), completely overwriting initial white
        XCTAssertLessThan(r, 10, "Red channel must be black")
        XCTAssertLessThan(g, 10, "Green channel must be black")
        XCTAssertLessThan(b, 10, "Blue channel must be black")
    }

    func testScreenAnalysisResultFilterAndRecover() {
        let size = CGSize(width: 1000, height: 800)
        let normalSegment = TextSegment(
            text: "Hello World",
            boundingBox: CGRect(x: 0.2, y: 0.2, width: 0.3, height: 0.05),
            confidence: 0.95
        )
        let noiseSegment = TextSegment(
            text: "x: 0.5",
            boundingBox: CGRect(x: 0.01, y: 0.01, width: 0.05, height: 0.02),
            confidence: 0.8
        )

        let initialResult = ScreenAnalysisResult(
            segments: [normalSegment, noiseSegment],
            imageSize: size
        )

        // 1. Filter for translation
        let filteredResult = initialResult.filteredForTranslation()
        XCTAssertEqual(filteredResult.segments.count, 1)
        XCTAssertEqual(filteredResult.segments.first?.text, "Hello World")
        XCTAssertEqual(filteredResult.filteredOutSegments.count, 1)
        XCTAssertEqual(filteredResult.filteredOutSegments.first?.text, "x: 0.5")

        // 2. Recover filtered segments
        let recovered = filteredResult.recoveringFilteredSegments()
        XCTAssertEqual(recovered.segments.count, 2)
        XCTAssertTrue(recovered.filteredOutSegments.isEmpty)
    }

    func testScreenAnalysisResultMergingROI() {
        let size = CGSize(width: 1000, height: 800)
        let segmentOutside = TextSegment(
            text: "Outside Header",
            boundingBox: CGRect(x: 0.1, y: 0.1, width: 0.3, height: 0.05)
        )
        let segmentInsideOriginal = TextSegment(
            text: "Inside Old Text",
            boundingBox: CGRect(x: 0.5, y: 0.5, width: 0.2, height: 0.05)
        )
        let segmentUserEdited = TextSegment(
            text: "User Edited Manual Text",
            boundingBox: CGRect(x: 0.55, y: 0.52, width: 0.2, height: 0.05),
            isUserEdited: true
        )

        let originalResult = ScreenAnalysisResult(
            segments: [segmentOutside, segmentInsideOriginal, segmentUserEdited],
            imageSize: size
        )

        let roi = CGRect(x: 0.45, y: 0.45, width: 0.4, height: 0.4)
        let reRecognizedNew = TextSegment(
            text: "Inside Re-recognized New Text",
            boundingBox: CGRect(x: 0.5, y: 0.5, width: 0.25, height: 0.05)
        )

        let merged = originalResult.mergingROISegments([reRecognizedNew], in: roi)

        // Should keep outside, keep user edited, replace non-edited inside, and add new
        XCTAssertTrue(merged.segments.contains(where: { $0.text == "Outside Header" }))
        XCTAssertTrue(merged.segments.contains(where: { $0.text == "User Edited Manual Text" }))
        XCTAssertTrue(merged.segments.contains(where: { $0.text == "Inside Re-recognized New Text" }))
        XCTAssertFalse(merged.segments.contains(where: { $0.text == "Inside Old Text" }))
    }

    func testCalloutBubbleAndRedactionHitTest() {
        let bubble = CalloutBubbleAnnotation(
            rect: CGRect(x: 100, y: 100, width: 120, height: 60),
            targetPoint: CGPoint(x: 50, y: 50),
            text: "Note"
        )
        let bubbleAnn = Annotation.calloutBubble(bubble)

        // Inside bubble rect
        XCTAssertTrue(bubbleAnn.hitTest(point: CGPoint(x: 120, y: 120), tolerance: 5))
        // Target point
        XCTAssertTrue(bubbleAnn.hitTest(point: CGPoint(x: 50, y: 50), tolerance: 5))
        // Far away
        XCTAssertFalse(bubbleAnn.hitTest(point: CGPoint(x: 300, y: 300), tolerance: 5))

        let redaction = PrivacyRedactionAnnotation(
            rect: CGRect(x: 10, y: 10, width: 50, height: 50)
        )
        let redactionAnn = Annotation.privacyRedaction(redaction)

        // Inside solid redaction
        XCTAssertTrue(redactionAnn.hitTest(point: CGPoint(x: 25, y: 25), tolerance: 2))
        // Outside
        XCTAssertFalse(redactionAnn.hitTest(point: CGPoint(x: 100, y: 100), tolerance: 2))
    }
}
