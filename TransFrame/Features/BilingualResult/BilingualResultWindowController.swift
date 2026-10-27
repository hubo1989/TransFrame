import AppKit
import SwiftUI

final class BilingualResultPanel: NSPanel {
    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { true }
}

@MainActor
final class BilingualResultWindowController: NSObject {
    static let shared = BilingualResultWindowController()

    private var window: NSWindow?
    private var viewModel: BilingualResultViewModel?

    private override init() {
        super.init()
    }

    /// Calculate window size from image point dimensions
    private func calculateWindowSize(
        imagePointWidth: CGFloat,
        imagePointHeight: CGFloat,
        maxWidth: CGFloat,
        maxHeight: CGFloat,
        minWidth: CGFloat = 400,
        minHeight: CGFloat = 300
    ) -> NSSize {
        // Default to 100% scale, only shrink if image exceeds screen bounds
        let scale: CGFloat
        if imagePointWidth > maxWidth || imagePointHeight > maxHeight {
            scale = min(maxWidth / imagePointWidth, maxHeight / imagePointHeight)
        } else {
            scale = 1.0
        }
        let windowWidth = max(minWidth, imagePointWidth * scale)
        let windowHeight = max(minHeight, imagePointHeight * scale + 50)
        return NSSize(width: windowWidth, height: windowHeight)
    }

    func showLoading(originalImage: CGImage, scaleFactor: CGFloat, message: String? = nil) {
        if let existingWindow = window, existingWindow.isVisible {
            viewModel?.showLoading(originalImage: originalImage, message: message)
            existingWindow.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            return
        }

        let newViewModel = BilingualResultViewModel(image: originalImage, displayScaleFactor: scaleFactor)
        newViewModel.isLoading = true
        newViewModel.loadingMessage = message ?? String(localized: "bilingualResult.loading")
        self.viewModel = newViewModel

        let contentView = BilingualResultView(viewModel: newViewModel)
        let hostingView = NSHostingView(rootView: contentView)

        let imagePointWidth = CGFloat(originalImage.width) / scaleFactor
        let imagePointHeight = CGFloat(originalImage.height) / scaleFactor
        let screenWidth = NSScreen.main?.frame.width ?? 1920
        let screenHeight = NSScreen.main?.frame.height ?? 1080

        let windowSize = calculateWindowSize(
            imagePointWidth: imagePointWidth,
            imagePointHeight: imagePointHeight,
            maxWidth: screenWidth * 0.9,
            maxHeight: screenHeight * 0.85
        )

        let newWindow = BilingualResultPanel(
            contentRect: NSRect(x: 0, y: 0, width: windowSize.width, height: windowSize.height),
            // .titled + .fullSizeContentView keeps the borderless look while
            // retaining system edge-resize (a pure .borderless mask ignores
            // .resizable for user resizing) and a VoiceOver window title.
            styleMask: [.titled, .resizable, .fullSizeContentView],
            backing: .buffered,
            defer: false
        )

        newWindow.titleVisibility = .hidden
        newWindow.titlebarAppearsTransparent = true
        newWindow.isOpaque = false
        newWindow.backgroundColor = .clear
        newWindow.hasShadow = true
        newWindow.isMovableByWindowBackground = true
        newWindow.contentView = hostingView
        newWindow.center()
        newWindow.isReleasedWhenClosed = false
        newWindow.delegate = self
        newWindow.minSize = NSSize(width: 400, height: 300)
        newWindow.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]

        self.window = newWindow
        newWindow.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    func showResult(image: CGImage, scaleFactor: CGFloat, translatedText: String? = nil) {
        viewModel?.showResult(image: image, displayScaleFactor: scaleFactor, translatedText: translatedText)

        if let window = window {
            let imagePointWidth = CGFloat(image.width) / scaleFactor
            let imagePointHeight = CGFloat(image.height) / scaleFactor
            let screenWidth = NSScreen.main?.frame.width ?? 1920
            let screenHeight = NSScreen.main?.frame.height ?? 1080

            let windowSize = calculateWindowSize(
                imagePointWidth: imagePointWidth,
                imagePointHeight: imagePointHeight,
                maxWidth: screenWidth * 0.9,
                maxHeight: screenHeight * 0.85
            )

            let newFrame = NSRect(
                x: window.frame.origin.x,
                y: window.frame.origin.y + window.frame.height - windowSize.height,
                width: windowSize.width,
                height: windowSize.height
            )
            window.setFrame(newFrame, display: true, animate: true)
        }
    }

    func showError(_ message: String) {
        viewModel?.showError(message)
    }

    func show(image: CGImage, scaleFactor: CGFloat) {
        if let existingWindow = window, existingWindow.isVisible {
            viewModel?.updateImage(image, displayScaleFactor: scaleFactor)
            existingWindow.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            return
        }

        let newViewModel = BilingualResultViewModel(image: image, displayScaleFactor: scaleFactor)
        self.viewModel = newViewModel

        let contentView = BilingualResultView(viewModel: newViewModel)
        let hostingView = NSHostingView(rootView: contentView)

        let imagePointWidth = CGFloat(image.width) / scaleFactor
        let imagePointHeight = CGFloat(image.height) / scaleFactor

        let windowSize = calculateWindowSize(
            imagePointWidth: imagePointWidth,
            imagePointHeight: imagePointHeight,
            maxWidth: 1200,
            maxHeight: 800
        )

        let newWindow = BilingualResultPanel(
            contentRect: NSRect(x: 0, y: 0, width: windowSize.width, height: windowSize.height),
            // .titled + .fullSizeContentView keeps the borderless look while
            // retaining system edge-resize (a pure .borderless mask ignores
            // .resizable for user resizing) and a VoiceOver window title.
            styleMask: [.titled, .resizable, .fullSizeContentView],
            backing: .buffered,
            defer: false
        )

        newWindow.titleVisibility = .hidden
        newWindow.titlebarAppearsTransparent = true
        newWindow.isOpaque = false
        newWindow.backgroundColor = .clear
        newWindow.hasShadow = true
        newWindow.isMovableByWindowBackground = true
        newWindow.contentView = hostingView
        newWindow.center()
        newWindow.isReleasedWhenClosed = false
        newWindow.delegate = self
        newWindow.minSize = NSSize(width: 400, height: 300)
        newWindow.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]

        self.window = newWindow
        newWindow.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    func close() {
        window?.close()
    }
}

extension BilingualResultWindowController: NSWindowDelegate {
    func windowWillClose(_ notification: Notification) {
        window = nil
        viewModel = nil
    }
}
