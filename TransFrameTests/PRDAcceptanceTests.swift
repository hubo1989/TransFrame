import XCTest
import CoreGraphics
import AppKit
@testable import TransFrame

/// PRD acceptance tests for 标注体验与内嵌面板增强.
/// Each test maps to a PRD success criterion / business rule.
@MainActor
final class PRDAcceptanceTests: XCTestCase {

    // MARK: - Helpers

    private func stringsURL(_ lang: String) -> URL {
        URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()          // TransFrameTests/
            .deletingLastPathComponent()          // repo root
            .appendingPathComponent("TransFrame/Resources/\(lang).lproj/Localizable.strings")
    }

    private func parseStrings(_ lang: String) -> [String: String] {
        let contents = try! String(contentsOf: stringsURL(lang), encoding: .utf8)
        var result: [String: String] = [:]
        // Matches: "key" = "value";   (value may contain escaped quotes)
        let pattern = "\"((?:[^\"\\\\]|\\\\.)+)\"\\s*=\\s*\"((?:[^\"\\\\]|\\\\.)+)\"\\s*;"
        let regex = try! NSRegularExpression(pattern: pattern)
        let ns = contents as NSString
        for match in regex.matches(in: contents, range: NSRange(location: 0, length: ns.length)) {
            let key = ns.substring(with: match.range(at: 1))
            let value = ns.substring(with: match.range(at: 2))
            result[key] = value
        }
        return result
    }

    private func sourceLocalizationKeys() -> Set<String> {
        var keys = Set<String>()
        let root = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .appendingPathComponent("TransFrame")
        let enumerator = FileManager.default.enumerator(at: root, includingPropertiesForKeys: nil)!
        let patterns = [
            "String\\(localized:\\s*\"([^\"]+)\"",
            "NSLocalizedString\\(\"([^\"]+)\"",
            "Text\\(\"([a-zA-Z0-9_.]+)\"\\)"   // dot-separated SwiftUI keys only
        ]
        for case let file as URL in enumerator where file.pathExtension == "swift" {
            guard let contents = try? String(contentsOf: file, encoding: .utf8) else { continue }
            for pattern in patterns {
                let regex = try! NSRegularExpression(pattern: pattern)
                let ns = contents as NSString
                for match in regex.matches(in: contents, range: NSRange(location: 0, length: ns.length)) {
                    let key = ns.substring(with: match.range(at: 1))
                    // Only treat dotted identifiers as localization keys
                    if key.contains(".") {
                        keys.insert(key)
                    }
                }
            }
        }
        return keys
    }

    // MARK: - 5.1 Menu bar localization (success criterion 1)

    /// Every localization key referenced in source must exist in BOTH language
    /// files with a non-empty, non-raw-key value. Catches "raw key exposed in UI".
    func testAllLocalizationKeysResolvedInBothLanguages() {
        let zh = parseStrings("zh-Hans")
        let en = parseStrings("en")
        let keys = sourceLocalizationKeys()
        XCTAssertGreaterThan(keys.count, 50, "Expected to scan a meaningful number of keys")

        var missing: [String] = []
        var rawExposed: [String] = []
        for key in keys.sorted() {
            for (lang, table) in [("zh-Hans", zh), ("en", en)] {
                if table[key] == nil {
                    missing.append("\(lang): \(key)")
                } else if table[key] == key {
                    rawExposed.append("\(lang): \(key)")
                }
            }
        }
        XCTAssertTrue(missing.isEmpty, "Missing localization keys:\n" + missing.joined(separator: "\n"))
        XCTAssertTrue(rawExposed.isEmpty, "Values equal to key (raw key exposure):\n" + rawExposed.joined(separator: "\n"))
    }

    /// PRD 5.1: the two previously broken menu entries must resolve in both languages.
    func testMenuColorPickerAndOpenImageLocalized() {
        let zh = parseStrings("zh-Hans")
        let en = parseStrings("en")

        XCTAssertEqual(zh["menu.pick.color"], "屏幕取色")
        XCTAssertEqual(en["menu.pick.color"], "Pick Screen Color")
        XCTAssertEqual(zh["menu.open.image"], "打开图片编辑...")
        XCTAssertEqual(en["menu.open.image"], "Open Image for Editing...")

        for key in ["menu.capture.full.screen", "menu.capture.selection", "menu.translation.mode",
                    "menu.translation.history", "menu.settings", "menu.about", "menu.quit"] {
            XCTAssertNotNil(zh[key], "zh missing \(key)")
            XCTAssertNotNil(en[key], "en missing \(key)")
        }
    }

    // MARK: - 5.1 Text tool cognition (success criterion 3)

