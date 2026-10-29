import AppKit
import SwiftUI

/// Controller for presenting and managing the settings window.
/// Uses a singleton pattern to ensure only one settings window is open at a time.
@MainActor
final class SettingsWindowController: NSObject {
    // MARK: - Singleton

    /// Shared instance
    static let shared = SettingsWindowController()

    // MARK: - Properties

    /// The settings window
    private var window: NSWindow?

    /// The settings view model
    private var viewModel: SettingsViewModel?

    /// The event monitor for keyboard shortcuts
    private var keyEventMonitor: Any?

    /// Observer for in-app language changes (refreshes the native window title)
    private var languageObserver: NSObjectProtocol?

    // MARK: - Initialization

    private override init() {
        super.init()
    }

    // MARK: - Public API

    /// Presents the settings window.
    /// If already open, brings it to front.
    func showSettings(appDelegate: AppDelegate) {
        // If window already exists, bring it to front
        if let window = window, window.isVisible {
            window.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            return
        }

        // Create view model with app delegate reference
        let viewModel = SettingsViewModel(settings: AppSettings.shared, appDelegate: appDelegate)
        self.viewModel = viewModel

        // Create the SwiftUI view
        let settingsView = SettingsView(viewModel: viewModel)

        // Create the hosting view
        let hostingView = NSHostingView(rootView: settingsView)
        hostingView.translatesAutoresizingMaskIntoConstraints = false

        // Create the window
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 800, height: 600),
            styleMask: [.titled, .closable, .miniaturizable],
            backing: .buffered,
            defer: false
        )
        window.title = NSLocalizedString("settings.title", comment: "Settings")
        window.backgroundColor = .windowBackgroundColor
        window.isOpaque = true
        window.hasShadow = true
        window.isMovableByWindowBackground = false
        window.titlebarSeparatorStyle = .automatic

        // Add hosting view and set up constraints to fill window COMPLETELY
        let containerView = NSView()
        window.contentView = containerView
        containerView.addSubview(hostingView)

        NSLayoutConstraint.activate([
            hostingView.topAnchor.constraint(equalTo: containerView.topAnchor),
            hostingView.leadingAnchor.constraint(equalTo: containerView.leadingAnchor),
            hostingView.trailingAnchor.constraint(equalTo: containerView.trailingAnchor),
            hostingView.bottomAnchor.constraint(equalTo: containerView.bottomAnchor)
        ])

        // 先居中作为默认位置；setFrameAutosaveName 返回 true 表示已恢复存档 frame（覆盖居中），
        // 返回 false 表示无存档，保持居中。
        window.center()
        _ = window.setFrameAutosaveName("TransFrameSettingsWindow")
        window.isReleasedWhenClosed = false
        window.delegate = self

        // Follow in-app language switches so the native title stays localized
        registerLanguageObserverIfNeeded()

        // Set window behavior
        window.level = .normal
        window.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]

        // Store reference
        self.window = window

        // Install key event monitor for shortcut recording
        installKeyEventMonitor()

        // Show the window
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)

        // Don't auto-check permissions on window open - it may trigger system dialogs
        // User can manually check via the reset button in Settings > General
    }

    /// Closes the settings window if open.
    func closeSettings() {
        removeKeyEventMonitor()
        removeLanguageObserver()
        window?.close()
        window = nil
        viewModel = nil
    }

    // MARK: - Language Observer

    /// Registers the language-change observer once per settings window session.
    private func registerLanguageObserverIfNeeded() {
        guard languageObserver == nil else { return }
        languageObserver = NotificationCenter.default.addObserver(
            forName: LanguageManager.languageDidChangeNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated {
                self?.window?.title = LanguageManager.shared.localizedString("settings.title", comment: "Settings")
            }
        }
    }

    /// Removes the language-change observer (idempotent).
    private func removeLanguageObserver() {
        if let languageObserver {
            NotificationCenter.default.removeObserver(languageObserver)
            self.languageObserver = nil
        }
    }

    // MARK: - Key Event Monitoring

    /// Installs a local key event monitor for shortcut recording.
    private func installKeyEventMonitor() {
        removeKeyEventMonitor()

        keyEventMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            guard let self = self, let viewModel = self.viewModel else {
                return event
            }

            // Try to handle the event for shortcut recording
            if viewModel.handleKeyEvent(event) {
                return nil  // Consume the event
            }

            return event
        }
    }

    /// Removes the key event monitor.
    private func removeKeyEventMonitor() {
        if let monitor = keyEventMonitor {
            NSEvent.removeMonitor(monitor)
            keyEventMonitor = nil
        }
    }
}

// MARK: - NSWindowDelegate

extension SettingsWindowController: NSWindowDelegate {
    nonisolated func windowWillClose(_ notification: Notification) {
        Task { @MainActor in
            // Cancel any in-progress shortcut recording
            viewModel?.cancelRecording()

            // Remove the event monitor
            removeKeyEventMonitor()

            // Remove the language observer (close button path also lands here)
            removeLanguageObserver()

            // 释放 autosave name，避免窗口重建时同名注册失败导致 frame 恢复失效
            window?.setFrameAutosaveName("")

            // Clear references
            window = nil
            viewModel = nil
        }
    }

    nonisolated func windowDidBecomeKey(_ notification: Notification) {
        // Reinstall monitor when window becomes key
        Task { @MainActor in
            installKeyEventMonitor()
        }
    }

    nonisolated func windowDidResignKey(_ notification: Notification) {
        // Cancel recording when window loses focus
        Task { @MainActor in
            viewModel?.cancelRecording()
        }
    }
}
