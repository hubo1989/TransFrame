import XCTest
@testable import TransFrame

@MainActor
final class TextInsertionSafetyTests: XCTestCase {
    func testChangedFocusNeverWrites() async throws {
        let target = InsertionTargetStub()
        let captured = try target.capture()
        target.isFocused = false
        assertRejected(target, captured: captured)
        XCTAssertEqual(target.writeCount, 0)
    }

    func testCollapsedOrMovedSelectionNeverWrites() async throws {
        for range in [NSRange(location: 5, length: 0), NSRange(location: 1, length: 5)] {
            let target = InsertionTargetStub()
            let captured = try target.capture()
            target.state.range = range
            assertRejected(target, captured: captured)
            XCTAssertEqual(target.writeCount, 0)
        }
    }

    func testChangedDocumentNeverWritesEvenWithSameSelectedText() async throws {
        let target = InsertionTargetStub()
        let captured = try target.capture()
        target.state.value = "Hello changed"
        assertRejected(target, captured: captured)
        XCTAssertEqual(target.writeCount, 0)
    }

    func testReplacesOnlyCapturedUTF16Range() async throws {
        let target = InsertionTargetStub(value: "👩🏽‍💻 Hello world", range: NSRange(location: 8, length: 5))
        let captured = try target.capture()
        try TextInsertService.shared.replaceSelection(with: "你好👨‍👩‍👧‍👦", captured: captured)
        XCTAssertEqual(target.state.value, "👩🏽‍💻 你好👨‍👩‍👧‍👦 world")
        XCTAssertEqual(target.writeCount, 1)
    }

    func testAXErrorAfterSuccessfulMutationDoesNotWriteTwice() async throws {
        let target = InsertionTargetStub()
        target.throwAfterWrite = true
        let captured = try target.capture()
        try TextInsertService.shared.replaceSelection(with: "你好", captured: captured)
        XCTAssertEqual(target.state.value, "你好 world")
        XCTAssertEqual(target.writeCount, 1)
    }

    func testPartialMutationOrNoMutationDoesNotRetry() async throws {
        for value in ["part world", "Hello world"] {
            let target = InsertionTargetStub()
            target.writtenValue = value
            let captured = try target.capture()
            assertRejected(target, captured: captured)
            XCTAssertEqual(target.state.value, value)
            XCTAssertEqual(target.writeCount, 1)
        }
    }

    func testMissingCaptureNeverWrites() {
        do {
            try TextInsertService.shared.replaceSelection(with: "你好", captured: nil)
            XCTFail("未捕获目标时不能插入")
        } catch { }
    }

    func testClipboardTextMismatchNeverWrites() throws {
        let target = InsertionTargetStub()
        let captured = try CapturedTextInsertion(
            target: target, selection: target.state, selectedText: "Hello", expectedClipboardText: "different"
        )
        assertRejected(target, captured: captured)
        XCTAssertEqual(target.writeCount, 0)
    }

    func testInvalidRangeAndTextMismatchCannotBeCaptured() throws {
        for range in [NSRange(location: -1, length: 5), NSRange(location: 0, length: 0), NSRange(location: Int.max, length: 1)] {
            let target = InsertionTargetStub(range: range)
            XCTAssertThrowsError(try target.capture())
        }
        let target = InsertionTargetStub()
        XCTAssertThrowsError(try CapturedTextInsertion(target: target, selection: target.state, selectedText: "other"))
    }

    private func assertRejected(_ target: InsertionTargetStub, captured: CapturedTextInsertion) {
        do {
            try TextInsertService.shared.replaceSelection(with: "你好", captured: captured)
            XCTFail("目标变化或写入不完整必须报错")
        } catch { }
    }
}

@MainActor
private final class InsertionTargetStub: TextInsertionTarget {
    var isFocused = true
    var state: TextInsertionSnapshot
    var writeCount = 0
    var throwAfterWrite = false
    var writtenValue: String?

    init(value: String = "Hello world", range: NSRange = NSRange(location: 0, length: 5)) {
        state = TextInsertionSnapshot(value: value, range: range)
    }

    func capture() throws -> CapturedTextInsertion {
        try CapturedTextInsertion(target: self, selection: state, selectedText: "Hello")
    }

    func snapshot() throws -> TextInsertionSnapshot { state }

    func replaceSelectedText(_ text: String) throws {
        writeCount += 1
        state.value = writtenValue ?? (state.value as NSString).replacingCharacters(in: state.range, with: text)
        state.range = NSRange(location: state.range.location + text.utf16.count, length: 0)
        if throwAfterWrite { throw TextInsertService.InsertError.insertionNotVerified }
    }
}