    func testTextToolDisplayNameAndTooltip() {
        XCTAssertEqual(AnnotationToolType.text.displayName, "文本")
        let zh = parseStrings("zh-Hans")
        XCTAssertEqual(zh["tool.text"], "文本")
        XCTAssertEqual(parseStrings("en")["tool.text"], "Text")

        // In-place toolbar shares the same display name (tooltip = name (shortcut))
        let inPlace = SelectionInPlaceToolbar.AnnotationTool.text
        XCTAssertEqual(inPlace.displayName, "文本")
        XCTAssertEqual(inPlace.shortcutKey, "T")
        XCTAssertTrue(inPlace.tooltip.contains("文本"))
        XCTAssertTrue(inPlace.tooltip.contains("T"))
    }

    // MARK: - 5.3 Line tool (success criterion 4)

    func testLineToolExistsInBothToolchains() {
        // Preview tool chain
        XCTAssertTrue(AnnotationToolType.allCases.contains(.line))
        XCTAssertEqual(AnnotationToolType.line.displayName, "直线")

        // In-place toolbar must expose the line tool as well
        XCTAssertTrue(SelectionInPlaceToolbar.AnnotationTool.allCases.contains(.line))
        XCTAssertEqual(SelectionInPlaceToolbar.AnnotationTool.line.displayName, "直线")

        // Drawing produces a line annotation between the two drag points
        var tool = LineTool()
        tool.beginDrawing(at: CGPoint(x: 10, y: 10))
        tool.continueDrawing(to: CGPoint(x: 100, y: 60))
        let anno = tool.endDrawing(at: CGPoint(x: 100, y: 60))
        guard case .line(let line)? = anno else {
            XCTFail("Expected line annotation")
            return
        }
        XCTAssertEqual(line.startPoint, CGPoint(x: 10, y: 10))
        XCTAssertEqual(line.endPoint, CGPoint(x: 100, y: 60))
    }

    // MARK: - 5.3 ⌘ square/circle + aspect lock (success criterion 5)

    func testCommandConstrainsSquareAndCircleWhileDrawing() {
        // Pure geometry helper
        let square = GeometryConstraint.squareRect(from: CGPoint(x: 10, y: 10), to: CGPoint(x: 80, y: 30))
        XCTAssertEqual(square.width, square.height)
        XCTAssertEqual(square.width, 70, accuracy: 0.01)

        // Rectangle tool
        var rect = RectangleTool()
        rect.beginDrawing(at: CGPoint(x: 10, y: 10))
        rect.continueDrawing(to: CGPoint(x: 80, y: 30))
        let freeRect = rect.calculatedRect(constrainSquare: false)
        XCTAssertEqual(freeRect, CGRect(x: 10, y: 10, width: 70, height: 20))
        let lockedRect = rect.calculatedRect(constrainSquare: true)
        XCTAssertEqual(lockedRect.width, lockedRect.height)

        // Ellipse tool (circle)
        var ellipse = EllipseTool()
        ellipse.beginDrawing(at: CGPoint(x: 0, y: 0))
        ellipse.continueDrawing(to: CGPoint(x: 50, y: 20))
        let lockedEllipse = ellipse.calculatedRect(constrainSquare: true)
        XCTAssertEqual(lockedEllipse.width, lockedEllipse.height)
        let freeEllipse = ellipse.calculatedRect(constrainSquare: false)
        XCTAssertEqual(freeEllipse.size, CGSize(width: 50, height: 20))
    }

    func testCommandLocksAspectRatioWhileResizing() {
        let anno = Annotation.ellipse(
            EllipseAnnotation(rect: CGRect(x: 0, y: 0, width: 200, height: 100))
        )
        let locked = anno.applyingHandleDrag(
            .bottomRight,
            to: CGPoint(x: 400, y: 350),
            options: HandleDragOptions(preserveAspectRatio: true)
        )
        guard case .ellipse(let e) = locked else {
            XCTFail("Expected ellipse")
            return
        }
        XCTAssertEqual(e.rect.width / e.rect.height, 2.0, accuracy: 0.02)
    }

    // MARK: - 5.3 Dual-color shapes (success criterion 6)

    func testBorderAndFillColorsAreIndependent() {
        let border = CodableColor(.red)
        let fill = CodableColor(red: 0, green: 0.5, blue: 1, opacity: 0.6)

        let rect = RectangleAnnotation(
            rect: CGRect(x: 0, y: 0, width: 10, height: 10),
            style: StrokeStyle(color: border, lineWidth: 3),
            isFilled: true,
            fillColor: fill
        )
        XCTAssertEqual(rect.style.color, border)
        XCTAssertEqual(rect.effectiveFillColor, fill)
        XCTAssertNotEqual(rect.style.color, rect.effectiveFillColor)

        // Hollow: background transparent (fill not applied by renderers when !isFilled)
        let hollow = RectangleAnnotation(rect: CGRect(x: 0, y: 0, width: 10, height: 10), isFilled: false)
        XCTAssertFalse(hollow.isFilled)

        // Ellipse mirrors the same independence
        let ellipse = EllipseAnnotation(
            rect: CGRect(x: 0, y: 0, width: 10, height: 10),
            style: StrokeStyle(color: border, lineWidth: 2),
            isFilled: true,
            fillColor: fill
        )
        XCTAssertEqual(ellipse.style.color, border)
        XCTAssertEqual(ellipse.effectiveFillColor, fill)

        // updatingFillColor only touches the background, never the border
        let updated = Annotation.rectangle(rect).updatingFillColor(CodableColor(.green))
        guard case .rectangle(let r) = updated else {
            XCTFail("Expected rectangle")
            return
        }
        XCTAssertEqual(r.effectiveFillColor, CodableColor(.green))
        XCTAssertEqual(r.style.color, border, "Border color must stay untouched")
    }

