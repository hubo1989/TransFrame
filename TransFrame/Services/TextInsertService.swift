//
//  TextInsertService.swift
//  TransFrame
//
//  Created for US-005: Add copy and insert buttons to translation popup
//

import AppKit
import Foundation
import CoreGraphics
import ApplicationServices
import os

/// Service for inserting text into the currently focused input field.
/// Uses CGEvent keyboard simulation to type text character by character.
actor TextInsertService {
    private let logger = Logger.general

    // MARK: - Types

    /// Errors that can occur during text insertion
    enum InsertError: LocalizedError, Sendable {
        /// Failed to create keyboard event
        case eventCreationFailed
        /// Accessibility permission is required but not granted
        case accessibilityPermissionDenied
        /// Text contains characters that cannot be typed
        case unsupportedCharacters(String)
        /// 翻译期间原控件或原选区已变化。
        case targetChanged
        /// 未捕获到可安全替换的 AX 文本选区。
        case targetUnavailable
        /// AX 写入后无法确认目标内容，绝不自动重复输入。
        case insertionNotVerified

        var errorDescription: String? {
            switch self {
            case .eventCreationFailed:
                return NSLocalizedString(
                    "error.text.insert.event.failed",
                    value: "Failed to create keyboard event",
                    comment: ""
                )
            case .accessibilityPermissionDenied:
                return NSLocalizedString(
                    "error.text.insert.accessibility.denied",
                    value: "Accessibility permission is required to insert text",
                    comment: ""
                )
            case .unsupportedCharacters(let chars):
                return NSLocalizedString(
                    "error.text.insert.unsupported.chars",
                    value: "Cannot type characters: \(chars)",
                    comment: ""
                )
            case .targetChanged:
                return NSLocalizedString(
                    "error.text.insert.target.changed",
                    value: "The original selection or focused app changed. The translation was not inserted.",
                    comment: ""
                )
            case .targetUnavailable:
                return NSLocalizedString(
                    "error.text.insert.target.unavailable",
                    value: "Cannot safely replace this selection. The translation was not inserted.",
                    comment: ""
                )
            case .insertionNotVerified:
                return NSLocalizedString(
                    "error.text.insert.not.verified",
                    value: "Text insertion could not be verified. Check the original app before retrying.",
                    comment: ""
                )
            }
        }

        var recoverySuggestion: String? {
            switch self {
            case .eventCreationFailed:
                return NSLocalizedString(
                    "error.text.insert.event.failed.recovery",
                    value: "Please try again",
                    comment: ""
                )
            case .accessibilityPermissionDenied:
                return NSLocalizedString(
                    "error.text.insert.accessibility.denied.recovery",
                    value: "Grant accessibility permission in System Settings > Privacy & Security > Accessibility",
                    comment: ""
                )
            case .unsupportedCharacters:
                return NSLocalizedString(
                    "error.text.insert.unsupported.chars.recovery",
                    value: "Some characters cannot be typed with keyboard simulation",
                    comment: ""
                )
            case .targetChanged:
                return NSLocalizedString(
                    "error.text.insert.target.changed.recovery",
                    value: "Select the text again to translate and insert",
                    comment: ""
                )
            case .targetUnavailable:
                return NSLocalizedString(
                    "error.text.insert.target.unavailable.recovery",
                    value: "Copy the translation manually or select an editable text field",
                    comment: ""
                )
            case .insertionNotVerified:
                return NSLocalizedString(
                    "error.text.insert.not.verified.recovery",
                    value: "Do not retry blindly; check whether some text was already inserted",
                    comment: ""
                )
            }
        }
    }

    // MARK: - Properties

    /// Delay between keystrokes in seconds (for reliability)
    private let keystrokeDelay: TimeInterval

    // MARK: - Initialization

    init(keystrokeDelay: TimeInterval = 0.01) {
        self.keystrokeDelay = keystrokeDelay
    }

    // MARK: - Public API

    /// Inserts text into the currently focused input field by simulating keyboard input.
    /// - Parameter text: The text to insert
    /// - Throws: InsertError if the insertion fails
    func insertText(_ text: String) async throws {
        // Check accessibility permission
        guard AXIsProcessTrusted() else {
            throw InsertError.accessibilityPermissionDenied
        }

        guard !text.isEmpty else { return }

        // Get the event source
        guard let source = CGEventSource(stateID: .hidSystemState) else {
            throw InsertError.eventCreationFailed
        }

        // Type each character
        for character in text {
            try await typeCharacter(character, source: source)
            // Small delay between characters for reliability
            try await Task.sleep(nanoseconds: UInt64(keystrokeDelay * 1_000_000_000))
        }
    }

    /// 只替换捕获时的 AX 目标。nil 表示无法核验，禁止回退键盘输入。
    @MainActor
    func replaceSelection(with text: String, captured: CapturedTextInsertion?) async throws {
        guard let captured else { throw InsertError.targetUnavailable }
        try await captured.replace(with: text)
    }

    /// 无 AX 绑定（如 Electron 无障碍未启用）时的降级插入：
    /// 以「触发时前台应用 pid + Cmd+C 取词成功」为锚，核对前台未变后用 Cmd+V 粘贴。
    /// 无法做读回验证；同应用内切换输入框的误贴风险与手动 Cmd+V 相同。
    @MainActor
    func pasteToCapturedFrontmost(_ text: String, anchorPID: pid_t) async throws {
        guard !text.isEmpty else { throw InsertError.insertionNotVerified }
        guard NSWorkspace.shared.frontmostApplication?.processIdentifier == anchorPID else {
            Logger.textInsertion.notice("降级粘贴失败: 前台应用已变化 anchorPid=\(anchorPID)")
            throw InsertError.targetChanged
        }
        let restorePasteboard = try ClipboardPaste.paste(text)
        // defer 覆盖取消路径：取消时也必须恢复剪贴板
        defer { restorePasteboard() }
        // 不可取消的等待：即使任务被取消也等满窗口，防止剪贴板在目标消费前被恢复
        await NonCancellableWait.wait(nanoseconds: 400_000_000)
        if Task.isCancelled {
            Logger.textInsertion.notice("降级粘贴: 任务已取消，等待窗口结束后已恢复剪贴板 anchorPid=\(anchorPID)")
        } else {
            Logger.textInsertion.notice("降级粘贴完成: 无AX验证 anchorPid=\(anchorPID)")
        }
    }

    /// Deletes the currently selected text and inserts new text via Unicode events.
    /// This bypasses the input method and inserts text directly.
    /// - Parameter text: The text to insert after deleting the selection
    /// - Throws: InsertError if the operation fails
    func deleteSelectionAndInsert(_ text: String) async throws {
        // Check accessibility permission
        guard AXIsProcessTrusted() else {
            throw InsertError.accessibilityPermissionDenied
        }

        // Get the event source
        guard let source = CGEventSource(stateID: .hidSystemState) else {
            throw InsertError.eventCreationFailed
        }

        // Step 1: Delete selected text by simulating Delete key
        try postDeleteKey(source: source)

        // Longer delay after delete to ensure focus is maintained
        try await Task.sleep(nanoseconds: 100_000_000) // 100ms

        // Step 2: Insert text using Unicode events (bypasses input method)
        try await insertUnicodeText(text, source: source)

        #if DEBUG
        logger.debug("Inserted \(text.count, privacy: .public) characters via Unicode events")
        #endif
    }

    /// Inserts text using Unicode keyboard events, bypassing input methods
    private func insertUnicodeText(_ text: String, source: CGEventSource) async throws {
        // Process text in chunks that can be sent via keyboardSetUnicodeString
        // The maximum safe chunk is around 20 characters
        let chunkSize = 20
        let characters = Array(text)

        #if DEBUG
        logger.debug(
            "Starting Unicode insertion of \(characters.count, privacy: .public) chars in \(max(1, (characters.count + chunkSize - 1) / chunkSize), privacy: .public) chunk(s)"
        )
        #endif

        for i in stride(from: 0, to: characters.count, by: chunkSize) {
            let endIndex = min(i + chunkSize, characters.count)
            let chunk = characters[i..<endIndex]
            let chunkText = String(chunk)

            // Convert to UTF-16 for keyboardSetUnicodeString
            let utf16Chars = Array(chunkText.utf16)

            guard let keyDown = CGEvent(
                keyboardEventSource: source,
                virtualKey: 0,
                keyDown: true
            ),
            let keyUp = CGEvent(
                keyboardEventSource: source,
                virtualKey: 0,
                keyDown: false
            ) else {
                throw InsertError.eventCreationFailed
            }

            var mutableChars = utf16Chars
            keyDown.keyboardSetUnicodeString(stringLength: utf16Chars.count, unicodeString: &mutableChars)
            keyUp.keyboardSetUnicodeString(stringLength: utf16Chars.count, unicodeString: &mutableChars)

            let loc = CGEventTapLocation.cghidEventTap
            keyDown.post(tap: loc)
            keyUp.post(tap: loc)

            #if DEBUG
            // Log metadata only, not actual content
            logger.debug("Posted chunk \(i / chunkSize + 1, privacy: .public): \(utf16Chars.count, privacy: .public) UTF-16 chars")
            #endif

            // Small delay between chunks
            if i + chunkSize < characters.count {
                try await Task.sleep(nanoseconds: UInt64(keystrokeDelay * 1_000_000_000))
            }
        }
    }

    /// Posts a Delete key event to remove selected text
    private func postDeleteKey(source: CGEventSource) throws {
        // Delete key code is 51 on macOS
        guard let keyDown = CGEvent(
            keyboardEventSource: source,
            virtualKey: 51,
            keyDown: true
        ),
        let keyUp = CGEvent(
            keyboardEventSource: source,
            virtualKey: 51,
            keyDown: false
        ) else {
            throw InsertError.eventCreationFailed
        }

        let loc = CGEventTapLocation.cghidEventTap
        keyDown.post(tap: loc)
        keyUp.post(tap: loc)
    }

    /// Checks if text insertion is likely to work.
    /// Returns false if accessibility permission is not granted.
    var canInsert: Bool {
        AXIsProcessTrusted()
    }

    // MARK: - Private Helpers

    /// Types a single character using CGEvent.
    /// - Parameters:
    ///   - character: The character to type
    ///   - source: The CGEventSource to use
    /// - Throws: InsertError if typing fails
    private func typeCharacter(_ character: Character, source: CGEventSource) async throws {
        // Get the UTF-16 code unit for the character
        guard let scalar = character.unicodeScalars.first else {
            // Skip characters we can't type
            return
        }

        // Create the keyboard events
        // For special characters, we need to use Unicode input
        let keyCode = keyCodeForCharacter(character)

        if keyCode != nil {
            // Use key code for ASCII characters
            try postKeyEvent(keyCode: keyCode!, source: source)
        } else {
            // Use Unicode input for non-ASCII characters
            try postUnicodeEvent(character: scalar, source: source)
        }
    }

    /// Posts a keyboard event for the given key code.
    /// - Parameters:
    ///   - keyCode: The key code to post
    ///   - source: The CGEventSource to use
    /// - Throws: InsertError if event creation fails
    private func postKeyEvent(keyCode: CGKeyCode, source: CGEventSource) throws {
        // Create key down event
        guard let keyDown = CGEvent(
            keyboardEventSource: source,
            virtualKey: keyCode,
            keyDown: true
        ) else {
            throw InsertError.eventCreationFailed
        }

        // Create key up event
        guard let keyUp = CGEvent(
            keyboardEventSource: source,
            virtualKey: keyCode,
            keyDown: false
        ) else {
            throw InsertError.eventCreationFailed
        }

        // Post events
        let loc = CGEventTapLocation.cghidEventTap
        keyDown.post(tap: loc)
        keyUp.post(tap: loc)
    }

    /// Posts a Unicode input event for non-ASCII characters.
    /// - Parameters:
    ///   - character: The Unicode scalar to type
    ///   - source: The CGEventSource to use
    /// - Throws: InsertError if event creation fails
    private func postUnicodeEvent(character: UnicodeScalar, source: CGEventSource) throws {
        guard let keyDown = CGEvent(
            keyboardEventSource: source,
            virtualKey: 0,
            keyDown: true
        ) else {
            throw InsertError.eventCreationFailed
        }

        // Handle surrogate pairs for characters outside BMP (e.g., emoji)
        let utf16 = String(character).utf16
        let count = utf16.count
        var chars = Array(utf16)

        keyDown.keyboardSetUnicodeString(stringLength: count, unicodeString: &chars)

        guard let keyUp = CGEvent(
            keyboardEventSource: source,
            virtualKey: 0,
            keyDown: false
        ) else {
            throw InsertError.eventCreationFailed
        }

        keyUp.keyboardSetUnicodeString(stringLength: count, unicodeString: &chars)

        let loc = CGEventTapLocation.cghidEventTap
        keyDown.post(tap: loc)
        keyUp.post(tap: loc)
    }

    /// Static mapping of ASCII characters to key codes (US keyboard layout)
    /// Using a dictionary reduces cyclomatic complexity compared to a large switch
    private static let keyCodeMap: [Character: CGKeyCode] = [
        "a": 0, "A": 0, "s": 1, "S": 1, "d": 2, "D": 2, "f": 3, "F": 3,
        "h": 4, "H": 4, "g": 5, "G": 5, "z": 6, "Z": 6, "x": 7, "X": 7,
        "c": 8, "C": 8, "v": 9, "V": 9, "b": 11, "B": 11, "q": 12, "Q": 12,
        "w": 13, "W": 13, "e": 14, "E": 14, "r": 15, "R": 15, "y": 16, "Y": 16,
        "t": 17, "T": 17, "1": 18, "!": 18, "2": 19, "@": 19, "3": 20, "#": 20,
        "4": 21, "$": 21, "6": 22, "^": 22, "5": 23, "%": 23, "=": 24, "+": 24,
        "9": 25, "(": 25, "7": 26, "&": 26, "-": 27, "_": 27, "8": 28, "*": 28,
        "0": 29, ")": 29, "]": 30, "}": 30, "o": 31, "O": 31, "u": 32, "U": 32,
        "[": 33, "{": 33, "i": 34, "I": 34, "p": 35, "P": 35, "\n": 36, "\r": 36,
        "l": 37, "L": 37, "j": 38, "J": 38, "'": 39, "\"": 39, "k": 40, "K": 40,
        ";": 41, ":": 41, "\\": 42, "|": 42, ",": 43, "<": 43, "/": 44, "?": 44,
        "n": 45, "N": 45, "m": 46, "M": 46, ".": 47, ">": 47, " ": 49,
        "`": 50, "~": 50
    ]

    /// Returns the key code for a given ASCII character, or nil for non-ASCII.
    ///
    /// This method provides key codes based on the US keyboard layout for ASCII characters.
    /// For non-ASCII characters (including international characters), the system falls back
    /// to Unicode input via `postUnicodeEvent`, which works correctly regardless of the
    /// current keyboard layout.
    ///
    /// - Parameter character: The character to get the key code for
    /// - Returns: The CGKeyCode for the character, or nil if not an ASCII character
    private func keyCodeForCharacter(_ character: Character) -> CGKeyCode? {
        return Self.keyCodeMap[character]
    }
}

