import XCTest
@testable import TransFrame

@MainActor
final class TextInsertionSafetyTests: XCTestCase {
    func testChangedFocusNeverWrites() async throws {
        let target = InsertionTargetStub()
        let captured = try target.capture()
        target.isFocused = false
        await assertRejected(target, captured: captured)
        XCTAssertEqual(target.writeCount, 0)
        XCTAssertEqual(target.pasteCount, 0)
    }

    func testCollapsedOrMovedSelectionNeverWrites() async throws {
        for range in [NSRange(location: 5, length: 0), NSRange(location: 1, length: 5)] {
            let target = InsertionTargetStub()
            let captured = try target.capture()
            target.state.range = range
            await assertRejected(target, captured: captured)
            XCTAssertEqual(target.writeCount, 0)
            XCTAssertEqual(target.pasteCount, 0)
        }
    }

    func testChangedDocumentNeverWritesEvenWithSameSelectedText() async throws {
        let target = InsertionTargetStub()
        let captured = try target.capture()
        target.state.value = "Hello changed"
        await assertRejected(target, captured: captured)
        XCTAssertEqual(target.writeCount, 0)
        XCTAssertEqual(target.pasteCount, 0)
    }

    func testReplacesOnlyCapturedUTF16Range() async throws {
        let target = InsertionTargetStub(value: "👩🏽‍💻 Hello world", range: NSRange(location: 8, length: 5))
        let captured = try target.capture()
        try await TextInsertService.shared.replaceSelection(with: "你好👨‍👩‍👧‍👦", captured: captured)
        XCTAssertEqual(target.state.value, "👩🏽‍💻 你好👨‍👩‍👧‍👦 world")
        XCTAssertEqual(target.writeCount, 1)
        XCTAssertEqual(target.pasteCount, 0)
    }

    func testAXErrorAfterSuccessfulMutationDoesNotWriteTwice() async throws {
        let target = InsertionTargetStub()
        target.throwAfterWrite = true
        let captured = try target.capture()
        try await TextInsertService.shared.replaceSelection(with: "你好", captured: captured)
        XCTAssertEqual(target.state.value, "你好 world")
        XCTAssertEqual(target.writeCount, 1)
        XCTAssertEqual(target.pasteCount, 0)
    }

    func testPartialMutationNeverFallsBackToPaste() async throws {
        let target = InsertionTargetStub()
        target.writtenValue = "part world"
        let captured = try target.capture()
        await assertRejected(target, captured: captured)
        XCTAssertEqual(target.state.value, "part world")
        XCTAssertEqual(target.writeCount, 1)
        XCTAssertEqual(target.pasteCount, 0)
    }

    func testAXNoOpFallsBackToPaste() async throws {
        let target = InsertionTargetStub()
        target.axNoOp = true
        let captured = try target.capture()
        try await TextInsertService.shared.replaceSelection(with: "你好", captured: captured)
        XCTAssertEqual(target.state.value, "你好 world")
        XCTAssertEqual(target.writeCount, 1)
        XCTAssertEqual(target.pasteCount, 1)
    }

    func testAXRejectedWriteFallsBackToPasteWhenTargetUnchanged() async throws {
        let target = InsertionTargetStub()
        target.axRejects = true
        let captured = try target.capture()
        try await TextInsertService.shared.replaceSelection(with: "你好", captured: captured)
        XCTAssertEqual(target.state.value, "你好 world")
        XCTAssertEqual(target.writeCount, 1)
        XCTAssertEqual(target.pasteCount, 1)
    }

    func testPasteNoOpFailsClosedWithoutRepeat() async throws {
        let target = InsertionTargetStub()
        target.axNoOp = true
        target.pasteNoOp = true
        let captured = try target.capture()
        await assertRejected(target, captured: captured)
        XCTAssertEqual(target.state.value, "Hello world")
        XCTAssertEqual(target.writeCount, 1)
        XCTAssertEqual(target.pasteCount, 1)
    }

