import AppKit
import CoreGraphics
import SwiftUI

// MARK: - TranslationPopoverDelegate

/// Delegate protocol for translation popover events.
@MainActor
protocol TranslationPopoverDelegate: AnyObject {
    /// Called when user dismisses the popover.
    func translationPopoverDidDismiss()
}

// MARK: - TranslationPopoverWindow

/// NSPanel subclass for displaying translation results in a popover below the selection.
/// Shows original text and translated text in a styled floating panel.
final class TranslationPopoverWindow: NSPanel {
    // MARK: - Properties

    /// The anchor rectangle for the popover (in screen coordinates)
    let anchorRect: CGRect

    /// The screen this popover appears on
    let targetScreen: NSScreen

    /// Translation results to display
    private let translations: [TranslationResult]

    /// The content view handling drawing and interaction
    private var popoverView: TranslationPopoverView?

    /// Delegate for popover events
    weak var popoverDelegate: TranslationPopoverDelegate?

    /// Whether the popover is currently positioned
    private var isPositioned = false

    // MARK: - Initialization

    /// Creates a new translation popover window.
    /// - Parameters:
    ///   - anchorRect: The rectangle to anchor the popover below (screen coordinates)
    ///   - screen: The NSScreen containing the anchor
    ///   - translations: Translation results to display
    @MainActor
    init(
        anchorRect: CGRect,
        screen: NSScreen,
        translations: [TranslationResult]
    ) {
        self.anchorRect = anchorRect
        self.targetScreen = screen
        self.translations = translations

        // Initial frame - will be repositioned
        let initialFrame = CGRect(x: 0, y: 0, width: 400, height: 200)

        super.init(
            contentRect: initialFrame,
            styleMask: [.borderless, .nonactivatingPanel],
            backing: .buffered,
            defer: false
        )

        configureWindow()
        setupPopoverView()
        positionPopover()
    }

    // MARK: - Configuration

    @MainActor
    private func configureWindow() {
        // Window properties for floating popover
        level = .floating
        isOpaque = false
        backgroundColor = .clear
        ignoresMouseEvents = false
        hasShadow = true

        hidesOnDeactivate = true

        // Behavior
        collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .ignoresCycle]
        isMovable = false
        isMovableByWindowBackground = false

        // Accept mouse events
        acceptsMouseMovedEvents = true
    }

    @MainActor
    private func setupPopoverView() {
        let view = TranslationPopoverView(
            translations: translations,
            window: self
        )
        self.contentView = view
        self.popoverView = view
    }

    /// Positions the popover below the anchor rectangle
    @MainActor
    private func positionPopover() {
        guard let popoverView = popoverView else { return }

        // Calculate the size needed for the content
        let contentSize = popoverView.sizeThatFits(NSSize(width: 380, height: 1000))

        // Calculate position below anchor rect
        let anchorBottom = anchorRect.maxY
        let anchorCenter = anchorRect.midX

        // Position below anchor with some padding
        let padding: CGFloat = 12
        var origin = CGPoint(
            x: anchorCenter - contentSize.width / 2,
            y: anchorBottom - contentSize.height - padding
        )

        // Keep within screen bounds (horizontal)
        if origin.x < 20 {
            origin.x = 20
        } else if origin.x + contentSize.width > targetScreen.frame.width - 20 {
            origin.x = targetScreen.frame.width - contentSize.width - 20
        }

        // Keep within screen bounds (vertical - flip if needed)
        if origin.y < 20 {
            // Not enough space below, try above
            origin.y = anchorRect.minY + padding
        }

        // Ensure still within bounds
        if origin.y < 20 {
            origin.y = 20
        } else if origin.y + contentSize.height > targetScreen.frame.height - 20 {
            origin.y = targetScreen.frame.height - contentSize.height - 20
        }

        let newFrame = CGRect(origin: origin, size: contentSize)
        setFrame(newFrame, display: true)
        isPositioned = true
    }

    // MARK: - Public API

    /// Shows the popover window
    @MainActor
    func showPopover() {
        makeKeyAndOrderFront(nil)
        orderFrontRegardless()

        // Add close button after window is positioned
        setupCloseButton()
    }

    private var closeButton: NSButton?

    private func setupCloseButton() {
        guard closeButton == nil else { return }

        let buttonSize: CGFloat = 28
        let margin: CGFloat = 8
        let button = NSButton(frame: NSRect(
            x: (contentView?.bounds.width ?? 400) - buttonSize - margin,
            y: (contentView?.bounds.height ?? 200) - buttonSize - margin,
            width: buttonSize,
            height: buttonSize
        ))
        button.bezelStyle = NSButton.BezelStyle.circular
        button.title = "×"
        button.font = NSFont.systemFont(ofSize: 18, weight: .medium)
        button.target = self
        button.action = #selector(closeWindow)
        button.autoresizingMask = NSView.AutoresizingMask([.minXMargin, .minYMargin])
        contentView?.addSubview(button)
        closeButton = button
    }

    @objc private func closeWindow() {
        orderOut(nil)
    }

    // MARK: - NSWindow Overrides

    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { true }
    override var acceptsFirstResponder: Bool { true }

    override func keyDown(with event: NSEvent) {
        // Escape key dismisses popover
        if event.keyCode == 53 { // Escape
            popoverDelegate?.translationPopoverDidDismiss()
            return
        }

        super.keyDown(with: event)
    }

    override func mouseDown(with event: NSEvent) {
        // Check if click is outside the popover content
        let locationInWindow = event.locationInWindow
        guard let contentView = contentView else {
            super.mouseDown(with: event)
            return
        }

        // Convert window coordinates to view coordinates
        let locationInView = contentView.convert(locationInWindow, from: nil)

        if !contentView.bounds.contains(locationInView) {
            // Click outside - dismiss
            popoverDelegate?.translationPopoverDidDismiss()
            return
        }

        super.mouseDown(with: event)
    }
}

// MARK: - TranslationPopoverController

/// Controller for managing translation popover lifecycle.
@MainActor
final class TranslationPopoverController {
    // MARK: - Properties

    /// Shared instance
    static let shared = TranslationPopoverController()

    /// The current popover window
    private var popoverWindow: TranslationPopoverWindow?

    /// Delegate for popover events
    weak var popoverDelegate: TranslationPopoverDelegate?

    /// Callback for when popover is dismissed
    var onDismiss: (() -> Void)?

    // MARK: - Initialization

    private init() {}

    // MARK: - Public API

    /// Presents translation popover with the given results.
    /// - Parameters:
    ///   - anchorRect: The rectangle to anchor below (in screen coordinates)
    ///   - translations: Array of translation results
    func presentPopover(
        anchorRect: CGRect,
        translations: [TranslationResult]
    ) {
        // Dismiss any existing popover
        dismissPopover()

        guard let screen = NSScreen.main else { return }

        // Create popover window
        let popover = TranslationPopoverWindow(
            anchorRect: anchorRect,
            screen: screen,
            translations: translations
        )
        popover.popoverDelegate = self

        self.popoverWindow = popover
        popover.showPopover()
    }

    /// Dismisses the current popover.
    func dismissPopover() {
        popoverWindow?.close()
        popoverWindow = nil
        onDismiss?()
    }
}

// MARK: - TranslationPopoverController + TranslationPopoverDelegate

extension TranslationPopoverController: TranslationPopoverDelegate {
    func translationPopoverDidDismiss() {
        dismissPopover()
        onDismiss?()
    }
}
