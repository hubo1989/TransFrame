import AppKit
import Foundation
import Observation

/// Manages all pinned capture windows with observable state.
/// Provides centralized control over in-place pinning, synchronization, and interaction recovery.
@MainActor
@Observable
final class PinnedWindowsManager {
    // MARK: - Singleton

    static let shared = PinnedWindowsManager()

    // MARK: - Properties

    /// All currently pinned windows
    @ObservationIgnored
    private(set) var pinnedWindows: [UUID: PinnedWindow] = [:]

    /// Maximum number of pinned windows allowed
    @ObservationIgnored
    private let maxPinnedWindows = 10

    /// Reactive counter for observable UI updates
    var changeCounter: Int = 0

    // MARK: - Initialization

    private init() {}

    // MARK: - Public API

    /// Pins a CaptureDocument with 1:1 in-place positioning.
    @discardableResult
    func pin(document: CaptureDocument) -> PinnedWindow? {
        CaptureDocumentStore.shared.register(document)

        if let existing = pinnedWindows[document.id] {
            existing.makeKeyAndOrderFront(nil)
            return existing
        }

        if pinnedWindows.count >= maxPinnedWindows {
            showLimitWarning()
            return nil
        }

        let window = PinnedWindow(document: document)
        window.onClose = { [weak self] in
            self?.unpinWindow(document.id)
        }

        pinnedWindows[document.id] = window
        window.show()
        changeCounter += 1

        return window
    }

    /// Pins a legacy screenshot with its annotations.
    @discardableResult
    func pinScreenshot(_ screenshot: Screenshot, annotations: [Annotation]) -> PinnedWindow? {
        if let existingWindow = pinnedWindows[screenshot.id] {
            existingWindow.makeKeyAndOrderFront(nil)
            return existingWindow
        }

        if pinnedWindows.count >= maxPinnedWindows {
            showLimitWarning()
            return nil
        }

        let pinnedWindow = PinnedWindow(
            screenshot: screenshot,
            annotations: annotations
        )

        pinnedWindow.onClose = { [weak self] in
            self?.unpinWindow(screenshot.id)
        }

        pinnedWindows[screenshot.id] = pinnedWindow
        pinnedWindow.show()
        changeCounter += 1

        return pinnedWindow
    }

    /// Unpins a window by its ID.
    func unpinWindow(_ id: UUID) {
        guard let window = pinnedWindows[id] else { return }
        window.isProgrammaticClose = true
        window.close()
        pinnedWindows.removeValue(forKey: id)
        changeCounter += 1
    }

    /// Unpins all pinned windows.
    func unpinAll() {
        let windows = Array(pinnedWindows.values)
        pinnedWindows.removeAll()
        for window in windows {
            window.isProgrammaticClose = true
            window.close()
        }
        changeCounter += 1
    }

    /// Disables mouse click-through for all pinned windows, restoring interactive controls.
    func resetAllInteractions() {
        for window in pinnedWindows.values {
            window.setClickThrough(false)
            window.orderFront(nil)
        }
        changeCounter += 1
    }

    /// Brings all pinned windows to the front on active workspace.
    func showAllHidden() {
        for window in pinnedWindows.values {
            window.orderFront(nil)
        }
    }

    /// Checks if a document or screenshot is currently pinned.
    func isPinned(_ id: UUID) -> Bool {
        pinnedWindows[id] != nil
    }

    /// Returns the number of currently pinned windows.
    var pinnedCount: Int {
        pinnedWindows.count
    }

    /// Brings a pinned window to front.
    func bringToFront(_ id: UUID) {
        pinnedWindows[id]?.makeKeyAndOrderFront(nil)
    }

    // MARK: - Private Helpers

    private func showLimitWarning() {
        let alert = NSAlert()
        alert.messageText = NSLocalizedString(
            "pinned.limit.title",
            value: "Pin Limit Reached",
            comment: "Alert title when pin limit is reached"
        )
        alert.informativeText = String(
            format: NSLocalizedString(
                "pinned.limit.message",
                value: "You can pin up to %d screenshots at a time. Please unpin some first.",
                comment: "Alert message explaining pin limit"
            ),
            maxPinnedWindows
        )
        alert.alertStyle = .warning
        alert.addButton(withTitle: NSLocalizedString("OK", comment: ""))
        alert.runModal()
    }
}
