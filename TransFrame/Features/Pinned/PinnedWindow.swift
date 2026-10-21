import AppKit
import SwiftUI
import Combine

/// A floating window that displays a pinned capture document with interactive controls.
/// Supports 1:1 in-place positioning, zoom, opacity adjustment, and click-through mode.
final class PinnedWindow: NSPanel {
    // MARK: - Properties

    let documentID: UUID
    private(set) var currentDocument: CaptureDocument?
    private var cancellables = Set<AnyCancellable>()

    /// Callback when the window is closed
    var onClose: (() -> Void)?

    /// Flag to prevent reentrant close calls
    var isProgrammaticClose = false

    /// Current zoom scale factor
    private(set) var zoomScale: CGFloat = 1.0

    /// Base initial frame before zooming
    private var baseFrame: NSRect = .zero

    // MARK: - Initialization

    @MainActor
    init(document: CaptureDocument) {
        self.documentID = document.id
        self.currentDocument = document

        let primaryHeight = NSScreen.screens.first(where: { $0.frame.origin == .zero })?.frame.height ?? 1080
        let cocoaRect = CoordinateMapper.quartzToCocoa(document.sourceScreenRect, primaryHeight: primaryHeight).rawValue

        // In-place 1:1 screen frame
        let initialFrame: NSRect
        if cocoaRect.width > 10 && cocoaRect.height > 10 {
            initialFrame = cocoaRect
        } else {
            let defaultSize = NSSize(
                width: document.visiblePixelSize.width / 2.0,
                height: document.visiblePixelSize.height / 2.0
            )
            initialFrame = Self.calculateCenteredRect(size: defaultSize)
        }
        self.baseFrame = initialFrame

        super.init(
            contentRect: initialFrame,
            styleMask: [.borderless, .nonactivatingPanel, .hudWindow],
            backing: .buffered,
            defer: false
        )

        configureWindow()
        setupContentView()
        observeDocumentChanges()
    }

    @MainActor
    init(screenshot: Screenshot, annotations: [Annotation]) {
        self.documentID = screenshot.id
        self.currentDocument = nil

        let scaleFactor = screenshot.sourceDisplay.scaleFactor
        let imageSize = CGSize(
            width: CGFloat(screenshot.image.width) / scaleFactor,
            height: CGFloat(screenshot.image.height) / scaleFactor
        )
        let windowSize = Self.calculateWindowSize(for: imageSize)
        let contentRect = Self.calculateCenteredRect(size: windowSize)
        self.baseFrame = contentRect

        super.init(
            contentRect: contentRect,
            styleMask: [.borderless, .nonactivatingPanel, .hudWindow],
            backing: .buffered,
            defer: false
        )

        configureWindow()
        setupLegacyContentView(screenshot: screenshot, annotations: annotations)
    }

    // MARK: - Configuration

