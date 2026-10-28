//
//  TextTranslationCoordinator.swift
//  TransFrame
//
//  Created during architecture refactoring - extracts text translation logic from AppDelegate
//
//  ## Responsibilities
//  - Text selection translation: Captures selected text, translates, shows popup
//  - Translate and insert: Captures text, translates, replaces original text
//
//  ## Usage
//  Access via AppDelegate.textTranslationCoordinator:
//  ```swift
//  appDelegate.textTranslationCoordinator?.translateSelectedText()
//  appDelegate.textTranslationCoordinator?.translateClipboardAndInsert()
//  ```
//

import AppKit
import os

/// Coordinates text translation functionality:
/// text selection translation and translate-and-insert workflows.
///
/// This coordinator was extracted from AppDelegate as part of the architecture
/// refactoring to improve separation of concerns and testability.
@MainActor
final class TextTranslationCoordinator {
    // MARK: - Properties

    /// Reference to app delegate for error handling
    private weak var appDelegate: AppDelegate?

    /// Whether a translation operation is currently in progress
    private var isTranslating = false

    private let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "TransFrame", category: "TextTranslationCoordinator")

    // MARK: - Initialization

    init(appDelegate: AppDelegate) {
        self.appDelegate = appDelegate
    }

    // MARK: - Public API

    /// Triggers text selection translation workflow
    func translateSelectedText() {
        guard !isTranslating else {
            logger.debug("Translation already in progress, ignoring request")
            return
        }

        logger.info("Text selection translation triggered")

        isTranslating = true

        Task { [weak self] in
            defer { self?.isTranslating = false }
            await self?.handleTextSelectionTranslation()
        }
    }

    /// Triggers translate-and-insert workflow
    func translateClipboardAndInsert() {
        guard !isTranslating else {
            logger.debug("Translation already in progress, ignoring request")
            return
        }

        logger.info("Translate and insert triggered")

        isTranslating = true

        Task { [weak self] in
            defer { self?.isTranslating = false }
            await self?.handleTranslateClipboardAndInsert()
        }
    }

    // MARK: - Private Implementation

    /// Ensures accessibility permission is granted before performing text operations.
    /// - Returns: true if permission is available, false otherwise (error already shown)
    private func ensureAccessibilityPermission() async -> Bool {
        let permissionManager = PermissionManager.shared
        permissionManager.refreshPermissionStatus()

        if !permissionManager.hasAccessibilityPermission {
            // Call the drag-to-authorize flow for accessibility
            permissionManager.ensureAccessibilityPermissionFlow()
            return false
        }
        return true
    }

    /// Handles the complete text selection translation flow
    private func handleTextSelectionTranslation() async {
        // Check accessibility permission before attempting text capture
        guard await ensureAccessibilityPermission() else { return }

        do {
            // Step 1: Capture selected text
            let textSelectionService = TextSelectionService.shared
            let selectionResult = try await textSelectionService.captureSelectedText()

            logger.info("Captured selected text: \(selectionResult.text.count) characters")
            logger.info("Source app: \(selectionResult.sourceApplication ?? "unknown")")

            // Step 2: Show loading indicator
            await showLoadingIndicator()

            // Step 3: Translate the captured text
            if #available(macOS 13.0, *) {
                let config = TextTranslationConfig.fromAppSettings()
                let translationResult = try await TextTranslationFlow.shared.translate(
                    selectionResult.text,
                    config: config
                )

                logger.info("Translation completed in \(translationResult.processingTime * 1000)ms")

                // Step 4: Hide loading and display result popup
                await hideLoadingIndicator()

                await MainActor.run {
                    TextTranslationPopupController.shared.presentPopup(result: translationResult)
                }

            } else {
                await hideLoadingIndicator()
                appDelegate?.showCaptureError(.captureFailure(underlying: NSError(
                    domain: "TransFrame",
                    code: -1,
                    userInfo: [NSLocalizedDescriptionKey: "macOS 13.0+ required for text translation"]
                )))
            }

        } catch let error as TextSelectionService.CaptureError {
            await hideLoadingIndicator()

            // Handle empty selection with user notification (no crash)
            switch error {
            case .noSelection:
                logger.info("No text selected for translation")
                await showNoSelectionNotification()
            case .accessibilityPermissionDenied:
                logger.error("Accessibility permission denied")
                appDelegate?.showCaptureError(.captureFailure(underlying: error))
            default:
                logger.error("Failed to capture selected text: \(error.localizedDescription)")
                appDelegate?.showCaptureError(.captureFailure(underlying: error))
            }

        } catch let error as TextTranslationError {
            await hideLoadingIndicator()
            logger.error("Translation failed: \(error.localizedDescription)")
            showTranslationError(error)

        } catch {
            await hideLoadingIndicator()
            logger.error("Unexpected error during text translation: \(error.localizedDescription)")
            appDelegate?.showCaptureError(.captureFailure(underlying: error))
        }
    }

    /// Handles the translate selected text and insert flow
    private func handleTranslateClipboardAndInsert() async {
        // Check accessibility permission before attempting text capture and insertion
        guard await ensureAccessibilityPermission() else { return }

        showInsertionProgress(message: String(localized: "translateAndInsert.progress.capturing"))
        defer {
            TextInsertionProgressController.shared.dismiss()
        }

        // Step 1: Capture selected text
        let textSelectionService = TextSelectionService.shared
        let selectedText: String
        let insertionTarget: CapturedTextInsertion?
        let sourcePID: pid_t?

        do {
            let selectionResult = try await textSelectionService.captureSelectedText(bindForInsertion: true)
            insertionTarget = selectionResult.insertionTarget
            selectedText = selectionResult.text
            sourcePID = selectionResult.sourceProcessID
            logger.info("Captured selected text: \(selectedText.count) characters")
        } catch let error as TextSelectionService.CaptureError {
            switch error {
            case .noSelection:
                logger.info("No text selected for translate-and-insert")
                hideInsertionProgress()
                await showNoSelectionNotification()
                return
            default:
                logger.error("Failed to capture selected text: \(error.localizedDescription)")
                hideInsertionProgress()
                appDelegate?.showCaptureError(.captureFailure(underlying: error))
                return
            }
        } catch {
            logger.error("Unexpected error capturing text: \(error.localizedDescription)")
            hideInsertionProgress()
            appDelegate?.showCaptureError(.captureFailure(underlying: error))
            return
        }

        // Step 2: Translate the text
        showInsertionProgress(message: String(localized: "translateAndInsert.progress.translating"))

        let translatedText: String

        do {
            if #available(macOS 13.0, *) {
                let config = TextTranslationConfig.forTranslateAndInsert()
                let translationResult = try await TextTranslationFlow.shared.translate(selectedText, config: config)
                translatedText = translationResult.translatedText

                logger.info("Translation completed in \(translationResult.processingTime * 1000)ms")
            } else {
                hideInsertionProgress()
                appDelegate?.showCaptureError(.captureFailure(underlying: NSError(
                    domain: "TransFrame",
                    code: -1,
                    userInfo: [NSLocalizedDescriptionKey: "macOS 13.0+ required for text translation"]
                )))
                return
            }
        } catch let error as TextTranslationError {
            logger.error("Translation failed: \(error.localizedDescription)")
            hideInsertionProgress()
            showTranslationError(error)
            return
        } catch {
            logger.error("Unexpected error during translation: \(error.localizedDescription)")
            hideInsertionProgress()
            appDelegate?.showCaptureError(.captureFailure(underlying: error))
            return
        }

        // Step 3: Delete selection and insert translated text
        showInsertionProgress(message: String(localized: "translateAndInsert.progress.inserting"))

        do {
            if let insertionTarget {
                try await TextInsertService.shared.replaceSelection(with: translatedText, captured: insertionTarget)
            } else if let sourcePID {
                // AX 无法绑定（如 Electron 无障碍未启用）：降级为前台锚定的粘贴插入
                logger.info("No AX target bound, falling back to frontmost-anchored paste")
                try await TextInsertService.shared.pasteToCapturedFrontmost(translatedText, anchorPID: sourcePID)
            } else {
                throw TextInsertService.InsertError.targetUnavailable
            }
            logger.info("Successfully inserted translated text")
        } catch let error as TextInsertService.InsertError {
            logger.error("Text insertion failed: \(error.localizedDescription)")
            hideInsertionProgress()
            let alert = NSAlert()
            alert.alertStyle = .warning
            alert.messageText = String(localized: "textTranslation.error.insertFailed")
            alert.informativeText = error.localizedDescription
            if insertionTarget == nil && sourcePID == nil {
                alert.informativeText += "\n\n" + String(localized: "textTranslation.error.insertFailed.noTarget")
            }
            let pasteboard = NSPasteboard.general
            pasteboard.clearContents()
            if pasteboard.setString(translatedText, forType: .string) {
                alert.informativeText += "\n\n" + String(localized: "textTranslation.error.insertFailed.copied")
            }
            alert.addButton(withTitle: String(localized: "common.ok"))
            alert.runModal()
        } catch {
            logger.error("Unexpected error during translate and insert: \(error.localizedDescription)")
            hideInsertionProgress()
            appDelegate?.showCaptureError(.captureFailure(underlying: error))
        }
    }

    /// Shows an error alert for translation failures
    private func showTranslationError(_ error: TextTranslationError) {
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = error.errorDescription ?? String(localized: "error.translation.failed")
        alert.informativeText = error.recoverySuggestion ?? ""
        alert.addButton(withTitle: String(localized: "common.ok"))
        alert.runModal()
    }

    // MARK: - UI Helpers

    /// Shows a brief loading indicator for text translation
    private func showLoadingIndicator() async {
        // Already @MainActor, no need for MainActor.run
        let placeholderImage = NSImage(
            systemSymbolName: "character.textbox",
            accessibilityDescription: "Translating"
        )

        let scaleFactor = NSScreen.main?.backingScaleFactor ?? 2.0

        if let cgImage = placeholderImage?.cgImage(forProposedRect: nil, context: nil, hints: nil) {
            BilingualResultWindowController.shared.showLoading(
                originalImage: cgImage,
                scaleFactor: scaleFactor,
                message: String(localized: "textTranslation.loading")
            )
        }
    }

    /// Hides the loading indicator
    private func hideLoadingIndicator() async {
        // Already @MainActor, no need for MainActor.run
        BilingualResultWindowController.shared.close()
    }

    /// Shows progress feedback next to the active insertion point.
    private func showInsertionProgress(message: String) {
        TextInsertionProgressController.shared.show(message: message)
    }

    /// Hides translate-and-insert progress feedback before presenting blocking UI.
    private func hideInsertionProgress() {
        TextInsertionProgressController.shared.dismiss()
    }

    /// Shows a notification when no text is selected
    private func showNoSelectionNotification() async {
        // Already @MainActor, no need for MainActor.run
        let alert = NSAlert()
        alert.alertStyle = .informational
        alert.messageText = String(localized: "textTranslation.noSelection.title")
        alert.informativeText = String(localized: "textTranslation.noSelection.message")
        alert.addButton(withTitle: String(localized: "common.ok"))
        alert.runModal()
    }
}