    func testPasteboardRestoredAfterPasteFallback() async throws {
        let success = InsertionTargetStub()
        success.axNoOp = true
        try await TextInsertService.shared.replaceSelection(with: "你好", captured: try success.capture())
        XCTAssertEqual(success.restoreCount, 1)

        let failed = InsertionTargetStub()
        failed.axNoOp = true
        failed.pasteNoOp = true
        await assertRejected(failed, captured: try failed.capture())
        XCTAssertEqual(failed.restoreCount, 1)
    }

    func testNewlineNormalizationCountsAsVerified() async throws {
        let target = InsertionTargetStub(value: "Hello\nworld", range: NSRange(location: 0, length: 5))
        let captured = try CapturedTextInsertion(
            target: target, selection: target.state, selectedText: "Hello"
        )
        target.writtenValue = "你好\r\nworld"
        try await TextInsertService.shared.replaceSelection(with: "你好", captured: captured)
        XCTAssertEqual(target.writeCount, 1)
        XCTAssertEqual(target.pasteCount, 0)
    }

    func testMissingCaptureNeverWrites() async {
        do {
            try await TextInsertService.shared.replaceSelection(with: "你好", captured: nil)
            XCTFail("未捕获目标时不能插入")
        } catch { }
    }

    func testClipboardTextMismatchNeverWrites() async throws {
        let target = InsertionTargetStub()
        let captured = try CapturedTextInsertion(
            target: target, selection: target.state, selectedText: "Hello", expectedClipboardText: "different"
        )
        await assertRejected(target, captured: captured)
        XCTAssertEqual(target.writeCount, 0)
        XCTAssertEqual(target.pasteCount, 0)
    }

    func testInvalidRangeAndTextMismatchCannotBeCaptured() throws {
        for range in [NSRange(location: -1, length: 5), NSRange(location: 0, length: 0), NSRange(location: Int.max, length: 1)] {
            let target = InsertionTargetStub(range: range)
            XCTAssertThrowsError(try target.capture())
        }
        let target = InsertionTargetStub()
        XCTAssertThrowsError(try CapturedTextInsertion(target: target, selection: target.state, selectedText: "other"))
    }

    private func assertRejected(_ target: InsertionTargetStub, captured: CapturedTextInsertion) async {
        do {
            try await TextInsertService.shared.replaceSelection(with: "你好", captured: captured)
            XCTFail("目标变化或写入不完整必须报错")
        } catch { }
    }
}

@MainActor
private final class InsertionTargetStub: TextInsertionTarget {
    var isFocused = true
    var state: TextInsertionSnapshot
    var writeCount = 0
    var pasteCount = 0
    var restoreCount = 0
    var throwAfterWrite = false
    var writtenValue: String?
    /// setter 被接受但静默不生效（Orca/微信等自定义控件的行为）
    var axNoOp = false
    /// setter 直接报错且未产生任何改写
    var axRejects = false
    /// 粘贴被接受但静默不生效
    var pasteNoOp = false

    init(value: String = "Hello world", range: NSRange = NSRange(location: 0, length: 5)) {
        state = TextInsertionSnapshot(value: value, range: range)
    }

    func capture() throws -> CapturedTextInsertion {
        try CapturedTextInsertion(target: self, selection: state, selectedText: "Hello")
    }

    func snapshot() throws -> TextInsertionSnapshot { state }

    func replaceSelectedText(_ text: String) throws {
        writeCount += 1
        if axRejects { throw TextInsertService.InsertError.insertionNotVerified }
        if axNoOp { return }
        apply(text: text)
        if throwAfterWrite { throw TextInsertService.InsertError.insertionNotVerified }
    }

    func replaceSelectedTextByPasting(_ text: String) async throws -> @MainActor () -> Void {
        pasteCount += 1
        if !pasteNoOp {
            apply(text: text)
        }
        return { self.restoreCount += 1 }
    }

    private func apply(text: String) {
        state.value = writtenValue ?? (state.value as NSString).replacingCharacters(in: state.range, with: text)
        state.range = NSRange(location: state.range.location + text.utf16.count, length: 0)
    }
}
