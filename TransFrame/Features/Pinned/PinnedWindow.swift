import AppKit
import SwiftUI
import Combine

/// A floating window that displays a pinned capture document with interactive controls.
/// Supports 1:1 in-place positioning, zoom, opacity adjustment, and click-through mode.
final class PinnedWindow: NSPanel {
    // MARK: - Properties

    /// Height reserved for the bottom floating toolbar (content spacing + bar).
    /// Window frames and default sizes all reserve this so content is never clipped.
    private static let toolbarReservedHeight: CGFloat = 46

    let documentID: UUID
    private(set) var currentDocument: CaptureDocument?
    private var cancellables = Set<AnyCancellable>()

    /// Callback when the window is closed
    var onClose: (() -> Void)?

    /// Flag to prevent reentrant close calls
    var isProgrammaticClose = false

    /// Current zoom scale factor
    private(set) var zoomScale: CGFloat = 1.0

    /// Accumulated precise scroll delta waiting to trigger a zoom step
    private var pendingZoomDelta: CGFloat = 0

    /// Base initial frame before zooming
    private var baseFrame: NSRect = .zero

    // MARK: - Initialization

    @MainActor
    init(document: CaptureDocument) {
        self.documentID = document.id
        self.currentDocument = document

        let primaryHeight = NSScreen.screens.first(where: { $0.frame.origin == .zero })?.frame.height ?? 1080
        let cocoaRect = CoordinateMapper.quartzToCocoa(document.sourceScreenRect, primaryHeight: primaryHeight).rawValue

        // In-place 1:1 screen frame + toolbar space below
        let initialFrame: NSRect
        if cocoaRect.width > 10 && cocoaRect.height > 10 {
            var f = cocoaRect
            f.size.height += Self.toolbarReservedHeight
            f.origin.y -= Self.toolbarReservedHeight
            initialFrame = f
        } else {
            let defaultSize = NSSize(
                width: max(document.visiblePixelSize.width / 2.0, 220),
                height: max(document.visiblePixelSize.height / 2.0 + Self.toolbarReservedHeight, 160)
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
        var windowSize = Self.calculateWindowSize(for: imageSize)
        windowSize.height += Self.toolbarReservedHeight
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
            isPinnedAlwaysOnTop: level == .floating,
            onClose: { [weak self] in
                self?.close()
            },
            onEdit: { [weak self] in
                guard let self, let doc = self.currentDocument else { return }
                self.close()
                PreviewWindowController.shared.showPreview(for: doc.toLegacyScreenshot())
            },
            onToggleAlwaysOnTop: { [weak self] in
                guard let self else { return }
                if self.level == .floating {
                    self.level = .normal
                } else {
                    self.level = .floating
                }
            },
            onToggleClickThrough: { [weak self] in
                self?.toggleClickThrough()
            },
            onCopy: { [weak self] in
                self?.copyRenderedImageToPasteboard()
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

    override func keyDown(with event: NSEvent) {
        if event.keyCode == 53 || event.keyCode == 51 { // Esc or Delete
            close()
            return
        }
        // 无修饰键的 E 才触发编辑；⌘E/⌥E 等组合留给系统语义
        let bareModifiers = event.modifierFlags
            .intersection(.deviceIndependentFlagsMask)
            .subtracting([.capsLock])
        if bareModifiers.isEmpty, event.charactersIgnoringModifiers?.lowercased() == "e" {
            if let doc = currentDocument {
                close()
                PreviewWindowController.shared.showPreview(for: doc.toLegacyScreenshot())
                return
            }
        }
        // ⌘C 复制渲染图（与工具栏复制按钮一致）
        if event.modifierFlags.contains(.command),
           event.charactersIgnoringModifiers?.lowercased() == "c" {
            copyRenderedImageToPasteboard()
            return
        }
        super.keyDown(with: event)
    }

    override func scrollWheel(with event: NSEvent) {
        if event.modifierFlags.contains(.option) {
            // Option + Scroll Wheel: Adjust opacity between 0.2 and 1.0
            let delta = event.scrollingDeltaY * 0.01
            let newAlpha = max(0.2, min(1.0, alphaValue + delta))
            alphaValue = newAlpha
        } else if event.hasPreciseScrollingDeltas {
            // 触控板精确滚动：累积达到阈值才缩放一步，避免惯性滚动过度灵敏
            pendingZoomDelta += event.scrollingDeltaY
            if abs(pendingZoomDelta) >= 10 {
                applyZoom(zoomScale * (pendingZoomDelta > 0 ? 1.05 : 0.95))
                pendingZoomDelta = 0
            }
        } else {
            // 普通滚轮（离散步进）
            let zoomDelta: CGFloat = event.scrollingDeltaY > 0 ? 1.05 : 0.95
            applyZoom(zoomScale * zoomDelta)
        }
    }

    /// Copies the rendered document (with annotations) to the pasteboard,
    /// shared by the toolbar button and the ⌘C key equivalent.
    @MainActor
    private func copyRenderedImageToPasteboard() {
        guard let doc = currentDocument,
              let cgImg = DocumentRenderer.shared.render(document: doc) else { return }
        let nsImg = NSImage(cgImage: cgImg, size: .zero)
        NSPasteboard.general.clearContents()
        NSPasteboard.general.writeObjects([nsImg])
    }

    override func mouseUp(with event: NSEvent) {
        super.mouseUp(with: event)
        if event.clickCount == 2 {
            // Double click: Reset to 100% original size
            applyZoom(1.0)
        }
    }

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
    var isPinnedAlwaysOnTop: Bool
    let onClose: () -> Void
    let onEdit: () -> Void
    let onToggleAlwaysOnTop: () -> Void
    let onToggleClickThrough: () -> Void
    let onCopy: () -> Void
    let onOpacityChange: (CGFloat) -> Void
    let onZoomChange: (CGFloat) -> Void

    @State private var showCopiedToast = false
    @State private var alwaysOnTopState: Bool = true
    @State private var renderedImage: NSImage?

    var body: some View {
        VStack(spacing: 6) {
            // 校准置顶状态为窗口真实 level（视图重建/外部变更后保持一致）
            EmptyView()
                .onAppear { alwaysOnTopState = isPinnedAlwaysOnTop }

            // Pinned Image
            Group {
                if let rendered = renderedImage {
                    Image(nsImage: rendered)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                } else {
                    Color.black.opacity(0.1)
                }
            }
            .clipShape(RoundedRectangle(cornerRadius: 8))
            .overlay(
                RoundedRectangle(cornerRadius: 8)
                    .stroke(Color.white.opacity(0.2), lineWidth: 1)
            )
            .shadow(color: .black.opacity(0.35), radius: 8, x: 0, y: 3)

            // Bottom Permanent Floating Action Toolbar
            HStack(spacing: 8) {
                // Edit
                Button(action: onEdit) {
                    HStack(spacing: 4) {
                        Image(systemName: "pencil.and.outline")
                        Text("编辑")
                    }
                    .font(.system(size: 11, weight: .medium))
                    .foregroundStyle(.primary)
                }
                .buttonStyle(.plain)
                .padding(.horizontal, 6)
                .padding(.vertical, 3)
                .background(Color.primary.opacity(0.08), in: RoundedRectangle(cornerRadius: 6))
                .customTooltip("在无边框编辑器中编辑 (E)")

                Divider().frame(height: 12)

                // Always on top toggle
                Button(action: {
                    alwaysOnTopState.toggle()
                    onToggleAlwaysOnTop()
                }) {
                    Image(systemName: alwaysOnTopState ? "pin.fill" : "pin.slash")
                        .font(.system(size: 12, weight: .bold))
                        .foregroundColor(alwaysOnTopState ? .accentColor : .secondary)
                }
                .buttonStyle(.plain)
                .padding(3)
                .customTooltip(alwaysOnTopState ? "当前：置顶 (点击取消置顶)" : "当前：普通窗口 (点击置顶)")

                // Click-through toggle
                Button(action: onToggleClickThrough) {
                    Image(systemName: "cursorarrow.slash")
                        .font(.system(size: 12, weight: .medium))
                        .foregroundColor(.secondary)
                }
                .buttonStyle(.plain)
                .padding(3)
                .customTooltip("鼠标穿透")

                // Copy to clipboard
                Button(action: {
                    onCopy()
                    showCopiedToast = true
                    DispatchQueue.main.asyncAfter(deadline: .now() + 1.2) {
                        showCopiedToast = false
                    }
                }) {
                    Image(systemName: showCopiedToast ? "checkmark" : "doc.on.clipboard")
                        .font(.system(size: 12, weight: .bold))
                        .foregroundColor(showCopiedToast ? .green : .secondary)
                }
                .buttonStyle(.plain)
                .padding(3)
                .customTooltip("复制图片 (⌘C)")

                Divider().frame(height: 12)

                // Close / Remove
                Button(action: onClose) {
                    Image(systemName: "xmark")
                        .font(.system(size: 11, weight: .bold))
                        .foregroundColor(.red.opacity(0.85))
                }
                .buttonStyle(.plain)
                .padding(3)
                .customTooltip("关闭钉图 (Esc / Delete)")
            }
            .cursor(.arrow)
            .padding(.horizontal, 10)
            .padding(.vertical, 4)
            .background(.ultraThinMaterial, in: Capsule())
            .shadow(color: .black.opacity(0.25), radius: 6, y: 2)
        }
        .padding(6)
        .onAppear {
            alwaysOnTopState = isPinnedAlwaysOnTop
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
