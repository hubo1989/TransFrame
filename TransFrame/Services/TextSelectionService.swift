import Foundation
import AppKit
import CoreGraphics
import ApplicationServices
import os

/// Result of text selection capture
struct TextSelectionResult {
    /// The captured selected text
    let text: String
    /// The name of the source application (if available)
    let sourceApplication: String?
    /// The bundle identifier of the source application (if available)
    let sourceBundleIdentifier: String?
    /// 触发时前台应用 pid，供无 AX 绑定时的降级粘贴锚定
    let sourceProcessID: pid_t?
    /// 仅在 translate-and-insert 中绑定；普通取词翻译不需要 AX 选区。
    @MainActor var insertionTarget: CapturedTextInsertion?
}

/// Service for capturing selected text from any application.
/// Uses clipboard-based capture with Cmd+C simulation to reliably get selected text.
actor TextSelectionService {
    private let logger = Logger.capture

    // MARK: - Types

    /// Errors that can occur during text selection capture
    enum CaptureError: LocalizedError, Sendable {
        /// No text was selected in the active application
        case noSelection
        /// Failed to simulate keyboard shortcut
        case keyboardSimulationFailed
        /// Failed to access clipboard
        case clipboardAccessFailed
        /// Failed to restore original clipboard content
        case clipboardRestoreFailed
        /// The operation timed out
        case timeout
        /// Accessibility permission is required but not granted
        case accessibilityPermissionDenied

        var errorDescription: String? {
            switch self {
            case .noSelection:
                return NSLocalizedString(
                    "error.text.selection.no.selection",
                    value: "No text is currently selected",
                    comment: ""
                )
            case .keyboardSimulationFailed:
                return NSLocalizedString(
                    "error.text.selection.keyboard.failed",
                    value: "Failed to simulate keyboard shortcut",
                    comment: ""
                )
            case .clipboardAccessFailed:
                return NSLocalizedString(
                    "error.text.selection.clipboard.access.failed",
                    value: "Failed to access clipboard",
                    comment: ""
                )
            case .clipboardRestoreFailed:
                return NSLocalizedString(
                    "error.text.selection.clipboard.restore.failed",
                    value: "Failed to restore original clipboard content",
                    comment: ""
                )
            case .timeout:
                return NSLocalizedString(
                    "error.text.selection.timeout",
                    value: "Text capture operation timed out",
                    comment: ""
                )
            case .accessibilityPermissionDenied:
                return NSLocalizedString(
                    "error.text.selection.accessibility.denied",
                    value: "Accessibility permission is required to capture text",
                    comment: ""
                )
            }
        }

        var recoverySuggestion: String? {
            switch self {
            case .noSelection:
                return NSLocalizedString(
                    "error.text.selection.no.selection.recovery",
                    value: "Select some text in any application and try again",
                    comment: ""
                )
            case .keyboardSimulationFailed, .clipboardAccessFailed:
                return NSLocalizedString(
                    "error.text.selection.general.recovery",
                    value: "Please try again",
                    comment: ""
                )
            case .clipboardRestoreFailed:
                return NSLocalizedString(
                    "error.text.selection.clipboard.restore.recovery",
                    value: "Your original clipboard content may have been replaced",
                    comment: ""
                )
            case .timeout:
                return NSLocalizedString(
                    "error.text.selection.timeout.recovery",
                    value: "The application may be busy. Please try again",
                    comment: ""
                )
            case .accessibilityPermissionDenied:
                return NSLocalizedString(
                    "error.text.selection.accessibility.denied.recovery",
                    value: "Grant accessibility permission in System Settings > Privacy & Security > Accessibility",
                    comment: ""
                )
            }
        }
    }

    // MARK: - Properties

    /// Time to wait for clipboard to be updated after Cmd+C
    private let clipboardWaitTimeout: TimeInterval

    /// Number of times to check clipboard before giving up
    private let clipboardCheckRetries: Int

    /// Delay between clipboard checks
    private let clipboardCheckInterval: TimeInterval

    // MARK: - Initialization

    init(
        clipboardWaitTimeout: TimeInterval = 2.0,
        clipboardCheckRetries: Int = 20,
        clipboardCheckInterval: TimeInterval = 0.1
    ) {
        self.clipboardWaitTimeout = clipboardWaitTimeout
        self.clipboardCheckRetries = clipboardCheckRetries
        self.clipboardCheckInterval = clipboardCheckInterval
    }

    // MARK: - Public API

    /// Captures currently selected text from the active application.
    /// - Returns: A TextSelectionResult containing the selected text and source application info
    /// - Throws: CaptureError if the capture fails
    func captureSelectedText() async throws -> TextSelectionResult {
        try await captureSelectedText(bindForInsertion: false)
    }

    func captureSelectedText(bindForInsertion: Bool) async throws -> TextSelectionResult {
        // Check accessibility permission
        guard AXIsProcessTrusted() else {
            throw CaptureError.accessibilityPermissionDenied
        }

        // Get source application info before capturing
        let sourceAppInfo = getActiveApplicationInfo()
        let insertionTarget = bindForInsertion ? await AXTextInsertionTarget.capture() : nil

        // AX 已捕获选中文本时直接使用：部分应用（如 WKWebView 编辑器）不响应合成 Cmd+C，
        // 且此路径完全不需要动剪贴板。Cmd+C 仅在无 AX 绑定的降级模式下使用。
        if let insertionTarget {
            return TextSelectionResult(
                text: insertionTarget.selectedText,
                sourceApplication: sourceAppInfo.name,
                sourceBundleIdentifier: sourceAppInfo.bundleIdentifier,
                sourceProcessID: sourceAppInfo.processIdentifier,
                insertionTarget: insertionTarget
            )
        }

        // Save current clipboard content
        let savedClipboard = PasteboardBackup.save()

        // Ensure clipboard is restored even if we throw below
        defer {
            PasteboardBackup.restore(savedClipboard)
        }

        // Clear clipboard to detect when new content is pasted
        NSPasteboard.general.clearContents()

        // Simulate Cmd+C to copy selected text
        try simulateCopyShortcut()

        // Wait for clipboard to be updated with selected text
        let capturedText = try await waitForClipboardUpdate(previousChangeCount: savedClipboard.changeCount)

        // Validate we got some text
        guard !capturedText.isEmpty else {
            throw CaptureError.noSelection
        }
        let verifiedTarget = await MainActor.run {
            insertionTarget?.matching(clipboardText: capturedText)
        }
        if bindForInsertion {
            logger.notice("插入目标绑定: \(verifiedTarget != nil ? "成功" : "失败", privacy: .public)")
        }

        return TextSelectionResult(
            text: capturedText,
            sourceApplication: sourceAppInfo.name,
            sourceBundleIdentifier: sourceAppInfo.bundleIdentifier,
            sourceProcessID: sourceAppInfo.processIdentifier,
            insertionTarget: verifiedTarget
        )
    }

    /// Checks if text selection capture is likely to work.
    /// Returns false if accessibility permission is not granted.
    var canCapture: Bool {
        AXIsProcessTrusted()
    }

    // MARK: - Private Helpers

    /// Information about the active application
    private struct ApplicationInfo {
        let name: String?
        let bundleIdentifier: String?
        let processIdentifier: pid_t?
    }

    /// Gets information about the currently active application.
    private func getActiveApplicationInfo() -> ApplicationInfo {
        let workspace = NSWorkspace.shared
        let frontmostApp = workspace.frontmostApplication

        return ApplicationInfo(
            name: frontmostApp?.localizedName,
            bundleIdentifier: frontmostApp?.bundleIdentifier,
            processIdentifier: frontmostApp?.processIdentifier
        )
    }

    /// Simulates Cmd+C keyboard shortcut to copy selected text.
    private func simulateCopyShortcut() throws {
        // Create Cmd+C key down event
        guard let keyDownEvent = CGEvent(
            keyboardEventSource: nil,
            virtualKey: 8, // kVK_ANSI_C
            keyDown: true
        ) else {
            throw CaptureError.keyboardSimulationFailed
        }

        // Create Cmd+C key up event
        guard let keyUpEvent = CGEvent(
            keyboardEventSource: nil,
            virtualKey: 8, // kVK_ANSI_C
            keyDown: false
        ) else {
            throw CaptureError.keyboardSimulationFailed
        }

        // Set Command flag for both events
        let cmdFlag = CGEventFlags.maskCommand
        keyDownEvent.flags = cmdFlag
        keyUpEvent.flags = cmdFlag

        // Post events
        let loc = CGEventTapLocation.cghidEventTap
        keyDownEvent.post(tap: loc)
        keyUpEvent.post(tap: loc)
    }

    /// Waits for clipboard to be updated with new content.
    /// - Parameter previousChangeCount: The previous clipboard change count
    /// - Returns: The new clipboard content
    /// - Throws: CaptureError if timeout or no change detected
    private func waitForClipboardUpdate(previousChangeCount: Int) async throws -> String {
        for _ in 0..<clipboardCheckRetries {
            try await Task.sleep(nanoseconds: UInt64(clipboardCheckInterval * 1_000_000_000))

            let pasteboard = NSPasteboard.general

            if pasteboard.changeCount != previousChangeCount {
                if let content = pasteboard.string(forType: .string), !content.isEmpty {
                    return content
                }
            }
        }

        throw CaptureError.noSelection
    }
}

// MARK: - Shared Instance

extension TextSelectionService {
    /// Shared instance for convenience
    static let shared = TextSelectionService()
}
