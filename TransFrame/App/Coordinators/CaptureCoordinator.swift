//
//  CaptureCoordinator.swift
//  TransFrame
//
//  Created during architecture refactoring - extracts capture logic from AppDelegate
//
//  ## Responsibilities
//  - Full screen capture: Captures entire display via CaptureManager
//  - Selection capture: Shows overlay for user to select region
//  - Translation mode: Captures region and initiates translation flow
//
//  ## Usage
//  Access via AppDelegate.captureCoordinator:
//  ```swift
//  appDelegate.captureCoordinator?.captureFullScreen()
//  appDelegate.captureCoordinator?.captureSelection()
//  appDelegate.captureCoordinator?.startTranslationMode()
//  ```
//

import AppKit
import os

/// Coordinates all capture-related functionality:
/// full screen capture, selection capture, and translation mode capture.
///
/// This coordinator was extracted from AppDelegate as part of the architecture
/// refactoring to improve separation of concerns and testability.
@MainActor
final class CaptureCoordinator {
    // MARK: - Properties

    /// Reference to app delegate for action routing
    private weak var appDelegate: AppDelegate?

    /// Whether a capture operation is currently in progress
    private var isCaptureInProgress = false

    /// Display selector for multi-display support
    private let displaySelector = DisplaySelector()

    private let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "TransFrame", category: "CaptureCoordinator")

    // MARK: - Initialization

    init(appDelegate: AppDelegate) {
        self.appDelegate = appDelegate
        setupSessionCallbacks()
    }

    private func setupSessionCallbacks() {
        let session = CaptureSessionController.shared

        session.onCaptureComplete = { document in
            PreviewWindowController.shared.showPreview(for: document.toLegacyScreenshot())
        }

        session.onTranslationRequested = { document in
            // Route into the preview window's embedded side panel — no external window
            PreviewWindowController.shared.showPreview(
                for: document.toLegacyScreenshot(),
                autoTriggerTranslate: true
            )
        }

        session.onOCRRequested = { document in
            PreviewWindowController.shared.showPreview(for: document.toLegacyScreenshot(), autoTriggerOCR: true)
        }
    }

    // MARK: - Public API

    /// Triggers a full screen capture
    func captureFullScreen() {
        logger.info("Full screen capture triggered via CaptureSessionController")
        Task {
            guard await CaptureManager.shared.hasPermission else {
                PermissionManager.shared.ensureScreenRecordingPermission()
                return
            }
            CaptureSessionController.shared.startCapture(mode: .fullScreen)
        }
    }

    /// Triggers a selection capture with instant frozen screen
    func captureSelection() {
        logger.info("Selection capture triggered with instant freeze")
        Task {
            guard await CaptureManager.shared.hasPermission else {
                PermissionManager.shared.ensureScreenRecordingPermission()
                return
            }
            CaptureSessionController.shared.startCapture(mode: .region)
        }
    }

    /// Starts translation mode - presents frozen selection for translation
    func startTranslationMode() {
        logger.info("Translation mode triggered with instant freeze")
        Task {
            guard await CaptureManager.shared.hasPermission else {
                PermissionManager.shared.ensureScreenRecordingPermission()
                return
            }
            CaptureSessionController.shared.startCapture(mode: .screenshotTranslation)
        }
    }

}
