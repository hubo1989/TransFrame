//
//  TextTranslationPopupWindow.swift
//  TransFrame
//
//  Created for US-004: Create TextTranslationPopup window for showing translation results
//  Updated: Standard window style with title bar, consistent with BilingualResultWindow
//

import AppKit
import CoreGraphics
import SwiftUI

// MARK: - TextTranslationPopupDelegate

/// Delegate protocol for text translation popup events.
@MainActor
protocol TextTranslationPopupDelegate: AnyObject {
    /// Called when user dismisses the popup.
    func textTranslationPopupDidDismiss()
}

// MARK: - TextTranslationPopupWindowController

/// Controller for managing text translation popup window.
/// Uses standard window style consistent with BilingualResultWindow.
@MainActor
final class TextTranslationPopupController: NSObject {
    static let shared = TextTranslationPopupController()

    private var window: NSWindow?
    private weak var popupDelegate: TextTranslationPopupDelegate?
    private var presentationID = UUID()
    var onDismiss: (() -> Void)?

    private let debounceInterval: TimeInterval = 0.3
    private var lastPresentationTime: Date?

    private override init() {
        super.init()
    }

    // MARK: - Public API

    func presentPopup(result: TextTranslationResult) {
        guard canPresent() else { return }

        dismissPopup()

        let sourceLanguageName = languageDisplayName(for: result.sourceLanguage)
        let targetLanguageName = languageDisplayName(for: result.targetLanguage)

        // Create SwiftUI view
        let contentView = TextTranslationPopupContentView(
            originalText: result.originalText,
            translatedText: result.translatedText,
            sourceLanguage: sourceLanguageName,
            targetLanguage: targetLanguageName,
            engineResults: result.engineResults,
            onCopy: { [weak self] in
                self?.copyToClipboard(result.translatedText)
            }
        )

        let hostingView = NSHostingView(rootView: contentView)

        // Calculate window size
        let windowSize = calculateWindowSize(
            originalText: result.originalText,
            translatedText: result.translatedText
        )

        // Create window with standard style
        let newWindow = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: windowSize.width, height: windowSize.height),
            styleMask: [.titled, .closable, .resizable],
            backing: .buffered,
            defer: false
        )

        newWindow.contentView = hostingView
        newWindow.title = String(localized: "textTranslation.window.title")
        newWindow.center()
        newWindow.isReleasedWhenClosed = false
        newWindow.delegate = self
        newWindow.minSize = NSSize(width: 380, height: 200)
        newWindow.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]

        presentationID = UUID()
        self.window = newWindow
        lastPresentationTime = Date()

        newWindow.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    func dismissPopup() {
        window?.close()
        window = nil
        onDismiss?()
    }

    // MARK: - Private

    private func canPresent() -> Bool {
        guard let lastTime = lastPresentationTime else { return true }
        return Date().timeIntervalSince(lastTime) >= debounceInterval
    }

    func resetDebounce() {
        lastPresentationTime = nil
    }

    private func languageDisplayName(for code: String?) -> String {
        TranslationLanguage.displayName(
            for: code,
            locale: .current,
            autoDisplayName: NSLocalizedString("language.auto", value: "Auto Detected", comment: "")
        )
    }

    private func calculateWindowSize(originalText: String, translatedText: String) -> NSSize {
        let textWidth: CGFloat = 388  // 420 - 16*2 padding

        let originalFont = NSFont.systemFont(ofSize: 13, weight: .regular)
        let translatedFont = NSFont.systemFont(ofSize: 15, weight: .medium)

        let originalSize = (originalText as NSString).boundingRect(
            with: NSSize(width: textWidth - 28, height: .greatestFiniteMagnitude),
            options: [.usesLineFragmentOrigin, .usesFontLeading],
            attributes: [.font: originalFont]
        )

        let translatedSize = (translatedText as NSString).boundingRect(
            with: NSSize(width: textWidth - 28, height: .greatestFiniteMagnitude),
            options: [.usesLineFragmentOrigin, .usesFontLeading],
            attributes: [.font: translatedFont]
        )

        // Calculate height
        var totalHeight: CGFloat = 0
        totalHeight += 16  // top padding
        totalHeight += 28 + ceil(originalSize.height) + 28  // original section with padding
        totalHeight += 16  // spacing
        totalHeight += 28 + ceil(translatedSize.height) + 28  // translated section with padding
        totalHeight += 16  // bottom padding
        totalHeight += 44  // toolbar

        // Constrain
        totalHeight = min(max(totalHeight, 200), 450)

        return NSSize(width: 420, height: totalHeight)
    }

    private func copyToClipboard(_ text: String) {
        let pasteboard = NSPasteboard.general
        pasteboard.clearContents()
        pasteboard.setString(text, forType: .string)
    }
}

// MARK: - NSWindowDelegate

extension TextTranslationPopupController: NSWindowDelegate {
    nonisolated func windowWillClose(_ notification: Notification) {
        guard let closedWindow = notification.object as? NSWindow else { return }
        Task { @MainActor in
            // Only clear the reference if this is still the current window,
            // preventing an old window's async close callback from clearing
            // a newly-presented window's reference.
            if window === closedWindow {
                window = nil
                onDismiss?()
                popupDelegate?.textTranslationPopupDidDismiss()
            }
        }
    }
}
