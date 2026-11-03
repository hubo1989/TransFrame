import Foundation
import AppKit
import Carbon.HIToolbox

/// Global hotkey configuration for capture shortcuts.
struct KeyboardShortcut: Equatable, Codable, Sendable {
    /// Virtual key code (Carbon key codes)
    let keyCode: UInt32

    /// Modifier flags (Cmd, Shift, Option, Control)
    let modifiers: UInt32

    // MARK: - Initialization

    init(keyCode: UInt32, modifiers: UInt32) {
        self.keyCode = keyCode
        self.modifiers = modifiers
    }

    /// Creates a shortcut from NSEvent modifier flags
    init(keyCode: UInt32, modifierFlags: NSEvent.ModifierFlags) {
        self.keyCode = keyCode
        self.modifiers = Self.carbonModifiers(from: modifierFlags)
    }

    // MARK: - Default Shortcuts

    /// Default full screen capture shortcut: Command + Shift + 3
    static let fullScreenDefault = KeyboardShortcut(
        keyCode: UInt32(kVK_ANSI_3),
        modifiers: UInt32(cmdKey | shiftKey)
    )

    /// Default selection capture shortcut: Command + Shift + 4
    static let selectionDefault = KeyboardShortcut(
        keyCode: UInt32(kVK_ANSI_4),
        modifiers: UInt32(cmdKey | shiftKey)
    )

    /// Default translation mode shortcut: Command + Shift + T
    static let translationModeDefault = KeyboardShortcut(
        keyCode: UInt32(kVK_ANSI_T),
        modifiers: UInt32(cmdKey | shiftKey)
    )

    /// Default text selection translation shortcut: Command + Shift + Y
    static let textSelectionTranslationDefault = KeyboardShortcut(
        keyCode: UInt32(kVK_ANSI_Y),
        modifiers: UInt32(cmdKey | shiftKey)
    )

    /// Default translate and insert shortcut: Command + Shift + I
    static let translateAndInsertDefault = KeyboardShortcut(
        keyCode: UInt32(kVK_ANSI_I),
        modifiers: UInt32(cmdKey | shiftKey)
    )

    /// Default screen color pick shortcut: Control + Command + C.
    /// Distinct from the system-wide Cmd+C copy: adds Control, keeping it in
    /// the same "family" as the capture hotkeys while avoiding the plain
    /// clipboard binding (spec §4.2 empty-state hint references this).
    static let pickColorDefault = KeyboardShortcut(
        keyCode: UInt32(kVK_ANSI_C),
        modifiers: UInt32(cmdKey | controlKey)
    )

    // MARK: - Validation

    /// Checks if the shortcut includes at least one modifier key
    var hasRequiredModifiers: Bool {
        let requiredMask = UInt32(cmdKey | controlKey | optionKey)
        return (modifiers & requiredMask) != 0
    }

    /// Validates this shortcut configuration
    var isValid: Bool {
        hasRequiredModifiers
    }

    // MARK: - Display

    /// Human-readable string representation (e.g., "Cmd+Shift+3")
    var displayString: String {
        var parts: [String] = []

        if modifiers & UInt32(controlKey) != 0 {
            parts.append("Ctrl")
        }
        if modifiers & UInt32(optionKey) != 0 {
            parts.append("Opt")
        }
        if modifiers & UInt32(shiftKey) != 0 {
            parts.append("Shift")
        }
        if modifiers & UInt32(cmdKey) != 0 {
            parts.append("Cmd")
        }

        if let keyString = Self.keyCodeToString(keyCode) {
            parts.append(keyString)
        }

        return parts.joined(separator: "+")
    }

    /// Symbol-based string representation (e.g., "^3")
    var symbolString: String {
        var symbols = ""

        if modifiers & UInt32(controlKey) != 0 {
            symbols += "^"
        }
        if modifiers & UInt32(optionKey) != 0 {
            symbols += "~"
        }
        if modifiers & UInt32(shiftKey) != 0 {
            symbols += "$"
        }
        if modifiers & UInt32(cmdKey) != 0 {
            symbols += "@"
        }

        if let keyString = Self.keyCodeToString(keyCode) {
            symbols += keyString
        }

        return symbols
    }

    // MARK: - Modifier Conversion

    /// Converts NSEvent.ModifierFlags to Carbon modifier mask
    static func carbonModifiers(from flags: NSEvent.ModifierFlags) -> UInt32 {
        var carbonMods: UInt32 = 0

        if flags.contains(.command) {
            carbonMods |= UInt32(cmdKey)
        }
        if flags.contains(.shift) {
            carbonMods |= UInt32(shiftKey)
        }
        if flags.contains(.option) {
            carbonMods |= UInt32(optionKey)
        }
        if flags.contains(.control) {
            carbonMods |= UInt32(controlKey)
        }

        return carbonMods
    }

    /// Converts Carbon modifier mask to NSEvent.ModifierFlags
    var nsModifierFlags: NSEvent.ModifierFlags {
        var flags: NSEvent.ModifierFlags = []

        if modifiers & UInt32(cmdKey) != 0 {
            flags.insert(.command)
        }
        if modifiers & UInt32(shiftKey) != 0 {
            flags.insert(.shift)
        }
        if modifiers & UInt32(optionKey) != 0 {
            flags.insert(.option)
        }
        if modifiers & UInt32(controlKey) != 0 {
            flags.insert(.control)
        }

        return flags
    }

    // MARK: - Key Code to String

    private static let keyCodeToStringMap: [Int: String] = [
        kVK_ANSI_0: "0", kVK_ANSI_1: "1", kVK_ANSI_2: "2", kVK_ANSI_3: "3", kVK_ANSI_4: "4",
        kVK_ANSI_5: "5", kVK_ANSI_6: "6", kVK_ANSI_7: "7", kVK_ANSI_8: "8", kVK_ANSI_9: "9",
        kVK_ANSI_A: "A", kVK_ANSI_B: "B", kVK_ANSI_C: "C", kVK_ANSI_D: "D", kVK_ANSI_E: "E",
        kVK_ANSI_F: "F", kVK_ANSI_G: "G", kVK_ANSI_H: "H", kVK_ANSI_I: "I", kVK_ANSI_J: "J",
        kVK_ANSI_K: "K", kVK_ANSI_L: "L", kVK_ANSI_M: "M", kVK_ANSI_N: "N", kVK_ANSI_O: "O",
        kVK_ANSI_P: "P", kVK_ANSI_Q: "Q", kVK_ANSI_R: "R", kVK_ANSI_S: "S", kVK_ANSI_T: "T",
        kVK_ANSI_U: "U", kVK_ANSI_V: "V", kVK_ANSI_W: "W", kVK_ANSI_X: "X", kVK_ANSI_Y: "Y",
        kVK_ANSI_Z: "Z", kVK_F1: "F1", kVK_F2: "F2", kVK_F3: "F3", kVK_F4: "F4", kVK_F5: "F5",
        kVK_F6: "F6", kVK_F7: "F7", kVK_F8: "F8", kVK_F9: "F9", kVK_F10: "F10", kVK_F11: "F11",
        kVK_F12: "F12", kVK_Space: "Space", kVK_Return: "Return", kVK_Tab: "Tab"
    ]

    /// The main key name for this shortcut
    var mainKey: String {
        Self.keyCodeToStringMap[Int(keyCode)] ?? "Key \(keyCode)"
    }

    /// Converts a virtual key code to its string representation
    private static func keyCodeToString(_ keyCode: UInt32) -> String? {
        keyCodeToStringMap[Int(keyCode)]
    }
}