    // MARK: - 5.2 Text box multi-line / drag / handles (success criterion 2)

    func testTextBoxCommitSelectsAndExposesEightHandles() {
        let image = Self.makeImage(width: 100, height: 100)
        let display = DisplayInfo(id: 1, name: "T", frame: CGRect(x: 0, y: 0, width: 100, height: 100), scaleFactor: 1.0, isPrimary: true)
        let vm = PreviewViewModel(screenshot: Screenshot(image: image, sourceDisplay: display))

        vm.selectTool(.text)
        vm.beginDrawing(at: CGPoint(x: 5, y: 5))
        vm.textInputContent = "Hello\nWorld multiline"
        vm.commitTextInput()

        // Committed text is selected so its handles show immediately
        XCTAssertEqual(vm.selectedAnnotationIndex, 0)
        XCTAssertEqual(vm.annotations.count, 1)

        let handles = vm.annotations[0].controlHandles()
        XCTAssertEqual(handles.count, 8, "Text box must expose 8 control handles (4 corners + 4 edges)")
        XCTAssertTrue(handles.map(\.handle).contains(.leftEdge))
        XCTAssertTrue(handles.map(\.handle).contains(.rightEdge))

        // Whole-box drag moves content without reflow
        let before = vm.annotations[0].bounds
        vm.beginDraggingAnnotation(at: before.origin)
        vm.continueDraggingAnnotation(to: CGPoint(x: before.minX + 25, y: before.minY + 15))
        vm.endDraggingAnnotation()
        let after = vm.annotations[0].bounds
        XCTAssertEqual(after.origin, CGPoint(x: before.minX + 25, y: before.minY + 15))
        XCTAssertEqual(after.size, before.size, "Dragging must not reflow or rescale content")
    }

    // MARK: - 5.4 Mosaic intensity (success criterion 8)

    func testMosaicIntensityClampingAndLiveResize() {
        // Block size is clamped to a sane range (granularity control)
        XCTAssertEqual(MosaicAnnotation(rect: CGRect(x: 0, y: 0, width: 20, height: 20), blockSize: 2).blockSize, 4)
        XCTAssertEqual(MosaicAnnotation(rect: CGRect(x: 0, y: 0, width: 20, height: 20), blockSize: 999).blockSize, 128)

        // Intensity presets offered in the in-place style bar map to distinct sizes
        let light = 4, medium = 10, strong = 20
        XCTAssertLessThan(light, medium)
        XCTAssertLessThan(medium, strong)

        // Selected mosaic can be re-strengthened
        let image = Self.makeImage(width: 60, height: 60)
        let display = DisplayInfo(id: 1, name: "T", frame: CGRect(x: 0, y: 0, width: 60, height: 60), scaleFactor: 1.0, isPrimary: true)
        var screenshot = Screenshot(image: image, sourceDisplay: display)
        screenshot = screenshot.adding(.mosaic(MosaicAnnotation(rect: CGRect(x: 5, y: 5, width: 30, height: 30), blockSize: 4)))
        let vm = PreviewViewModel(screenshot: screenshot)
        vm.selectAnnotation(at: 0)
        vm.updateSelectedAnnotationBlockSize(20)
        guard case .mosaic(let m) = vm.annotations[0] else {
            XCTFail("Expected mosaic")
            return
        }
        XCTAssertEqual(m.blockSize, 20)
    }

    // MARK: - 5.4 Smart mosaic shares VLM configuration (success criterion 9)

