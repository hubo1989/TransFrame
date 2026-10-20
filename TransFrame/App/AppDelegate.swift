import AppKit
import os
import UserNotifications
import Sparkle

/// Application delegate responsible for menu bar setup, coordinator management, and app lifecycle.
/// Runs on the main actor to ensure thread-safe UI operations.
@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    // MARK: - Coordinators

    /// Coordinates capture functionality (full screen, selection, translation mode)
    private(set) var captureCoordinator: CaptureCoordinator?

    /// Coordinates text translation functionality
    private(set) var textTranslationCoordinator: TextTranslationCoordinator?

    /// Coordinates hotkey management
    private(set) var hotkeyCoordinator: HotkeyCoordinator?

    // MARK: - Other Properties

    private var menuBarController: MenuBarController?
    private let settings = AppSettings.shared

    private lazy var updaterController: SPUStandardUpdaterController = {
        let controller = SPUStandardUpdaterController(
            startingUpdater: true,
            updaterDelegate: self,
            userDriverDelegate: nil
        )
        // Listen for check for updates notification from About window
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(checkForUpdates(_:)),
            name: .checkForUpdates,
            object: nil
        )
        return controller
    }()

    // MARK: - NSApplicationDelegate

    func applicationDidFinishLaunching(_ notification: Notification) {
        // Skip UI and auth side effects when running under XCTest
        if ProcessInfo.processInfo.environment["XCTestConfigurationFilePath"] != nil {
            return
        }

        // Ensure we're a menu bar only app (no dock icon)
        NSApp.setActivationPolicy(.accessory)

        // Initialize coordinators
        captureCoordinator = CaptureCoordinator(appDelegate: self)
        textTranslationCoordinator = TextTranslationCoordinator(appDelegate: self)
        hotkeyCoordinator = HotkeyCoordinator(appDelegate: self)

        // Set up menu bar
        menuBarController = MenuBarController(appDelegate: self)
        menuBarController?.setup()

        // Register global hotkeys via coordinator
        Task {
            await hotkeyCoordinator?.registerAllHotkeys()
        }

        // Show onboarding for first launch, otherwise check screen recording permission
        Task {
            await checkFirstLaunchAndShowOnboarding()
        }

        // Check PaddleOCR availability in background (non-blocking)
        PaddleOCRChecker.checkAvailabilityAsync()

        // If credentials exist, unlock access once per app session with Touch ID or the macOS password.
        Task {
            await CredentialAuthManager.shared.authenticateAtLaunchIfNeeded()
        }

        // Initialize updater controller to register notification observer
        // Skip during first launch to avoid overlapping with onboarding
        if settings.onboardingCompleted {
            _ = updaterController
        }

        Logger.general.info("TransFrame launched - settings loaded from: \(self.settings.saveLocation.path)")
    }

    /// Checks if this is the first launch and shows onboarding if needed.
    private func checkFirstLaunchAndShowOnboarding() async {
        if !settings.onboardingCompleted {
            // Show onboarding for first-time users - already @MainActor
            OnboardingWindowController.shared.showOnboarding(settings: settings)

            // Initialize updater after onboarding completes or is dismissed
            NotificationCenter.default.addObserver(
                self,
                selector: #selector(initializeUpdaterIfNeeded),
                name: .onboardingCompleted,
                object: nil
            )
            NotificationCenter.default.addObserver(
                self,
                selector: #selector(initializeUpdaterIfNeeded),
                name: .onboardingDismissed,
                object: nil
            )
        }
        // Note: Don't auto-check screen recording permission on launch
        // to avoid triggering system dialog. Users can check in Settings.
    }

    /// Initializes the updater controller if not already done.
    @objc private func initializeUpdaterIfNeeded() {
        _ = updaterController
    }

    func applicationWillTerminate(_ notification: Notification) {
        // Unregister hotkeys synchronously with timeout
        // Use semaphore to ensure completion before process exits
        let semaphore = DispatchSemaphore(value: 0)
        Task {
            await hotkeyCoordinator?.unregisterAllHotkeys()
            semaphore.signal()
        }
        // Wait up to 2 seconds for hotkey unregistration
        _ = semaphore.wait(timeout: .now() + 2.0)

        // Remove menu bar item
        menuBarController?.teardown()

        Logger.general.info("TransFrame terminating")
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        // For menu bar apps, we don't need to do anything special on reopen
        // The menu bar icon is always visible
        return false
    }

    func applicationSupportsSecureRestorableState(_ app: NSApplication) -> Bool {
        // Enable secure state restoration
        return true
    }

    // MARK: - Hotkey Management (Delegated to HotkeyCoordinator)

    /// Re-registers hotkeys after settings change
    func updateHotkeys() {
        hotkeyCoordinator?.updateHotkeys()
    }

    // MARK: - Capture Actions (Delegated to CaptureCoordinator)

    /// Triggers a full screen capture
    @objc func captureFullScreen() {
        captureCoordinator?.captureFullScreen()
    }

    /// Triggers a selection capture
    @objc func captureSelection() {
        captureCoordinator?.captureSelection()
    }

    /// Starts translation mode - presents region selection for translation
    @objc func startTranslationMode() {
        captureCoordinator?.startTranslationMode()
    }

    // MARK: - Text Translation Actions (Delegated to TextTranslationCoordinator)

    /// Triggers text selection translation
    @objc func translateSelectedText() {
        textTranslationCoordinator?.translateSelectedText()
    }

    /// Triggers translate and insert workflow
    @objc func translateClipboardAndInsert() {
        textTranslationCoordinator?.translateClipboardAndInsert()
    }

    // MARK: - UI Actions

    /// Opens the settings window
    @objc func openSettings() {
        Logger.ui.debug("Opening settings window")

        SettingsWindowController.shared.showSettings(appDelegate: self)
    }

    /// Opens the about window
    @objc func openAbout() {
        Logger.ui.debug("Opening about window")

        AboutWindowController.shared.showAbout()
    }

    /// Checks for app updates via Sparkle
    @objc func checkForUpdates(_ sender: Any?) {
        Logger.ui.info("Check for updates triggered")
        // Activate the app to ensure Sparkle's window is visible
        NSApp.activate(ignoringOtherApps: true)
        updaterController.checkForUpdates(sender)
    }

    /// Opens the translation history window
    @objc func openHistory() {
        Logger.ui.debug("Opening translation history window")

        HistoryWindowController.shared.showHistory()
    }

    // MARK: - Error Handling

    /// Shows an error alert for capture failures
    func showCaptureError(_ error: TransFrameError) {
        Logger.general.error("Capture error: \(error.localizedDescription)")

        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = error.errorDescription ?? NSLocalizedString("error.capture.failed", comment: "")
        alert.informativeText = error.recoverySuggestion ?? ""

        switch error {
        case .permissionDenied:
            let openSettingsTitle = NSLocalizedString(
                "error.permission.open.settings",
                comment: "Open System Settings"
            )
            alert.addButton(withTitle: openSettingsTitle)
            alert.addButton(withTitle: NSLocalizedString("error.dismiss", comment: "Dismiss"))

            let response = alert.runModal()
            if response == .alertFirstButtonReturn {
                // Open System Settings > Privacy > Screen Recording
                let urlString = "x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"
                if let url = URL(string: urlString) {
                    NSWorkspace.shared.open(url)
                }
            }

        case .displayDisconnected:
            // Offer to retry capture on a different display
            alert.addButton(withTitle: NSLocalizedString(
                "error.retry.capture",
                comment: "Retry"
            ))
            alert.addButton(withTitle: NSLocalizedString("error.dismiss", comment: "Dismiss"))

            let response = alert.runModal()
            if response == .alertFirstButtonReturn {
                // Retry the capture on the remaining displays
                captureFullScreen()
            }

        case .diskFull, .invalidSaveLocation:
            // Offer to open settings to change save location
            alert.addButton(withTitle: NSLocalizedString("menu.settings", comment: "Settings..."))
            alert.addButton(withTitle: NSLocalizedString("error.dismiss", comment: "Dismiss"))

            let response = alert.runModal()
            if response == .alertFirstButtonReturn {
                openSettings()
            }

        default:
            alert.addButton(withTitle: NSLocalizedString("error.ok", comment: "OK"))
            alert.runModal()
        }
    }
}

// MARK: - SPUUpdaterDelegate

extension AppDelegate: SPUUpdaterDelegate {
    nonisolated func updater(_ updater: SPUUpdater, didFinishLoading appcast: SUAppcast) {
        Logger.ui.info("Sparkle: didFinishLoading appcast")
    }

    nonisolated func updater(_ updater: SPUUpdater, didFindValidUpdate item: SUAppcastItem) {
        Logger.ui.info("Sparkle: didFindValidUpdate - \(item.displayVersionString)")
    }

    nonisolated func updaterDidNotFindUpdate(_ updater: SPUUpdater) {
        Logger.ui.info("Sparkle: didNotFindUpdate")
    }

    nonisolated func updater(_ updater: SPUUpdater, didAbortWithError error: Error) {
        Logger.ui.error("Sparkle: didAbortWithError - \(error.localizedDescription)")
    }
}