// MARK: - Shared Instance

extension TextInsertService {
    /// Shared instance for convenience
    static let shared = TextInsertService()
}


enum NonCancellableWait {
    /// 取消不中断的等待：Task.sleep 在任务取消时会立即抛出，
    /// 而粘贴后的剪贴板消费窗口必须等满（恢复早于消费会损坏用户剪贴板）。
    static func wait(nanoseconds: UInt64) async {
        await withCheckedContinuation { continuation in
            Task.detached(priority: .utility) {
                usleep(useconds_t(nanoseconds / 1_000))
                continuation.resume()
            }
        }
    }
}

/// 剪贴板 + 合成 Cmd+V 的共享粘贴实现。
/// 返回恢复剪贴板的闭包，调用方必须在读回验证或固定等待结束后调用，
/// 避免目标应用延迟消费剪贴板时拿到旧内容。
enum ClipboardPaste {
    @MainActor
    static func paste(_ text: String) throws -> @MainActor () -> Void {
        guard AXIsProcessTrusted() else { throw TextInsertService.InsertError.accessibilityPermissionDenied }
        let backup = PasteboardBackup.save()
        let pasteboard = NSPasteboard.general
        pasteboard.clearContents()
        guard pasteboard.setString(text, forType: .string) else {
            PasteboardBackup.restore(backup)
            throw TextInsertService.InsertError.eventCreationFailed
        }
        guard let source = CGEventSource(stateID: .hidSystemState),
              let keyDown = CGEvent(keyboardEventSource: source, virtualKey: 9, keyDown: true), // kVK_ANSI_V
              let keyUp = CGEvent(keyboardEventSource: source, virtualKey: 9, keyDown: false) else {
            PasteboardBackup.restore(backup)
            throw TextInsertService.InsertError.eventCreationFailed
        }
        keyDown.flags = .maskCommand
        keyUp.flags = .maskCommand
        keyDown.post(tap: .cghidEventTap)
        keyUp.post(tap: .cghidEventTap)
        return { PasteboardBackup.restore(backup) }
    }
}
