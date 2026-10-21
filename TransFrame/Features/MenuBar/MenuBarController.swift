import AppKit

/// Manages the menu bar status item and its menu.
/// Responsible for setting up the menu bar icon and building the app menu.
@MainActor
final class MenuBarController {
    // MARK: - Properties

    /// The status item displayed in the menu bar
    private var statusItem: NSStatusItem?

    /// Reference to the app delegate for action routing
    private weak var appDelegate: AppDelegate?

    // MARK: - Initialization

    init(appDelegate: AppDelegate) {
        self.appDelegate = appDelegate

        NotificationCenter.default.addObserver(
            forName: LanguageManager.languageDidChangeNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            Task { @MainActor in
                self?.rebuildMenu()
            }
        }

        NotificationCenter.default.addObserver(
            forName: AppSettings.shortcutDidChangeNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            Task { @MainActor in
                self?.rebuildMenu()
            }
        }
    }

    // MARK: - Setup

    /// Sets up the menu bar status item with icon and menu
    func setup() {
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)

        if let button = statusItem?.button {
            button.image = NSImage(systemSymbolName: "camera.viewfinder", accessibilityDescription: "TransFrame")
            button.image?.isTemplate = true
        }

        statusItem?.menu = buildMenu()
    }

    /// Removes the status item from the menu bar
    func teardown() {
        if let item = statusItem {
            NSStatusBar.system.removeStatusItem(item)
            statusItem = nil
        }
    }

    /// Rebuilds the menu when language changes
    func rebuildMenu() {
        statusItem?.menu = buildMenu()
    }

    // MARK: - Menu Construction

    /// Builds the complete menu for the status item
    private func buildMenu() -> NSMenu {
        let menu = NSMenu()
        let settings = AppSettings.shared

        // Capture Full Screen
        menu.addItem(createMenuItem(
            titleKey: "menu.capture.full.screen",
            comment: "Capture Full Screen",
            action: #selector(AppDelegate.captureFullScreen),
            shortcut: settings.fullScreenShortcut,
            target: appDelegate,
            imageName: "camera.fill"
        ))

        // Capture Selection
        menu.addItem(createMenuItem(
            titleKey: "menu.capture.selection",
            comment: "Capture Selection",
            action: #selector(AppDelegate.captureSelection),
            shortcut: settings.selectionShortcut,
            target: appDelegate,
            imageName: "crop"
        ))

        // Translation Mode
        menu.addItem(createMenuItem(
            titleKey: "menu.translation.mode",
            comment: "Translation Mode",
            action: #selector(AppDelegate.startTranslationMode),
            shortcut: settings.translationModeShortcut,
            target: appDelegate,
            imageName: "character"
        ))

        menu.addItem(NSMenuItem.separator())

        // Translation History
        menu.addItem(createMenuItem(
            titleKey: "menu.translation.history",
            comment: "Translation History",
            action: #selector(AppDelegate.openHistory),
            keyEquivalent: "h",
            target: appDelegate,
            imageName: "clock.arrow.circlepath"
        ))

        menu.addItem(NSMenuItem.separator())

        // Pinned Windows Actions
        let pinSubmenu = NSMenu()
        let resetItem = NSMenuItem(
            title: NSLocalizedString("menu.pinned.reset_interactions", value: "Restore All Pin Interactions", comment: ""),
            action: #selector(resetPinnedInteractions),
            keyEquivalent: ""
        )
        resetItem.target = self
        resetItem.image = NSImage(systemSymbolName: "cursorarrow.rays", accessibilityDescription: nil)
        pinSubmenu.addItem(resetItem)

        let showAllItem = NSMenuItem(
            title: NSLocalizedString("menu.pinned.show_all", value: "Bring All Pins to Front", comment: ""),
            action: #selector(showAllPinnedWindows),
            keyEquivalent: ""
        )
        showAllItem.target = self
        showAllItem.image = NSImage(systemSymbolName: "macwindow.on.rectangle", accessibilityDescription: nil)
        pinSubmenu.addItem(showAllItem)

        let pinParentItem = NSMenuItem(
            title: NSLocalizedString("menu.pinned.title", value: "Pinned Windows", comment: ""),
            action: nil,
            keyEquivalent: ""
        )
        pinParentItem.image = NSImage(systemSymbolName: "pin.fill", accessibilityDescription: nil)
        pinParentItem.submenu = pinSubmenu
        menu.addItem(pinParentItem)

        menu.addItem(NSMenuItem.separator())

        // Settings
        menu.addItem(createMenuItem(
            titleKey: "menu.settings",
            comment: "Settings...",
            action: #selector(AppDelegate.openSettings),
            keyEquivalent: ",",
            modifierMask: [.command],
            target: appDelegate,
            imageName: "gearshape"
        ))

        // About
        menu.addItem(createMenuItem(
            titleKey: "menu.about",
            comment: "About TransFrame",
            action: #selector(AppDelegate.openAbout),
            target: appDelegate,
            imageName: "info.circle"
        ))

        menu.addItem(NSMenuItem.separator())

        // Quit
        menu.addItem(createMenuItem(
            titleKey: "menu.quit",
            comment: "Quit TransFrame",
            action: #selector(NSApplication.terminate(_:)),
            keyEquivalent: "q",
            modifierMask: [.command],
            imageName: "power"
        ))

        return menu
    }

    /// Creates a localized menu item with common properties
    private func createMenuItem(
        titleKey: String,
        comment: String,
        action: Selector?,
        shortcut: KeyboardShortcut? = nil,
        keyEquivalent: String = "",
        modifierMask: NSEvent.ModifierFlags = [],
        target: AnyObject? = nil,
        imageName: String? = nil
    ) -> NSMenuItem {
        let finalKeyEquivalent: String
        let finalModifierMask: NSEvent.ModifierFlags

        if let shortcut = shortcut {
            finalKeyEquivalent = shortcut.mainKey.lowercased()
            finalModifierMask = shortcut.nsModifierFlags
        } else {
            finalKeyEquivalent = keyEquivalent
            finalModifierMask = modifierMask
        }

        let item = NSMenuItem(
            title: NSLocalizedString(titleKey, tableName: "Localizable", bundle: .main, comment: comment),
            action: action,
            keyEquivalent: finalKeyEquivalent
        )
        item.keyEquivalentModifierMask = finalModifierMask
        item.target = target

        if let imageName = imageName,
           let image = NSImage(systemSymbolName: imageName, accessibilityDescription: nil) {
            item.image = image
        }

        return item
    }

    // MARK: - Actions

    @objc private func resetPinnedInteractions() {
        PinnedWindowsManager.shared.resetAllInteractions()
    }

    @objc private func showAllPinnedWindows() {
        PinnedWindowsManager.shared.showAllHidden()
    }
}
