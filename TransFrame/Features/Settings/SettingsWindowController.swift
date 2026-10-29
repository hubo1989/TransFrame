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
            styleMask: [.titled, .closable, .miniaturizable, .fullSizeContentView],
            backing: .buffered,
            defer: false
        )
        window.titleVisibility = .hidden
        window.titlebarAppearsTransparent = true
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

        window.setFrameAutosaveName("TransFrameSettingsWindow")
        if !window.setFrameUsingName("TransFrameSettingsWindow") {
            window.center()
        }
        window.isReleasedWhenClosed = false
        window.delegate = self

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
        window?.close()
        window = nil
        viewModel = nil
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