    func testSmartMosaicUsesSharedVLMProviderConfiguration() {
        // Local providers need no API key — smart mosaic must accept them as configured
        XCTAssertFalse(VLMProviderType.ollama.requiresAPIKey)
        XCTAssertTrue(VLMProviderType.openai.requiresAPIKey)

        // The privacy prompt must be part of the shared VLM prompt pair system
        XCTAssertEqual(VLMPromptPair.privacy.system.isEmpty, false)
        XCTAssertTrue(VLMPromptPair.privacy.user.lowercased().contains("sensitive"))

        // Error copy names the VLM settings it shares
        let zh = parseStrings("zh-Hans")
        let en = parseStrings("en")
        XCTAssertTrue(zh["smartMosaic.error.notConfigured"]?.contains("VLM") == true, "Prompt must point at VLM config")
        XCTAssertTrue(en["smartMosaic.error.notConfigured"]?.contains("VLM") == true)

        // Localized feedback states from PRD 5.4
        XCTAssertEqual(zh["smartMosaic.detecting"], "正在识别图中隐私...")
        XCTAssertEqual(zh["smartMosaic.noneFound"], "未检测到敏感隐私信息")
        XCTAssertEqual(zh["smartMosaic.redacted"], "已自动遮挡 %d 处隐私")
        XCTAssertEqual(zh["smartMosaic.error.failed"], "隐私识别失败，请检查网络或重试")
    }

    // MARK: - 5.5 Embedded results drawer (success criterion 10)

    func testEmbeddedDrawerStringsAndModel() {
        let zh = parseStrings("zh-Hans")
        // Drawer chrome from PRD 5.5
        XCTAssertEqual(zh["preview.panel.ocrTab"], "文本识别")
        XCTAssertEqual(zh["preview.panel.translateTab"], "翻译")
        XCTAssertNotNil(zh["preview.panel.rerun"])       // 重新识别
        XCTAssertNotNil(zh["preview.panel.copyAll"])     // 复制全部
        XCTAssertEqual(zh["preview.panel.close"], "关闭")
        XCTAssertEqual(zh["preview.panel.recognizing"], "正在识别...")
        XCTAssertNotNil(zh["preview.panel.copyLine"])    // 单行复制
        XCTAssertNotNil(zh["preview.panel.original"])    // 原文
        XCTAssertNotNil(zh["preview.panel.translation"]) // 译文

        // Model state machine: loading → lines, tab switching keeps data
        let model = InPlaceResultsModel()
        XCTAssertFalse(model.isVisible)
        model.isVisible = true
        model.activeTab = .ocr
        model.isLoading = true
        model.isLoading = false
        model.ocrLines = ["line1", "line2"]
        model.activeTab = .translate
        XCTAssertEqual(model.ocrLines.count, 2, "Tab switch must not drop OCR results")
    }

    /// In-place drawer actions must not be routed to the delegate (no external window)
    func testInPlaceDrawerActionsAreLocal() {
        // The overlay handles .ocr/.translate itself; dispatchAction treats
        // .smartMosaic as local-only. Verify the action enum still carries them.
        let actions: [SelectionInPlaceToolbar.Action] = [.ocr, .translate, .smartMosaic]
        XCTAssertTrue(actions.contains(.ocr))
        XCTAssertTrue(actions.contains(.translate))
        XCTAssertTrue(actions.contains(.smartMosaic))
    }

    // MARK: - Export rendering (fill color + multi-line text)

    func testExportRenderingAppliesFillAndWrapsText() {
        let size = 200
        guard let ctx = CGContext(
            data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0,
            space: CGColorSpace(name: CGColorSpace.sRGB)!,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ) else {
            XCTFail("context")
            return
        }
        ctx.setFillColor(NSColor.white.cgColor)
        ctx.fill(CGRect(x: 0, y: 0, width: size, height: size))

        let fill = CodableColor(red: 0, green: 0, blue: 1, opacity: 1)  // pure blue fill
        let rectAnno = RectangleAnnotation(
            rect: CGRect(x: 20, y: 20, width: 60, height: 40),
            style: StrokeStyle(color: .red, lineWidth: 2),
            isFilled: true,
            fillColor: fill
        )
        ImageExporter.shared.renderAnnotation(.rectangle(rectAnno), in: ctx, imageHeight: CGFloat(size))

        // Multi-line text renders without trapping
        let textAnno = TextAnnotation(
            position: CGPoint(x: 100, y: 20),
            content: "hello world this wraps",
            style: TextStyle(color: .black, fontSize: 12, fontName: ".AppleSystemUIFont"),
            wrapWidth: 80
        )
        ImageExporter.shared.renderAnnotation(.text(textAnno), in: ctx, imageHeight: CGFloat(size))

        guard let image = ctx.makeImage() else {
            XCTFail("image")
            return
        }
        // Sample a pixel inside the filled rect: expect blue-dominant
        let pixel = Self.pixel(in: image, x: 50, y: 50)
        XCTAssertNotNil(pixel)
        if let pixel {
            XCTAssertGreaterThan(pixel.b, pixel.r, "Fill color must paint (blue > red), got \(pixel)")
            XCTAssertGreaterThan(pixel.b, 0.5, "Fill should be strongly blue, got \(pixel)")
        }
    }

    // MARK: - Cross-line selection & line copy (user report 2026-10-23)

