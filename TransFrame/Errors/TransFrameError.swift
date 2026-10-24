import Foundation
import CoreGraphics

/// Typed error enum for all TransFrame failure cases.
/// Provides localized descriptions and recovery suggestions for user-friendly error handling.
enum TransFrameError: LocalizedError, Sendable {
    // MARK: - Capture Errors

    /// Screen recording permission was denied by the user or system
    case permissionDenied

    /// The specified display is no longer available
    case displayNotFound(CGDirectDisplayID)

    /// The display was disconnected during capture
    case displayDisconnected(displayName: String)

    /// Screen capture operation failed with an underlying error
    case captureFailure(underlying: any Error)

    // MARK: - Export Errors

    /// The save location is not accessible or writable
    case invalidSaveLocation(URL)

    /// Insufficient disk space to save the screenshot
    case diskFull

    /// Failed to encode the image to the specified format
    case exportEncodingFailed(format: ExportFormat)

    // MARK: - Clipboard Errors

    /// Failed to write the screenshot to the system clipboard
    case clipboardWriteFailed

    // MARK: - Hotkey Errors

    /// Failed to register the global keyboard shortcut
    case hotkeyRegistrationFailed(keyCode: UInt32)

    /// The keyboard shortcut conflicts with another application
    case hotkeyConflict(existingApp: String?)

    // MARK: - OCR Errors

    /// The image provided for OCR is invalid
    case ocrInvalidImage

    /// Text recognition failed
    case ocrRecognitionFailed

    /// No text was found in the image
    case ocrNoTextFound

    // MARK: - LocalizedError Conformance

    var errorDescription: String? {
        switch self {
        case .permissionDenied:
            return NSLocalizedString("error.permission.denied", comment: "")
        case .displayNotFound:
            return NSLocalizedString("error.display.not.found", comment: "")
        case .displayDisconnected(let displayName):
            return String(format: NSLocalizedString("error.display.disconnected", comment: ""), displayName)
        case .captureFailure:
            return NSLocalizedString("error.capture.failed", comment: "")
        case .invalidSaveLocation:
            return NSLocalizedString("error.save.location.invalid", comment: "")
        case .diskFull:
            return NSLocalizedString("error.disk.full", comment: "")
        case .exportEncodingFailed:
            return NSLocalizedString("error.export.encoding.failed", comment: "")
        case .clipboardWriteFailed:
            return NSLocalizedString("error.clipboard.write.failed", comment: "")
        case .hotkeyRegistrationFailed:
            return NSLocalizedString("error.hotkey.registration.failed", comment: "")
        case .hotkeyConflict:
            return NSLocalizedString("error.hotkey.conflict", comment: "")
        case .ocrInvalidImage:
            return NSLocalizedString("error.ocr.invalid.image", comment: "")
        case .ocrRecognitionFailed:
            return NSLocalizedString("error.ocr.recognition.failed", comment: "")
        case .ocrNoTextFound:
            return NSLocalizedString("error.ocr.no.text.found", comment: "")
        }
    }

    var recoverySuggestion: String? {
        switch self {
        case .permissionDenied:
            return NSLocalizedString("error.permission.denied.recovery", comment: "")
        case .displayNotFound:
            return NSLocalizedString("error.display.not.found.recovery", comment: "")
        case .displayDisconnected:
            return NSLocalizedString("error.display.disconnected.recovery", comment: "")
        case .captureFailure:
            return NSLocalizedString("error.capture.failed.recovery", comment: "")
        case .invalidSaveLocation:
            return NSLocalizedString("error.save.location.invalid.recovery", comment: "")
        case .diskFull:
            return NSLocalizedString("error.disk.full.recovery", comment: "")
        case .exportEncodingFailed:
            return NSLocalizedString("error.export.encoding.failed.recovery", comment: "")
        case .clipboardWriteFailed:
            return NSLocalizedString("error.clipboard.write.failed.recovery", comment: "")
        case .hotkeyRegistrationFailed:
            return NSLocalizedString("error.hotkey.registration.failed.recovery", comment: "")
        case .hotkeyConflict:
            return NSLocalizedString("error.hotkey.conflict.recovery", comment: "")
        case .ocrInvalidImage:
            return NSLocalizedString("error.ocr.invalid.image.recovery", comment: "")
        case .ocrRecognitionFailed:
            return NSLocalizedString("error.ocr.recognition.failed.recovery", comment: "")
        case .ocrNoTextFound:
            return NSLocalizedString("error.ocr.no.text.found.recovery", comment: "")
        }
    }
}

// MARK: - Sendable Conformance for Underlying Error

extension TransFrameError {
    /// Creates a capture failure error with a sendable error description
    static func captureError(message: String) -> TransFrameError {
        .captureFailure(underlying: CaptureFailureError(message: message))
    }
}

/// A simple sendable error type for capture failures
struct CaptureFailureError: Error, Sendable {
    let message: String

    var localizedDescription: String { message }
}
