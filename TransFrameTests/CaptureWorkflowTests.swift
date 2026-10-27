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

    func testRetinaAnnotationScalingAndNumberLabelCircleRect() {
        let label = NumberLabelAnnotation(
            position: CGPoint(x: 50, y: 60),
            number: 1,
            size: 26,
            color: .red,
            leadPoint: CGPoint(x: 120, y: 140)
        )
        // circleRect must stay a 26x26 square centered at position even when leadPoint expands bounds
        XCTAssertEqual(label.circleRect, CGRect(x: 37, y: 47, width: 26, height: 26))
        XCTAssertGreaterThan(label.bounds.width, 26)

        let scaled = Annotation.numberLabel(label).offsetBy(dx: -10, dy: -20).scaled(by: 2.0)
        guard case .numberLabel(let scaledLabel) = scaled else {
            XCTFail("Expected scaled numberLabel")
            return
        }
        XCTAssertEqual(scaledLabel.position, CGPoint(x: 80, y: 80))
        XCTAssertEqual(scaledLabel.size, 52)
        XCTAssertEqual(scaledLabel.leadPoint, CGPoint(x: 220, y: 240))
    }

    func testAnnotationHandlesAndResize() {
        let rectAnno = Annotation.rectangle(
            RectangleAnnotation(rect: CGRect(x: 10, y: 20, width: 100, height: 80))
        )
        // Hit bottom-right handle (110 + 4, 100 + 4)
        let hitHandle = rectAnno.hitTestHandle(at: CGPoint(x: 114, y: 104), padding: 4, tolerance: 10)
        XCTAssertEqual(hitHandle, .bottomRight)

        let resized = rectAnno.applyingHandleDrag(.bottomRight, to: CGPoint(x: 160, y: 140))
        if case .rectangle(let r) = resized {
            XCTAssertEqual(r.rect, CGRect(x: 10, y: 20, width: 150, height: 120))
        } else {
            XCTFail("Expected rectangle annotation")
        }

        // Arrow curve control handle
        let arrowAnno = Annotation.arrow(
            ArrowAnnotation(startPoint: CGPoint(x: 0, y: 0), endPoint: CGPoint(x: 100, y: 0))
        )
        let curvedArrow = arrowAnno.applyingHandleDrag(.controlPoint, to: CGPoint(x: 50, y: 40))
        if case .arrow(let a) = curvedArrow {
            XCTAssertEqual(a.controlPoint, CGPoint(x: 50, y: 40))
        } else {
            XCTFail("Expected arrow annotation")
        }
    }

    @MainActor
    func testPreviewViewModelDragUndoAndNumberLabelLeadPointStability() {
        let baseImage = createTestImage(width: 200, height: 200, color: .white)
        let display = DisplayInfo(id: 1, name: "Test", frame: CGRect(x: 0, y: 0, width: 200, height: 200), scaleFactor: 2.0, isPrimary: true)
        let label = NumberLabelAnnotation(
            position: CGPoint(x: 40, y: 40),
            number: 1,
            size: 24,
            color: .red,
            leadPoint: CGPoint(x: 80, y: 80)
        )
        let screenshot = Screenshot(image: baseImage, sourceDisplay: display, annotations: [.numberLabel(label)])
        let vm = PreviewViewModel(screenshot: screenshot)

        vm.selectAnnotation(at: 0)
        vm.beginDraggingAnnotation(at: CGPoint(x: 40, y: 40))
        // Multiple drag steps should offset relative to initial position, not accumulate exponentially
        vm.continueDraggingAnnotation(to: CGPoint(x: 50, y: 50))
        vm.continueDraggingAnnotation(to: CGPoint(x: 60, y: 70))
        vm.endDraggingAnnotation()

        guard case .numberLabel(let movedLabel) = vm.annotations[0] else {
            XCTFail("Expected numberLabel")
            return
        }
        XCTAssertEqual(movedLabel.position, CGPoint(x: 60, y: 70))
        XCTAssertEqual(movedLabel.leadPoint, CGPoint(x: 100, y: 110))
        XCTAssertTrue(vm.canUndo)

        vm.undo()
        guard case .numberLabel(let restoredLabel) = vm.annotations[0] else {
            XCTFail("Expected numberLabel after undo")
            return
        }
        XCTAssertEqual(restoredLabel.position, CGPoint(x: 40, y: 40))
        XCTAssertEqual(restoredLabel.leadPoint, CGPoint(x: 80, y: 80))
    }

    @MainActor
    func testPreviewViewModelNonDestructiveTextEditing() {
        let baseImage = createTestImage(width: 200, height: 200, color: .white)
        let display = DisplayInfo(id: 1, name: "Test", frame: CGRect(x: 0, y: 0, width: 200, height: 200), scaleFactor: 1.0, isPrimary: true)
        let textAnno = Annotation.text(TextAnnotation(position: CGPoint(x: 20, y: 20), content: "Original"))
        let rectAnno = Annotation.rectangle(RectangleAnnotation(rect: CGRect(x: 50, y: 50, width: 40, height: 40)))
        let screenshot = Screenshot(image: baseImage, sourceDisplay: display, annotations: [textAnno, rectAnno])
        let vm = PreviewViewModel(screenshot: screenshot)

        // Start editing first annotation and cancel -> must preserve original text and Z-Order (index 0)
        vm.beginEditingTextAnnotation(at: 0)
        XCTAssertTrue(vm.isWaitingForTextInput)
        vm.textInputContent = "Draft Change"
        vm.cancelCurrentDrawing()

        XCTAssertEqual(vm.annotations.count, 2)
        if case .text(let preserved) = vm.annotations[0] {
            XCTAssertEqual(preserved.content, "Original")
        } else {
            XCTFail("Text annotation lost or reordered on cancel")
        }

        // Now edit and commit -> updates in-place at index 0 and pushes undo state
        vm.beginEditingTextAnnotation(at: 0)
        vm.textInputContent = "Updated Text"
        vm.commitTextInput()

        if case .text(let updated) = vm.annotations[0] {
            XCTAssertEqual(updated.content, "Updated Text")
        } else {
            XCTFail("Expected updated text at index 0")
        }
        XCTAssertTrue(vm.canUndo)
    }

    func testPrivacyRedactionShortcutDoesNotConflictWithPin() {
        XCTAssertEqual(AnnotationToolType.privacyRedaction.keyboardShortcut, "x")
        XCTAssertNotEqual(AnnotationToolType.privacyRedaction.keyboardShortcut, "p")
    }

    func testShapeFillColorSeparationAndLegacyFallback() {
        // Legacy annotation without fillColor falls back to stroke color
        let legacy = RectangleAnnotation(rect: CGRect(x: 0, y: 0, width: 10, height: 10))
        XCTAssertNil(legacy.fillColor)
        XCTAssertEqual(legacy.effectiveFillColor, legacy.style.color)

        // Independent fill color
        let customFill = CodableColor(red: 0.2, green: 0.6, blue: 1.0)
        let filled = RectangleAnnotation(
            rect: CGRect(x: 0, y: 0, width: 10, height: 10),
            style: StrokeStyle(color: .red, lineWidth: 2),
            isFilled: true,
            fillColor: customFill
        )
        XCTAssertEqual(filled.effectiveFillColor, customFill)
        XCTAssertEqual(filled.style.color, .red)

        // Ellipse mirrors the same behavior
        let ellipse = Annotation.ellipse(EllipseAnnotation(rect: CGRect(x: 0, y: 0, width: 10, height: 10)))
        let updated = ellipse.updatingFillColor(customFill)
        guard case .ellipse(let e) = updated else {
            XCTFail("Expected ellipse")
            return
        }
        XCTAssertEqual(e.fillColor, customFill)

        // Non-shape types ignore fill updates
        let lineAnno = Annotation.line(LineAnnotation(startPoint: .zero, endPoint: CGPoint(x: 10, y: 10)))
        if case .line(let l) = lineAnno.updatingFillColor(customFill) {
            XCTAssertEqual(l.style.color, StrokeStyle.default.color)
        } else {
            XCTFail("Expected line")
        }
    }

    func testTextWrapWidthMultiLineBoundsAndScaling() {
        let content = "Hello world this is a multi-line annotation test"
        let single = TextAnnotation(position: .zero, content: content)
        let wrapped = TextAnnotation(position: .zero, content: content, wrapWidth: 60)

        // Wrapping must shrink width but grow height relative to the single-line case
        XCTAssertLessThan(wrapped.bounds.width, single.bounds.width)
        XCTAssertGreaterThan(wrapped.bounds.height, single.bounds.height)
        XCTAssertGreaterThanOrEqual(wrapped.bounds.height, single.style.fontSize * 1.5)

        // Hard line breaks count as extra lines
        let multiline = TextAnnotation(position: .zero, content: "one\ntwo\nthree")
        XCTAssertGreaterThan(multiline.bounds.height, multiline.style.fontSize * 2)

        // Scaling preserves wrapWidth proportionally
        let scaled = Annotation.text(multiline).scaled(by: 2.0)
        guard case .text(let t) = scaled else {
            XCTFail("Expected text")
            return
        }
        XCTAssertEqual(t.style.fontSize, multiline.style.fontSize * 2.0, accuracy: 0.01)
        XCTAssertNil(multiline.wrapWidth)

        let wrappedScaled = Annotation.text(wrapped).scaled(by: 2.0)
        guard case .text(let tw) = wrappedScaled else {
            XCTFail("Expected text")
            return
        }
        XCTAssertEqual(tw.wrapWidth ?? 0, 120, accuracy: 0.5)

        // Offset moves the whole box
        let moved = Annotation.text(wrapped).offsetBy(dx: 30, dy: 15)
        guard case .text(let tm) = moved else {
            XCTFail("Expected text")
            return
        }
        XCTAssertEqual(tm.position, CGPoint(x: 30, y: 15))
    }

    func testHandleDragPreservesAspectRatioWhenRequested() {
        let rectAnno = Annotation.rectangle(
            RectangleAnnotation(rect: CGRect(x: 0, y: 0, width: 100, height: 50))
        )

        // Free drag changes aspect
        let free = rectAnno.applyingHandleDrag(.bottomRight, to: CGPoint(x: 200, y: 80))
        guard case .rectangle(let r) = free else {
            XCTFail("Expected rectangle")
            return
        }
        XCTAssertEqual(r.rect.width, 200, accuracy: 0.5)
        XCTAssertEqual(r.rect.height, 80, accuracy: 0.5)

        // ⌘ drag locks the 2:1 ratio of the original
        let locked = rectAnno.applyingHandleDrag(
            .bottomRight,
            to: CGPoint(x: 200, y: 80),
            options: HandleDragOptions(preserveAspectRatio: true)
        )
        guard case .rectangle(let lr) = locked else {
            XCTFail("Expected rectangle")
            return
        }
        XCTAssertEqual(lr.rect.width / lr.rect.height, 2.0, accuracy: 0.02)
        XCTAssertEqual(lr.rect.origin, .zero)

        // Top-left handle keeps the opposite corner anchored
        let fromTopLeft = rectAnno.applyingHandleDrag(.topLeft, to: CGPoint(x: -10, y: -10))
        guard case .rectangle(let tl) = fromTopLeft else {
            XCTFail("Expected rectangle")
            return
        }
        XCTAssertEqual(tl.rect.maxX, 100, accuracy: 0.5)
        XCTAssertEqual(tl.rect.maxY, 50, accuracy: 0.5)
    }

    func testTextHandlesCornerScaleAndEdgeWrap() {
        let anno = TextAnnotation(
            position: CGPoint(x: 0, y: 0),
            content: "Hello annotation",
            style: TextStyle(color: .red, fontSize: 20, fontName: ".AppleSystemUIFont"),
            wrapWidth: 200
        )
        let base = Annotation.text(anno)

        // Eight handles around the text box (4 corners + 4 edges)
        let handles = base.controlHandles()
        XCTAssertEqual(handles.count, 8)
        XCTAssertTrue(handles.map(\.handle).contains(.leftEdge))
        XCTAssertTrue(handles.map(\.handle).contains(.rightEdge))
        XCTAssertTrue(handles.map(\.handle).contains(.topEdge))
        XCTAssertTrue(handles.map(\.handle).contains(.bottomEdge))

        // Corner drag scales font size and wrap width together
        let originalHeight = anno.bounds.height
        let scaled = base.applyingHandleDrag(.bottomRight, to: CGPoint(x: anno.bounds.maxX * 2, y: anno.bounds.maxY * 2))
        guard case .text(let st) = scaled else {
            XCTFail("Expected text")
            return
        }
        XCTAssertGreaterThan(st.style.fontSize, 20)
        XCTAssertGreaterThan(st.wrapWidth ?? 0, 200)
        XCTAssertGreaterThan(st.bounds.height, originalHeight)

        // Right edge drag changes wrap width but keeps the font size
        let wrapped = base.applyingHandleDrag(.rightEdge, to: CGPoint(x: 60, y: anno.bounds.midY))
        guard case .text(let wt) = wrapped else {
            XCTFail("Expected text")
            return
        }
        XCTAssertEqual(wt.style.fontSize, 20, accuracy: 0.01)
        XCTAssertEqual(wt.wrapWidth ?? 0, 60, accuracy: 1.0)
        XCTAssertLessThan(wt.bounds.width, anno.bounds.width)

        // Left edge drag keeps the right edge fixed
        let fromLeft = base.applyingHandleDrag(.leftEdge, to: CGPoint(x: 40, y: anno.bounds.midY))
        guard case .text(let lt) = fromLeft else {
            XCTFail("Expected text")
            return
        }
        let rightEdgeBefore = anno.bounds.maxX
        XCTAssertEqual(lt.bounds.maxX, rightEdgeBefore, accuracy: 2.0)
    }
}
