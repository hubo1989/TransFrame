import AppKit
import SwiftUI

/// Controller for presenting and managing the translation history window.
/// Uses a singleton pattern to ensure only one history window is open at a time.
@MainActor
final class HistoryWindowController: NSObject {
    // MARK: - Singleton

    /// Shared instance
    static let shared = HistoryWindowController()

    // MARK: - Properties

    /// The history window
    private var window: NSWindow?

    /// The history store
    private let store: HistoryStore

    // MARK: - Initialization

    private override init() {
        self.store = HistoryStore()
        super.init()
    }

    // MARK: - Public API

    /// Presents the history window.
    /// If already open, brings it to front.
    func showHistory() {
        // If window already exists, bring it to front
        if let window = window, window.isVisible {
            window.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            return
        }

        // Create the SwiftUI view
        let historyView = HistoryView(store: store)

        // Create the hosting view
        let hostingView = NSHostingView(rootView: historyView)
        hostingView.translatesAutoresizingMaskIntoConstraints = false

        // Create the window
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 700, height: 500),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = NSLocalizedString("history.title", comment: "Translation History")
        window.contentView = hostingView
        // 先居中作为默认位置；setFrameAutosaveName 返回 true 表示已恢复存档 frame（覆盖居中），
        // 返回 false 表示无存档（或名称被占用），保持居中。
        // 旧窗口对象若仍占用同名 autosave name（isReleasedWhenClosed=false），
        // 这里会返回 false 退化为居中，不会崩溃。
        window.center()
        _ = window.setFrameAutosaveName("TransFrameHistoryWindow")
        window.isReleasedWhenClosed = false
        window.delegate = self

        // Set window behavior
        window.level = .normal
        window.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]

        // Set minimum size
        window.minSize = NSSize(width: 500, height: 400)

        // Store reference
        self.window = window

        // Show the window
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    /// Closes the history window if open.
    func closeHistory() {
        window?.close()
        window = nil
    }

    /// Adds a translation result to the history.
    /// - Parameters:
    ///   - result: The translation result to save
    ///   - image: Optional screenshot image for thumbnail generation
    func addTranslation(result: TranslationResult, image: CGImage? = nil) {
        store.add(result: result, image: image)
    }
}

// MARK: - NSWindowDelegate

extension HistoryWindowController: NSWindowDelegate {
    nonisolated func windowWillClose(_ notification: Notification) {
        Task { @MainActor in
            // 释放 autosave name，避免窗口复用/重建时同名注册失败导致 frame 恢复失效
            window?.setFrameAutosaveName("")
            // Clear reference
            window = nil
        }
    }
}
