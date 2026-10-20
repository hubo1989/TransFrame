//
//  HotkeyCoordinator.swift
//  TransFrame
//
//  Created during architecture refactoring - extracts hotkey management from AppDelegate
//
//  ## Responsibilities
//  - Register all global hotkeys on app launch
//  - Unregister hotkeys on app termination
//  - Update hotkeys when settings change
//
//  ## Usage
//  Access via AppDelegate.hotkeyCoordinator:
//  ```swift
//  await appDelegate.hotkeyCoordinator?.registerAllHotkeys()
//  await appDelegate.hotkeyCoordinator?.unregisterAllHotkeys()
//  appDelegate.hotkeyCoordinator?.updateHotkeys()
//  ```
//

import AppKit
import os

/// Coordinates global hotkey management:
/// registration, unregistration, and updates based on settings changes.
///
/// This coordinator was extracted from AppDelegate as part of the architecture
/// refactoring to improve separation of concerns and testability.
@MainActor
final class HotkeyCoordinator {
    // MARK: - Types

    /// Types of hotkeys managed by the coordinator
    enum HotkeyType: String, CaseIterable {
        case fullScreen
        case selection
        case translationMode
        case textSelectionTranslation
        case translateAndInsert
    }

    // MARK: - Properties

    /// Reference to app delegate for action routing
    private weak var appDelegate: AppDelegate?

    /// Registered hotkey references by type
    private var registrations: [HotkeyType: HotkeyManager.Registration] = [:]
    private var updateTask: Task<Void, Never>?

    private let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "TransFrame", category: "HotkeyCoordinator")

    // MARK: - Initialization

    init(appDelegate: AppDelegate) {
        self.appDelegate = appDelegate
    }

    // MARK: - Public API

    /// Registers all hotkeys based on current settings
    func registerAllHotkeys() async {
        logger.info("Registering all hotkeys")

        // Unregister existing hotkeys first
        await unregisterAllHotkeys()

        let settings = AppSettings.shared

        // Register full screen capture hotkey
        await registerHotkey(
            type: .fullScreen,
            shortcut: settings.fullScreenShortcut,
            description: "Full Screen Capture"
        ) { [weak self] in
            Task { @MainActor in
                self?.appDelegate?.captureCoordinator?.captureFullScreen()
            }
        }

        // Register selection capture hotkey
        await registerHotkey(
            type: .selection,
            shortcut: settings.selectionShortcut,
            description: "Selection Capture"
        ) { [weak self] in
            Task { @MainActor in
                self?.appDelegate?.captureCoordinator?.captureSelection()
            }
        }

        // Register translation mode hotkey
        await registerHotkey(
            type: .translationMode,
            shortcut: settings.translationModeShortcut,
            description: "Translation Mode"
        ) { [weak self] in
            Task { @MainActor in
                self?.appDelegate?.captureCoordinator?.startTranslationMode()
            }
        }

        // Register text selection translation hotkey
        await registerHotkey(
            type: .textSelectionTranslation,
            shortcut: settings.textSelectionTranslationShortcut,
            description: "Text Selection Translation"
        ) { [weak self] in
            Task { @MainActor in
                self?.appDelegate?.textTranslationCoordinator?.translateSelectedText()
            }
        }

        // Register translate and insert hotkey
        await registerHotkey(
            type: .translateAndInsert,
            shortcut: settings.translateAndInsertShortcut,
            description: "Translate and Insert"
        ) { [weak self] in
            Task { @MainActor in
                self?.appDelegate?.textTranslationCoordinator?.translateClipboardAndInsert()
            }
        }

        logger.info("All hotkeys registered successfully")
    }

    /// Unregisters all hotkeys
    func unregisterAllHotkeys() async {
        logger.info("Unregistering all hotkeys")

        await HotkeyManager.shared.unregisterAll()
        registrations.removeAll()
    }

    /// Updates hotkeys when settings change
    func updateHotkeys() {
        updateTask?.cancel()
        updateTask = Task {
            logger.info("Hotkey settings changed, re-registering")
            await registerAllHotkeys()
        }
    }

    /// Returns the currently registered hotkey for a type
    func registration(for type: HotkeyType) -> HotkeyManager.Registration? {
        registrations[type]
    }

    /// Returns all currently registered hotkey types
    var registeredTypes: [HotkeyType] {
        Array(registrations.keys)
    }

    // MARK: - Private Helpers

    /// Registers a single hotkey
    private func registerHotkey(
        type: HotkeyType,
        shortcut: KeyboardShortcut,
        description: String,
        handler: @escaping @Sendable () -> Void
    ) async {
        do {
            let registration = try await HotkeyManager.shared.register(
                shortcut: shortcut,
                handler: handler
            )
            registrations[type] = registration
            logger.debug("Registered hotkey: \(description) -> \(shortcut.displayString)")
        } catch {
            logger.error("Failed to register hotkey \(type.rawValue): \(error.localizedDescription)")
        }
    }
}
