import XCTest
import Carbon.HIToolbox
@testable import TransFrame

// MARK: - ShortcutRecordingType Tests

/// Tests for ShortcutRecordingType enum
final class ShortcutRecordingTypeTests: XCTestCase {

    // MARK: - All Cases

    func testAllCasesExist() {
        let allCases: [ShortcutRecordingType] = [
            .fullScreen,
            .selection,
            .translationMode,
            .textSelectionTranslation,
            .translateAndInsert,
            .pickColor
        ]

        XCTAssertEqual(allCases.count, 6)
    }

    // MARK: - Equatable

    func testTypeEquality() {
        XCTAssertEqual(ShortcutRecordingType.fullScreen, ShortcutRecordingType.fullScreen)
        XCTAssertEqual(ShortcutRecordingType.selection, ShortcutRecordingType.selection)
        XCTAssertNotEqual(ShortcutRecordingType.fullScreen, ShortcutRecordingType.selection)
    }

    // MARK: - Switch Coverage

    func testSwitchExhaustiveness() {
        // This test verifies switch exhaustiveness
        let types: [ShortcutRecordingType] = [
            .fullScreen,
            .selection,
            .translationMode,
            .textSelectionTranslation,
            .translateAndInsert,
            .pickColor
        ]

        for type in types {
            // If switch is not exhaustive, compiler will error
            switch type {
            case .fullScreen:
                XCTAssertTrue(true)
            case .selection:
                XCTAssertTrue(true)
            case .translationMode:
                XCTAssertTrue(true)
            case .textSelectionTranslation:
                XCTAssertTrue(true)
            case .translateAndInsert:
                XCTAssertTrue(true)
            case .pickColor:
                XCTAssertTrue(true)
            }
        }
    }
}

// MARK: - SettingsViewModel Shortcut Tests (Partial)

/// Tests for SettingsViewModel shortcut-related functionality
/// Note: Full tests require @MainActor isolation
final class SettingsViewModelShortcutTests: XCTestCase {

    // MARK: - Shortcut Conflict Detection

    func testShortcutConflictDetection() {
        // Test the conflict detection logic indirectly
        // by verifying that equal shortcuts would conflict

        let shortcut1 = KeyboardShortcut(keyCode: 0x14, modifiers: UInt32(cmdKey | shiftKey))
        let shortcut2 = KeyboardShortcut(keyCode: 0x14, modifiers: UInt32(cmdKey | shiftKey))
        let shortcut3 = KeyboardShortcut(keyCode: 0x15, modifiers: UInt32(cmdKey | shiftKey))

        // Equal shortcuts should be equal
        XCTAssertEqual(shortcut1, shortcut2)

        // Different shortcuts should not be equal
        XCTAssertNotEqual(shortcut1, shortcut3)
    }

    // MARK: - Shortcut Validation

    func testValidShortcuts() {
        // Cmd modifier
        let cmdShortcut = KeyboardShortcut(keyCode: 0x00, modifiers: UInt32(cmdKey))
        XCTAssertTrue(cmdShortcut.isValid)

        // Control modifier
        let ctrlShortcut = KeyboardShortcut(keyCode: 0x00, modifiers: UInt32(controlKey))
        XCTAssertTrue(ctrlShortcut.isValid)

        // Option modifier
        let optShortcut = KeyboardShortcut(keyCode: 0x00, modifiers: UInt32(optionKey))
        XCTAssertTrue(optShortcut.isValid)

        // Shift only (invalid)
        let shiftOnlyShortcut = KeyboardShortcut(keyCode: 0x00, modifiers: UInt32(shiftKey))
        XCTAssertFalse(shiftOnlyShortcut.isValid)

        // No modifier (invalid)
        let noModShortcut = KeyboardShortcut(keyCode: 0x00, modifiers: 0)
        XCTAssertFalse(noModShortcut.isValid)
    }
}