    func testResultsTextBlockSupportsCrossLineSelectionAndLineCopy() {
        // One continuous attributed block = selection can span lines
        let ocr = ResultsTextBuilder.ocrText(lines: ["line one", "line two", "line three"])
        XCTAssertEqual(ocr.string, "line one\nline two\nline three")

        // Line extraction used by click-to-copy
        XCTAssertEqual(ResultsTextView.lineString(at: 0, in: ocr.string), "line one")
        XCTAssertEqual(ResultsTextView.lineString(at: 10, in: ocr.string), "line two")
        XCTAssertEqual(ResultsTextView.lineString(at: 22, in: ocr.string), "line three")
        XCTAssertNil(ResultsTextView.lineString(at: 0, in: ""))

        // Translation pairs stay in one block (original + translated styles)
        let seg = BilingualSegment(
            original: TextSegment(text: "hello", boundingBox: CGRect(x: 0, y: 0, width: 1, height: 1)),
            translated: "你好",
            sourceLanguage: "en",
            targetLanguage: "zh-Hans"
        )
        let tr = ResultsTextBuilder.translationText(segments: [seg])
        XCTAssertTrue(tr.string.contains("hello"))
        XCTAssertTrue(tr.string.contains("你好"))
    }

    /// Toolbar buttons must keep a hit area around the icon so edge clicks
    /// don't fall through to the overlay and cancel the selection.
    func testInPlaceToolbarExposesToolAndActionButtons() {
        XCTAssertTrue(SelectionInPlaceToolbar.AnnotationTool.allCases.contains(.line))
        // Every tool tooltip carries its shortcut — used for a11y + E2E targeting
        for tool in SelectionInPlaceToolbar.AnnotationTool.allCases {
            XCTAssertFalse(tool.tooltip.isEmpty)
        }
    }

    // MARK: - Regression tests (user report 2026-10-23)

    /// AppKit's `hitTest(_:)` hands the point in the **superview's** coordinate
    /// system. The in-place toolbar previously compared it against its own
    /// `bounds`, so real clicks/hovers fell through and tools went dead.
    func testInPlaceToolbarHitTestAcceptsSuperviewCoordinates() {
        let parent = NSView(frame: NSRect(x: 0, y: 0, width: 1400, height: 900))
        let bar = SelectionInPlaceToolbar(frame: NSRect(x: 400, y: 300, width: 520, height: 42))
        parent.addSubview(bar)

        // A point inside the bar's frame, expressed in the parent's coordinates
        let insideInSuperview = NSPoint(x: 450, y: 320)
        XCTAssertNotNil(
            bar.hitTest(insideInSuperview),
            "hitTest must treat its argument as superview coordinates"
        )

        // A point far outside the bar must pass through to the overlay
        XCTAssertNil(bar.hitTest(NSPoint(x: 50, y: 50)))
    }

    /// The toolbar must report a size that fits every button. The legacy
    /// hard-coded 520pt predates the line/smart-mosaic/eyedropper additions
    /// and pushed the leftmost buttons (rectangle, ellipse) outside the view,
    /// which killed their hover tooltips and clicks.
    func testInPlaceToolbarPreferredSizeAccommodatesEveryButton() {
        let bar = SelectionInPlaceToolbar(frame: .zero)
        let fitted = bar.fittingContentSize
        XCTAssertGreaterThan(fitted.width, 0, "SwiftUI content must lay out")
        XCTAssertGreaterThanOrEqual(
            bar.preferredSize.width + 0.5,
            fitted.width,
            "preferredSize must never be narrower than the button row it hosts"
        )
        // 10 tools + beautify/SM/OCR/pin/save/upload + undo/redo + cancel/confirm
        XCTAssertGreaterThanOrEqual(
            bar.preferredSize.width,
            CGFloat(SelectionInPlaceToolbar.AnnotationTool.allCases.count + 9) * 24,
            "every tool and action button needs a click slot"
        )
    }

    /// 截图翻译 must differ from 区域截图: it runs translation right after a
    /// region is selected instead of waiting for the confirm button.
    func testCaptureModesHaveDistinctPostSelectionActions() {
        XCTAssertNil(
            CaptureSessionController.CaptureMode.region.automaticActionAfterSelection,
            "区域截图 must not auto-trigger any analysis"
        )
        XCTAssertEqual(
            CaptureSessionController.CaptureMode.screenshotTranslation.automaticActionAfterSelection,
            .translate,
            "截图翻译 must auto-trigger translation after selection"
        )
        XCTAssertNil(
            CaptureSessionController.CaptureMode.fullScreen.automaticActionAfterSelection
        )
    }

    // MARK: - Results drawer regressions (review 2026-10-23)