    @MainActor
    private func configureWindow() {
        level = .floating
        collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .stationary]
        isOpaque = false
        backgroundColor = .clear
        hasShadow = true
        isMovableByWindowBackground = true
        hidesOnDeactivate = false
        isReleasedWhenClosed = false
    }

    @MainActor
    private func setupContentView() {
        guard let doc = currentDocument else { return }
        let contentView = PinnedDocumentContent(
            document: doc,
            onClose: { [weak self] in
                self?.close()
            },
            onToggleClickThrough: { [weak self] in
                self?.toggleClickThrough()
            },
            onOpacityChange: { [weak self] alpha in
                self?.alphaValue = alpha
            },
            onZoomChange: { [weak self] scale in
                self?.applyZoom(scale)
            }
        )

        let hostingView = NSHostingView(rootView: contentView)
        hostingView.autoresizingMask = [.width, .height]
        self.contentView = hostingView
    }

    @MainActor
    private func setupLegacyContentView(screenshot: Screenshot, annotations: [Annotation]) {
        let dummyDoc = CaptureDocument(
            id: screenshot.id,
            baseImage: screenshot.image,
            sourceScreenRect: QuartzGlobalRect(screenshot.sourceDisplay.frame),
            sourceDisplay: screenshot.sourceDisplay,
            annotations: annotations
        )
        self.currentDocument = dummyDoc
        setupContentView()
    }

    private func observeDocumentChanges() {
        NotificationCenter.default.publisher(for: .captureDocumentDidUpdate)
            .compactMap { $0.object as? CaptureDocument }
            .filter { [weak self] in $0.id == self?.documentID }
            .receive(on: DispatchQueue.main)
            .sink { [weak self] updatedDoc in
                guard let self else { return }
                self.currentDocument = updatedDoc
                self.setupContentView()
            }
            .store(in: &cancellables)
    }

    // MARK: - Interactive Controls

    /// Toggles mouse click-through for this window
    @MainActor
    func toggleClickThrough() {
        let enableClickThrough = !ignoresMouseEvents
        setClickThrough(enableClickThrough)
    }

    @MainActor
    func setClickThrough(_ enable: Bool) {
        ignoresMouseEvents = enable
        alphaValue = enable ? 0.65 : 1.0
    }

    @MainActor
    func applyZoom(_ factor: CGFloat) {
        let clamped = max(0.25, min(4.0, factor))
        self.zoomScale = clamped
        let newWidth = baseFrame.width * clamped
        let newHeight = baseFrame.height * clamped
        let newOrigin = CGPoint(
            x: frame.midX - newWidth / 2.0,
            y: frame.midY - newHeight / 2.0
        )
        setFrame(NSRect(origin: newOrigin, size: NSSize(width: newWidth, height: newHeight)), display: true, animate: false)
    }

    // MARK: - Window Sizing Helpers

    @MainActor
    private static func calculateWindowSize(for imageSize: CGSize) -> NSSize {
        guard let screen = NSScreen.main else {
            return NSSize(width: min(imageSize.width, 800), height: min(imageSize.height, 600))
        }

        let screenFrame = screen.visibleFrame
        let maxWidth = screenFrame.width * 0.7
        let maxHeight = screenFrame.height * 0.7

        let widthScale = maxWidth / imageSize.width
        let heightScale = maxHeight / imageSize.height
        let scale = min(widthScale, heightScale, 1.0)

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

    override var canBecomeKey: Bool { !ignoresMouseEvents }
    override var canBecomeMain: Bool { false }

    override func close() {
        if !isProgrammaticClose {
            onClose?()
        }
        super.close()
    }

    @MainActor
    func show() {
        makeKeyAndOrderFront(nil)
    }

    var pinnedId: UUID { documentID }
}

// MARK: - Pinned Document Content View

private struct PinnedDocumentContent: View {
    let document: CaptureDocument
    let onClose: () -> Void
    let onToggleClickThrough: () -> Void
    let onOpacityChange: (CGFloat) -> Void
    let onZoomChange: (CGFloat) -> Void

    @State private var isHovering = false
    @State private var currentOpacity: CGFloat = 1.0
    @State private var currentZoom: CGFloat = 1.0
    @State private var renderedImage: NSImage?

    var body: some View {
        ZStack(alignment: .topTrailing) {
            Group {
                if let rendered = renderedImage {
                    Image(nsImage: rendered)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                } else {
                    Color.black.opacity(0.1)
                }
            }
            .clipShape(RoundedRectangle(cornerRadius: 6))
            .overlay(
                RoundedRectangle(cornerRadius: 6)
                    .stroke(Color.white.opacity(0.2), lineWidth: 1)
            )
            .shadow(color: .black.opacity(0.3), radius: 6, x: 0, y: 3)

            // Hover Controls Toolbar
            if isHovering {
                HStack(spacing: 8) {
                    // Click-through button
                    Button(action: onToggleClickThrough) {
                        Image(systemName: "cursorarrow.slash")
                            .font(.system(size: 11, weight: .bold))
                            .foregroundColor(.white)
                    }
                    .buttonStyle(.plain)
                    .help("点击穿透 (快捷键)")

                    // Close button
                    Button(action: onClose) {
                        Image(systemName: "xmark")
                            .font(.system(size: 11, weight: .bold))
                            .foregroundColor(.white)
                    }
                    .buttonStyle(.plain)
                    .help("关闭钉图")
                }
                .padding(.horizontal, 8)
                .padding(.vertical, 4)
                .background(.ultraThinMaterial, in: Capsule())
                .padding(8)
                .transition(.opacity.combined(with: .scale(scale: 0.9)))
            }
        }
        .onHover { hovering in
            withAnimation(.easeInOut(duration: 0.15)) {
                isHovering = hovering
            }
        }
        .onAppear {
            renderContent()
        }
        .onChange(of: document.editRevision) { _, _ in
            renderContent()
        }
    }

    private func renderContent() {
        if let cgImage = DocumentRenderer.shared.render(document: document) {
            renderedImage = NSImage(cgImage: cgImage, size: .zero)
        }
    }
}
