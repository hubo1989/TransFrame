//
//  ColorHistoryWindowController.swift
//  TransFrame
//
//  Presents the independent color history window (spec §4.1): single
//  instance, remembered frame, system appearance, sidebar vibrancy.
//

import AppKit
import SwiftUI

/// Controller for presenting and managing the independent color history
/// window. Singleton so repeated opens focus the existing window instead of
/// stacking duplicates (spec §4.1).
@MainActor
final class ColorHistoryWindowController: NSObject {
    // MARK: - Singleton

    /// Shared instance
    static let shared = ColorHistoryWindowController()

    // MARK: - Properties

    /// The color history window
    private var window: NSWindow?

    /// The history store shared with this window's content
    private let store: HistoryStore

    // MARK: - Initialization

    private override init() {
        self.store = HistoryWindowController.shared.store
        super.init()
    }

    // MARK: - Public API

    /// Presents the color history window.
    /// If already open, brings it to front (spec §4.1 single-instance rule).
    func showColorHistory() {
        // If window already exists, bring it to front
        if let window = window, window.isVisible {
            window.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            return
        }

        // Create the SwiftUI view
        let colorHistoryView = ColorHistoryView(store: store)

        // Create the hosting view
        let hostingView = NSHostingView(rootView: colorHistoryView)
        hostingView.translatesAutoresizingMaskIntoConstraints = false

        // Create the window: default 560×420, resizable (spec §4.1)
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 560, height: 420),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = NSLocalizedString("colorHistory.title", comment: "Color History")
        window.contentView = hostingView
        // 先居中作为默认位置；setFrameAutosaveName 返回 true 表示已恢复存档 frame（覆盖居中），
        // 返回 false 表示无存档（或名称被占用），保持居中。
        // 旧窗口对象若仍占用同名 autosave name（isReleasedWhenClosed=false），
        // 这里会返回 false 退化为居中，不会崩溃。
        window.center()
        _ = window.setFrameAutosaveName("TransFrameColorHistoryWindow")
        window.isReleasedWhenClosed = false
        window.delegate = self

        // Set window behavior
        window.level = .normal
        window.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]

        // Set minimum size
        window.minSize = NSSize(width: 420, height: 320)

        // Store reference
        self.window = window

        // Show the window
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    /// Closes the color history window if open.
    func closeColorHistory() {
        window?.close()
        window = nil
    }
}

// MARK: - NSWindowDelegate

extension ColorHistoryWindowController: NSWindowDelegate {
    nonisolated func windowWillClose(_ notification: Notification) {
        Task { @MainActor in
            // 释放 autosave name，避免窗口复用/重建时同名注册失败导致 frame 恢复失效
            window?.setFrameAutosaveName("")
            // Clear reference
            window = nil
        }
    }
}