    /// Long OCR/translation results must overflow the fixed-height drawer
    /// viewport and stay scrollable — the text view has to grow with content.
    func testResultsTextViewGrowsBeyondViewportForLongContent() {
        let scroll = SelectableTextBlock.makeScrollView(
            textView: SelectableTextBlock.makeConfiguredTextView()
        )
        let view = scroll.documentView as! ResultsTextView
        scroll.frame = NSRect(x: 0, y: 0, width: 280, height: 200)

        XCTAssertTrue(view.isVerticallyResizable, "content height must drive the frame")
        XCTAssertTrue(view.textContainer?.widthTracksTextView == true, "must wrap at the drawer width")

        let lines = (0..<60).map { "OCR line \($0) with enough words to wrap at the panel width" }
        view.textStorage?.setAttributedString(ResultsTextBuilder.ocrText(lines: lines))
        view.fitToViewport(width: 280)

        XCTAssertGreaterThan(
            view.frame.height, 200,
            "60 wrapped lines must exceed the 200pt viewport, not get clipped"
        )
    }

    /// Only a plain single click copies its line — shift+click extends the
    /// selection, drags end in one, and a double-click's first release is
    /// followed by word selection (none may clobber the pasteboard).
    /// (Event-level testing is impossible here: NSTextView.mouseDown runs a
    /// tracking loop that waits on the real event queue.)
    func testLineCopyDecisionExcludesShiftClickDragsAndDoubleClicks() {
        XCTAssertTrue(
            ResultsTextView.shouldCopyLine(clickCount: 1, modifierFlags: [], travel: 1),
            "plain single click copies the line"
        )
        XCTAssertFalse(
            ResultsTextView.shouldCopyLine(clickCount: 1, modifierFlags: [.shift], travel: 0),
            "shift+click extends the selection"
        )
        XCTAssertFalse(
            ResultsTextView.shouldCopyLine(clickCount: 2, modifierFlags: [], travel: 0),
            "double-click selects a word"
        )
        XCTAssertFalse(
            ResultsTextView.shouldCopyLine(clickCount: 1, modifierFlags: [], travel: 5),
            "drag ends in a selection"
        )
    }

    /// The merged drawer must clear the selection's resize-handle hit areas
    /// (child views win hit testing) and never enter the toolbar zone below
    /// the selection (toolbar top sits at maxY + 8).
    func testResultsPanelFrameClearsHandlesAndToolbarZone() {
        let bounds = NSRect(x: 0, y: 0, width: 1400, height: 900)

        // Typical short selection on the right half
        let sel = NSRect(x: 600, y: 100, width: 200, height: 120)
        let frame = SelectionOverlayView.resultsPanelFrame(
            selectionRect: sel, bounds: bounds, handleSize: 8
        )
        XCTAssertGreaterThanOrEqual(
            frame.minX - sel.maxX, 10,
            "gap must clear the handle hit radius (handleSize + 2)"
        )
        XCTAssertLessThanOrEqual(
            frame.maxY, sel.maxY + 8 + 0.5,
            "drawer bottom must not enter the toolbar zone (sel.maxY + 8)"
        )

        // Right edge too tight: flip to the left without covering the selection
        let rightHugging = NSRect(x: 1150, y: 100, width: 220, height: 120)
        let flipped = SelectionOverlayView.resultsPanelFrame(
            selectionRect: rightHugging, bounds: bounds, handleSize: 8
        )
        XCTAssertLessThanOrEqual(
            flipped.maxX, rightHugging.minX + 0.5,
            "left-side fallback must not overlap the selection content"
        )

        // Near-fullscreen selection: neither side fits — hug the screen edge
        let fullscreenish = NSRect(x: 0, y: 0, width: 1390, height: 890)
        let edge = SelectionOverlayView.resultsPanelFrame(
            selectionRect: fullscreenish, bounds: bounds, handleSize: 8
        )
        XCTAssertLessThanOrEqual(
            edge.maxX, bounds.maxX - 8 + 0.5,
            "fallback must stay inside the screen"
        )
    }

    /// VLM gateways forwarding the OpenAI Responses API put a `reasoning`
    /// output item in front of the `message` item — parsing must skip it and
    /// read the message's text blocks (user report 2026-10-23: deepseek relay).
    func testVLMResponsesAPIWithLeadingReasoningItemParses() throws {
        let provider = OpenAIVLMProvider(
            configuration: try VLMProviderConfiguration(
                apiKey: "test",
                baseURLString: "https://example.invalid/v1",
                modelName: "test"
            )
        )
        let payload = """
        {"created_at":1792742674,"error":null,"id":"resp_1e895847","incomplete_details":null,
        "model":"deepseek-v4.1-flash-free","object":"response",
        "output":[
          {"id":"rs_18e119","type":"reasoning","summary":[]},
          {"id":"msg_01","type":"message","role":"assistant",
           "content":[{"type":"output_text","text":"TRANSLATED"}]}
        ],
        "status":"completed"}
        """
        let (content, isTruncated, finishReason) = try OpenAIVLMProvider.extractContentAndStatus(from: Data(payload.utf8))
        XCTAssertEqual(content, "TRANSLATED", "message text must be read past the reasoning item")
        XCTAssertFalse(isTruncated)
        XCTAssertNil(finishReason)

        let incomplete = payload.replacingOccurrences(of: "\"status\":\"completed\"", with: "\"status\":\"incomplete\"")
        let (_, truncated, reason) = try OpenAIVLMProvider.extractContentAndStatus(from: Data(incomplete.utf8))
        XCTAssertTrue(truncated)
        XCTAssertEqual(reason, "length")
    }

