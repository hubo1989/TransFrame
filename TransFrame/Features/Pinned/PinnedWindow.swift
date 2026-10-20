import AppKit
import SwiftUI

/// A window that displays a pinned screenshot with annotations.
/// Can be set to always stay on top of other windows.
final class PinnedWindow: NSPanel {
    // MARK: - Properties

    private let screenshot: Screenshot
    private let annotations: [Annotation]
    private let id: UUID

    /// Callback when the window is closed
    var onClose: (() -> Void)?
    
    /// Flag to prevent reentrant close calls
    var isProgrammaticClose = false

    // MARK: - Initialization

    @MainActor
    init(screenshot: Screenshot, annotations: [Annotation]) {
        self.screenshot = screenshot
        self.annotations = annotations
        self.id = screenshot.id

        // Calculate window size based on image dimensions
        let scaleFactor = screenshot.sourceDisplay.scaleFactor
        let imageSize = CGSize(
            width: CGFloat(screenshot.image.width) / scaleFactor,
            height: CGFloat(screenshot.image.height) / scaleFactor
        )

        // Limit window size to 60% of screen
        let windowSize = Self.calculateWindowSize(for: imageSize)
        let contentRect = Self.calculateCenteredRect(size: windowSize)

        super.init(
            contentRect: contentRect,
            styleMask: [.borderless, .nonactivatingPanel, .hudWindow],
            backing: .buffered,
            defer: false
        )

        configureWindow()
        setupContentView()
    }

    // MARK: - Configuration

    @MainActor
    private func configureWindow() {
        // Window behavior - always on top but not intrusive
        level = .floating
        collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .stationary]

        // Appearance
        isOpaque = false
        backgroundColor = .clear
        hasShadow = true

        // Behavior
        isMovableByWindowBackground = true
        hidesOnDeactivate = false
        isReleasedWhenClosed = false

        // Allow becoming key for keyboard events
        _ = canBecomeKey
    }

    @MainActor
    private func setupContentView() {
        let contentView = PinnedWindowContent(
            image: screenshot.image,
            annotations: annotations,
            scaleFactor: screenshot.sourceDisplay.scaleFactor,
            onClose: { [weak self] in
                self?.close()
            }
        )

        let hostingView = NSHostingView(rootView: contentView)
        hostingView.autoresizingMask = [.width, .height]
        self.contentView = hostingView
    }

    // MARK: - Window Sizing

    @MainActor
    private static func calculateWindowSize(for imageSize: CGSize) -> NSSize {
        guard let screen = NSScreen.main else {
            return NSSize(width: min(imageSize.width, 800), height: min(imageSize.height, 600))
        }

        let screenFrame = screen.visibleFrame

        // Leave padding around the window
        let maxWidth = screenFrame.width * 0.6
        let maxHeight = screenFrame.height * 0.6

        // Calculate scale factor to fit within screen
        let widthScale = maxWidth / imageSize.width
        let heightScale = maxHeight / imageSize.height
        let scale = min(widthScale, heightScale, 1.0) // Don't scale up

        return NSSize(
            width: max(imageSize.width * scale, 150),
            height: max(imageSize.height * scale, 100)
        )
    }

    @MainActor
    private static func calculateCenteredRect(size: NSSize) -> NSRect {
        guard let screen = NSScreen.main else {
            return NSRect(origin: .zero, size: size)
        }

        let screenFrame = screen.visibleFrame
        let x = screenFrame.midX - size.width / 2
        let y = screenFrame.midY - size.height / 2

        return NSRect(x: x, y: y, width: size.width, height: size.height)
    }

    // MARK: - NSPanel Overrides

    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { false }

    // MARK: - Lifecycle

    override func close() {
        // Only call onClose for user-initiated closes
        if !isProgrammaticClose {
            onClose?()
        }
        super.close()
    }

    // MARK: - Public API

    /// Shows the pinned window
    @MainActor
    func show() {
        makeKeyAndOrderFront(nil)
    }

    /// Returns the unique identifier for this pinned window
    var pinnedId: UUID { id }
}

// MARK: - Pinned Window Content View

private struct PinnedWindowContent: View {
    let image: CGImage
    let annotations: [Annotation]
    let scaleFactor: CGFloat
    let onClose: () -> Void

    @State private var isHovering = false

    var body: some View {
        ZStack(alignment: .topTrailing) {
            // Image with annotations
            GeometryReader { geometry in
                let viewSize = geometry.size
                let imageSize = CGSize(width: CGFloat(image.width), height: CGFloat(image.height))
                let scale = min(viewSize.width / imageSize.width, viewSize.height / imageSize.height)

                ZStack {
                    // Background
                    Color.black.opacity(0.1)

                    // Image
                    Image(nsImage: NSImage(cgImage: image, size: NSSize(
                        width: CGFloat(image.width) / scaleFactor,
                        height: CGFloat(image.height) / scaleFactor
                    )))
                    .resizable()
                    .aspectRatio(contentMode: .fit)

                    let displayedWidth = imageSize.width * scale
                    let displayedHeight = imageSize.height * scale

                    // Annotations overlay
                    AnnotationCanvas(
                        annotations: annotations,
                        currentAnnotation: nil,
                        canvasSize: CGSize(width: displayedWidth, height: displayedHeight),
                        scale: scale,
                        selectedIndex: nil,
                        sourceImage: image
                    )
                    .frame(width: displayedWidth, height: displayedHeight)
                    .aspectRatio(contentMode: .fit)
                }
            }
            .cornerRadius(8)

            // Close button (visible on hover)
            if isHovering {
                Button(action: onClose) {
                    Image(systemName: "xmark.circle.fill")
                        .font(.title2)
                        .foregroundStyle(.white, .gray)
                        .shadow(radius: 2)
                }
                .buttonStyle(.plain)
                .padding(8)
                .transition(.opacity)
            }
        }
        .onHover { hovering in
            withAnimation(.easeInOut(duration: 0.15)) {
                isHovering = hovering
            }
        }
        .frame(minWidth: 150, minHeight: 100)
    }
}
