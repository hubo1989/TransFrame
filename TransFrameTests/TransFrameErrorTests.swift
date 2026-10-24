import XCTest
@testable import TransFrame

// MARK: - TransFrameError Tests

/// Tests for TransFrameError enum
final class TransFrameErrorTests: XCTestCase {

    // MARK: - Capture Errors

    func testPermissionDeniedErrorDescription() {
        let error = TransFrameError.permissionDenied

        XCTAssertNotNil(error.errorDescription)
        XCTAssertTrue(error.errorDescription!.count > 0)
        XCTAssertNotNil(error.recoverySuggestion)
    }

    func testDisplayNotFoundErrorDescription() {
        let displayID: CGDirectDisplayID = 12345
        let error = TransFrameError.displayNotFound(displayID)

        XCTAssertNotNil(error.errorDescription)
        XCTAssertNotNil(error.recoverySuggestion)
    }

    func testDisplayDisconnectedErrorDescription() {
        let error = TransFrameError.displayDisconnected(displayName: "External Monitor")

        XCTAssertNotNil(error.errorDescription)
        XCTAssertTrue(error.errorDescription!.contains("External Monitor"))
        XCTAssertNotNil(error.recoverySuggestion)
    }

    func testCaptureFailureErrorDescription() {
        let underlyingError = NSError(domain: "TestDomain", code: 1, userInfo: nil)
        let error = TransFrameError.captureFailure(underlying: underlyingError)

        XCTAssertNotNil(error.errorDescription)
        XCTAssertNotNil(error.recoverySuggestion)
    }

    // MARK: - Export Errors

    func testInvalidSaveLocationErrorDescription() {
        let url = URL(fileURLWithPath: "/nonexistent/path")
        let error = TransFrameError.invalidSaveLocation(url)

        XCTAssertNotNil(error.errorDescription)
        XCTAssertNotNil(error.recoverySuggestion)
    }

    func testDiskFullErrorDescription() {
        let error = TransFrameError.diskFull

        XCTAssertNotNil(error.errorDescription)
        XCTAssertNotNil(error.recoverySuggestion)
    }

    func testExportEncodingFailedErrorDescription() {
        let error = TransFrameError.exportEncodingFailed(format: .png)

        XCTAssertNotNil(error.errorDescription)
        XCTAssertNotNil(error.recoverySuggestion)
    }

    // MARK: - Clipboard Errors

    func testClipboardWriteFailedErrorDescription() {
        let error = TransFrameError.clipboardWriteFailed

        XCTAssertNotNil(error.errorDescription)
        XCTAssertNotNil(error.recoverySuggestion)
    }

    // MARK: - Hotkey Errors

    func testHotkeyRegistrationFailedErrorDescription() {
        let error = TransFrameError.hotkeyRegistrationFailed(keyCode: 0x14)

        XCTAssertNotNil(error.errorDescription)
        XCTAssertNotNil(error.recoverySuggestion)
    }

    func testHotkeyConflictErrorDescription() {
        let error = TransFrameError.hotkeyConflict(existingApp: "OtherApp")

        XCTAssertNotNil(error.errorDescription)
        XCTAssertNotNil(error.recoverySuggestion)
    }

    // MARK: - OCR Errors

    func testOCRInvalidImageErrorDescription() {
        let error = TransFrameError.ocrInvalidImage

        XCTAssertNotNil(error.errorDescription)
        XCTAssertNotNil(error.recoverySuggestion)
    }

    func testOCRRecognitionFailedErrorDescription() {
        let error = TransFrameError.ocrRecognitionFailed

        XCTAssertNotNil(error.errorDescription)
        XCTAssertNotNil(error.recoverySuggestion)
    }

    func testOCRNoTextFoundErrorDescription() {
        let error = TransFrameError.ocrNoTextFound

        XCTAssertNotNil(error.errorDescription)
        XCTAssertNotNil(error.recoverySuggestion)
    }

    // MARK: - Sendable Conformance

    func testErrorIsSendable() {
        // Verify Sendable conformance by using in a Sendable closure
        let error = TransFrameError.permissionDenied

        let sendableClosure: @Sendable () -> TransFrameError = {
            return error
        }

        // If this compiles and runs, the error is Sendable
        if case .permissionDenied = sendableClosure() {
            XCTAssertTrue(true)
        } else {
            XCTFail("Expected permissionDenied case")
        }
    }

    // MARK: - CaptureFailureError Helper

    func testCaptureErrorHelper() {
        let error = TransFrameError.captureError(message: "Test error message")

        if case .captureFailure(let underlying) = error {
            XCTAssertTrue(underlying is CaptureFailureError)
            XCTAssertEqual((underlying as? CaptureFailureError)?.message, "Test error message")
        } else {
            XCTFail("Expected captureFailure case")
        }
    }
}

// MARK: - CaptureFailureError Tests

/// Tests for CaptureFailureError struct
final class CaptureFailureErrorTests: XCTestCase {

    func testLocalizedDescription() {
        let error = CaptureFailureError(message: "Capture failed")

        XCTAssertEqual(error.localizedDescription, "Capture failed")
    }

    func testSendableConformance() {
        let error = CaptureFailureError(message: "Test")

        // If this compiles, CaptureFailureError conforms to Sendable
        let sendableClosure: @Sendable () -> CaptureFailureError = {
            return error
        }

        XCTAssertEqual(sendableClosure().message, "Test")
    }
}