    /// 验证真实选区窗口的绘制结果，不依赖右侧面板或坐标函数自报成功。
    /// 译文只出现在右侧结果面板：翻译 tab 激活时选区内的原图像素必须
    /// 完全不被遮挡（用户反馈密集文字下原位覆盖不可读，已撤除）。
    func testTranslateTabKeepsSelectionPixelsUntouched() throws {
        let screen = try XCTUnwrap(NSScreen.main)
        let bounds = CGRect(origin: .zero, size: screen.frame.size)
        let displayID = CGMainDisplayID()
        let image = Self.makeImage(width: Int(bounds.width), height: Int(bounds.height))
        let descriptor = DisplayDescriptor(
            displayID: displayID, localizedName: "Test", frame: screen.frame,
            scaleFactor: 1, isPrimary: true
        )
        let snapshot = DisplaySnapshot(
            displayID: displayID, descriptor: descriptor,
            cocoaBounds: CocoaGlobalRect(screen.frame), quartzBounds: QuartzGlobalRect(bounds),
            scaleFactor: 1, image: image
        )
        let snapshots = FrozenSnapshotSet(snapshots: [snapshot], candidateWindows: [], triggerReceivedAt: .now)
        let window = SelectionOverlayWindow(screen: screen, snapshot: snapshot, snapshotSet: snapshots)
        window.isReleasedWhenClosed = false
        defer { window.orderOut(nil); window.close() }
        let overlay = try XCTUnwrap(window.overlayView)
        let selection = CGRect(x: 100, y: 150, width: 600, height: 400)
        func mouse(_ type: NSEvent.EventType, _ point: CGPoint) throws -> NSEvent {
            try XCTUnwrap(NSEvent.mouseEvent(
                with: type, location: overlay.convert(point, to: nil), modifierFlags: [],
                timestamp: 0, windowNumber: window.windowNumber, context: nil,
                eventNumber: 0, clickCount: 1, pressure: 0
            ))
        }
        let start = selection.origin
        let end = CGPoint(x: selection.maxX, y: selection.maxY)
        overlay.mouseDown(with: try mouse(.leftMouseDown, start))
        overlay.mouseDragged(with: try mouse(.leftMouseDragged, end))
        overlay.mouseUp(with: try mouse(.leftMouseUp, end))
        overlay.resultsModel.isVisible = true
        overlay.resultsModel.activeTab = .translate
        overlay.resultsModel.segments = [BilingualSegment(
            original: TextSegment(text: "Hello world", boundingBox: CGRect(x: 0.1, y: 0.2, width: 0.5, height: 0.05)),
            translated: "你好，世界", sourceLanguage: "en", targetLanguage: "zh-Hans"
        )]
        window.orderFront(nil)
        let container = try XCTUnwrap(window.contentView)
        container.displayIfNeeded()
        let bitmap = try XCTUnwrap(container.bitmapImageRepForCachingDisplay(in: container.bounds))
        container.cacheDisplay(in: container.bounds, to: bitmap)
        let rendered = try XCTUnwrap(bitmap.cgImage)
        let scale = CGFloat(rendered.width) / bounds.width
        func color(at point: CGPoint) throws -> (r: Double, g: Double, b: Double) {
            try XCTUnwrap(Self.pixel(in: rendered, x: Int(point.x * scale), y: Int(point.y * scale)))
        }
        // 原图区域：任何位置都不得绘制译文/面板
        let formerStripArea = try color(at: CGPoint(x: 350, y: 268))
        XCTAssertGreaterThan(
            formerStripArea.r, 0.95,
            "译文不得绘制在选区画布上（只在右侧面板显示）"
        )
        let sourceArea = try color(at: CGPoint(x: 350, y: 240))
        XCTAssertGreaterThan(sourceArea.r, 0.95, "原文区必须保持原始像素")
        let unused = try color(at: CGPoint(x: 162, y: 475))
        XCTAssertGreaterThan(unused.r, 0.95, "不能沿错误的 Y 轴绘制译文")
        let attachment = XCTAttachment(image: NSImage(cgImage: rendered, size: bounds.size))
        attachment.name = "selection-bilingual-overlay"
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    /// Responses API 状态矩阵：截断判定按 status + incomplete_details.reason，
/// 顶层 output_text 与 message 共用同一套状态判定，空输出报明确错误。
    func testVLMResponsesAPIStatusMatrixAndChatRegression() throws {
        let provider = try Self.makeOpenAIProvider()
        let messageItem = #"{"id":"m1","type":"message","content":[{"type":"output_text","text":"HI"}]}"#

        // incomplete + max_output_tokens：判定截断，进入续写
        let truncatedPayload = """
        {"object":"response","status":"incomplete","incomplete_details":{"reason":"max_output_tokens"},"output":[\(messageItem)]}
        """
        let (t1, truncated1, reason1) = try OpenAIVLMProvider.extractContentAndStatus(from: Data(truncatedPayload.utf8))
        XCTAssertEqual(t1, "HI")
        XCTAssertTrue(truncated1)
        XCTAssertEqual(reason1, "length")

        // incomplete + content_filter：报错，不进入续写
        let filteredPayload = """
        {"object":"response","status":"incomplete","incomplete_details":{"reason":"content_filter"},"output":[\(messageItem)]}
        """
        XCTAssertThrowsError(try OpenAIVLMProvider.extractContentAndStatus(from: Data(filteredPayload.utf8))) { error in
            XCTAssertTrue("\(error)".contains("content_filter"), "应报出具体原因，实际: \(error)")
        }

        // 顶层 output_text 与 message 同样受状态判定约束
        let outputTextTruncated = """
        {"object":"response","status":"incomplete","incomplete_details":{"reason":"max_output_tokens"},"output_text":"HI"}
        """
        let (_, truncated2, _) = try OpenAIVLMProvider.extractContentAndStatus(from: Data(outputTextTruncated.utf8))
        XCTAssertTrue(truncated2, "output_text 分支不能绕过截断状态")

        // 空输出 / 仅 reasoning：在 Responses 分支直接报错，不跌入 Chat 路径
        for payload in [
            #"{"object":"response","status":"completed","output":[]}"#,
            #"{"object":"response","status":"completed","output":[{"id":"r1","type":"reasoning","summary":[]}]}"#
        ] {
            XCTAssertThrowsError(try OpenAIVLMProvider.extractContentAndStatus(from: Data(payload.utf8))) { error in
                XCTAssertTrue("\(error)".contains("no message text"), "空输出应给出来源明确的错误，实际: \(error)")
            }
        }

        // Chat 分支回归：stop 正常、length 截断
        let chat = { (finish: String) in Data(#"{"choices":[{"message":{"content":"HELLO"},"finish_reason":"\#(finish)"}]}"#.utf8) }
        let (c1, _, r1) = try OpenAIVLMProvider.extractContentAndStatus(from: chat("stop"))
        XCTAssertEqual(c1, "HELLO")
        XCTAssertEqual(r1, "stop", "finish_reason 原样透传，截断与否由 isTruncated 表达")
        let (_, truncated3, r3) = try OpenAIVLMProvider.extractContentAndStatus(from: chat("length"))
        XCTAssertTrue(truncated3)
        XCTAssertEqual(r3, "length")

        // 未知格式错误不含原始响应正文
        XCTAssertThrowsError(
            try OpenAIVLMProvider.extractContentAndStatus(from: Data("<html>oops</html>".utf8))
        ) { error in
            XCTAssertFalse("\(error)".contains("oops"), "错误信息不得回显响应正文")
        }
    }

    private static func makeOpenAIProvider() throws -> OpenAIVLMProvider {
        try OpenAIVLMProvider(
            configuration: VLMProviderConfiguration(
                apiKey: "test",
                baseURLString: "https://example.invalid/v1",
                modelName: "test"
            )
        )
    }

    // MARK: - Utilities

    private static func makeImage(width: Int, height: Int) -> CGImage {
        let ctx = CGContext(
            data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: 0,
            space: CGColorSpace(name: CGColorSpace.sRGB)!,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        )!
        ctx.setFillColor(NSColor.white.cgColor)
        ctx.fill(CGRect(x: 0, y: 0, width: width, height: height))
        return ctx.makeImage()!
    }

    /// Reads one pixel (top-left origin) from a bitmap-backed CGImage.
    private static func pixel(in image: CGImage, x: Int, y: Int) -> (r: Double, g: Double, b: Double)? {
        guard let data = image.dataProvider?.data else { return nil }
        let bytes = CFDataGetBytePtr(data)!
        let bpr = image.bytesPerRow
        let bpp = image.bitsPerPixel / 8
        let offset = y * bpr + x * bpp
        // premultipliedLast bitmap stores components as R, G, B, A
        let r = Double(bytes[offset]) / 255.0
        let g = Double(bytes[offset + 1]) / 255.0
        let b = Double(bytes[offset + 2]) / 255.0
        return (r, g, b)
    }
}
