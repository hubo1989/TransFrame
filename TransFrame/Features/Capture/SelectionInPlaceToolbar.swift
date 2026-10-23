import AppKit
import SwiftUI
import Observation

// MARK: - Toolbar Action Protocol

@MainActor
public protocol SelectionToolbarDelegate: AnyObject {
    func toolbarDidSelectAction(_ action: SelectionInPlaceToolbar.Action)
    func toolbarDidChangeTool(_ tool: SelectionInPlaceToolbar.AnnotationTool?)
    func toolbarDidChangeColor(_ color: NSColor)
    func toolbarDidChangeLineWidth(_ width: CGFloat)
    func toolbarDidRequestUndo()
    func toolbarDidRequestRedo()
    func toolbarDidUpdateLayout()
    func toolbarDidChangeMosaicIntensity(_ blockSize: Int)
}

extension SelectionToolbarDelegate {
    public func toolbarDidUpdateLayout() {}
    public func toolbarDidChangeMosaicIntensity(_ blockSize: Int) {}
}

// MARK: - Observable Toolbar State

@MainActor
@Observable
public final class SelectionToolbarState {
    public var activeTool: SelectionInPlaceToolbar.AnnotationTool?
    public var activeColor: Color = .red
    public var activeLineWidth: CGFloat = 3.0
    public var mosaicBlockSize: Int = 10
    public var showStyleBar: Bool = false
    public var isBeautifyEnabled: Bool = false
    public var canUndo: Bool = false
    public var canRedo: Bool = false

    public init() {}
}

// MARK: - SelectionInPlaceToolbar

/// In-place floating toolbar displayed immediately adjacent to the selected region on frozen overlay.
public final class SelectionInPlaceToolbar: NSView {
    public enum Action: Sendable {
        case copy
        case pin
        case ocr
        case translate
        case save
        case upload
        case beautify
        case smartMosaic
        case cancel
    }

    public enum AnnotationTool: String, CaseIterable, Sendable {
        case rectangle
        case ellipse
        case line
        case arrow
        case pen
        case text
        case numberLabel
        case highlight
        case mosaic
        case privacyRedaction

        public var displayName: String {
            switch self {
            case .rectangle: return String(localized: "tool.rectangle")
            case .ellipse: return String(localized: "tool.ellipse")
            case .line: return String(localized: "tool.line")
            case .arrow: return String(localized: "tool.arrow")
            case .pen: return String(localized: "tool.freehand")
            case .text: return String(localized: "tool.text")
            case .numberLabel: return String(localized: "tool.numberLabel")
            case .highlight: return String(localized: "tool.highlight")
            case .mosaic: return String(localized: "tool.mosaic")
            case .privacyRedaction: return String(localized: "tool.privacyRedaction")
            }
        }

        public var shortcutKey: String {
            switch self {
            case .rectangle: return "R"
            case .ellipse: return "O"
            case .line: return "L"
            case .arrow: return "A"
            case .pen: return "D"
            case .text: return "T"
            case .numberLabel: return "N"
            case .highlight: return "H"
            case .mosaic: return "M"
            case .privacyRedaction: return "X"
            }
        }

        public var tooltip: String {
            "\(displayName) (\(shortcutKey))"
        }

        public var cursor: NSCursor {
            switch self {
            case .text:
                return .iBeam
            case .pen:
                return NSCursor.symbolCursor(systemName: "pencil", hotSpot: NSPoint(x: 3, y: 17)) ?? .crosshair
            case .highlight:
                return NSCursor.symbolCursor(systemName: "highlighter", hotSpot: NSPoint(x: 3, y: 17)) ?? .crosshair
            case .mosaic:
                return NSCursor.symbolCursor(systemName: "checkerboard.rectangle", hotSpot: NSPoint(x: 10, y: 10)) ?? .crosshair
            case .privacyRedaction:
                return NSCursor.symbolCursor(systemName: "eye.slash.fill", hotSpot: NSPoint(x: 10, y: 10)) ?? .crosshair
            case .numberLabel:
                return NSCursor.symbolCursor(systemName: "number.circle.fill", hotSpot: NSPoint(x: 10, y: 10)) ?? .crosshair
            case .rectangle, .ellipse, .line, .arrow:
                return .crosshair
            }
        }
    }

    public weak var delegate: SelectionToolbarDelegate?
    public let state = SelectionToolbarState()

    public var activeTool: AnnotationTool? {
        get { state.activeTool }
        set {
            state.activeTool = newValue
            state.showStyleBar = (newValue != nil)
        }
    }

    public var selectedColor: NSColor {
        get { NSColor(state.activeColor) }
        set { state.activeColor = Color(nsColor: newValue) }
    }

    public var selectedLineWidth: CGFloat {
        get { state.activeLineWidth }
        set { state.activeLineWidth = newValue }
    }

    public var preferredSize: CGSize {
        CGSize(width: 520, height: state.showStyleBar ? 78 : 42)
    }

    private var hostingView: NSHostingView<ToolbarContentView>?

    override public var isFlipped: Bool { true }

    public override func resetCursorRects() {
        super.resetCursorRects()
        addCursorRect(bounds, cursor: .arrow)
    }

    /// Consume every click within the bar so edge clicks can never fall
    /// through to the overlay and cancel/re-pick the selection. Buttons
    /// still get their events via `super` (with padded hit areas); the
    /// leftover dead zones become inert instead of destructive.
    public override func hitTest(_ point: NSPoint) -> NSView? {
        guard bounds.contains(point) else { return nil }
        return super.hitTest(point) ?? self
    }

    override public init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupViews()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupViews()
    }

    private func setupViews() {
        wantsLayer = true
        clipsToBounds = false
        layer?.cornerRadius = 8
        layer?.masksToBounds = false
        layer?.shadowColor = NSColor.black.cgColor
        layer?.shadowOpacity = 0.35
        layer?.shadowRadius = 8
        layer?.shadowOffset = CGSize(width: 0, height: -2)

        let content = ToolbarContentView(
            state: state,
            onAction: { [weak self] action in
                self?.delegate?.toolbarDidSelectAction(action)
            },
            onToolSelected: { [weak self] tool in
                guard let self else { return }
                self.state.activeTool = tool
                self.delegate?.toolbarDidChangeTool(tool)
                self.delegate?.toolbarDidUpdateLayout()
            },
            onColorSelected: { [weak self] color in
                self?.delegate?.toolbarDidChangeColor(color)
            },
            onLineWidthSelected: { [weak self] width in
                self?.delegate?.toolbarDidChangeLineWidth(width)
            },
            onMosaicIntensitySelected: { [weak self] blockSize in
                self?.delegate?.toolbarDidChangeMosaicIntensity(blockSize)
            },
            onUndo: { [weak self] in
                self?.delegate?.toolbarDidRequestUndo()
            },
            onRedo: { [weak self] in
                self?.delegate?.toolbarDidRequestRedo()
            }
        )

        let hosting = NSHostingView(rootView: content)
        hosting.clipsToBounds = false
        hosting.layer?.masksToBounds = false
        hosting.autoresizingMask = [.width, .height]
        hosting.frame = bounds
        addSubview(hosting)
        self.hostingView = hosting
    }

    /// Calculates ideal toolbar frame relative to selection rectangle in flipped local view coordinates (Y-down).
    public static func preferredFrame(
        for selectionRect: CGRect,
        in bounds: CGRect,
        toolbarSize: CGSize = CGSize(width: 520, height: 42)
    ) -> CGRect {
        // Horizontal positioning:
        // Try aligning right edge of toolbar to right edge of selection
        var originX = selectionRect.maxX - toolbarSize.width
        // If selection is narrower than toolbar, align toolbar to center of selection
        if selectionRect.width < toolbarSize.width {
            originX = selectionRect.midX - toolbarSize.width / 2
        }
        // Clamp to screen bounds with padding
        let minX = bounds.minX + 12
        let maxX = bounds.maxX - toolbarSize.width - 12
        originX = min(max(originX, minX), max(minX, maxX))

        // Vertical positioning (flipped coordinate system: Y increases downwards):
        // 1. Try placing immediately below selection
        var originY = selectionRect.maxY + 8

        // 2. If it exceeds bottom screen boundary, place above selection
        if originY + toolbarSize.height > bounds.maxY - 8 {
            originY = selectionRect.minY - toolbarSize.height - 8
        }

        // 3. If it also exceeds top screen boundary, place inside selection at bottom
        if originY < bounds.minY + 8 {
            originY = min(selectionRect.maxY, bounds.maxY) - toolbarSize.height - 12
        }

        // Final safety clamp so toolbar is never clipped outside bounds
        let minY = bounds.minY + 8
        let maxY = bounds.maxY - toolbarSize.height - 8
        originY = min(max(originY, minY), max(minY, maxY))

        return CGRect(origin: CGPoint(x: originX, y: originY), size: toolbarSize)
    }
}

// MARK: - SwiftUI Content View

private struct ToolbarContentView: View {
    @Bindable var state: SelectionToolbarState
    let onAction: (SelectionInPlaceToolbar.Action) -> Void
    let onToolSelected: (SelectionInPlaceToolbar.AnnotationTool?) -> Void
    let onColorSelected: (NSColor) -> Void
    let onLineWidthSelected: (CGFloat) -> Void
    let onMosaicIntensitySelected: (Int) -> Void
    let onUndo: () -> Void
    let onRedo: () -> Void

    private var quickColors: [(color: Color, name: String)] {
        [
            (.red, String(localized: "color.red")),
            (.yellow, String(localized: "color.yellow")),
            (.green, String(localized: "color.green")),
            (.blue, String(localized: "color.blue")),
            (.black, String(localized: "color.black")),
            (.white, String(localized: "color.white"))
        ]
    }

    var body: some View {
        VStack(spacing: 4) {
            HStack(spacing: 5) {
                // Annotation Tools
                Group {
                    toolButton(.rectangle, icon: "rectangle")
                    toolButton(.ellipse, icon: "circle")
                    toolButton(.line, icon: "line.diagonal")
                    toolButton(.arrow, icon: "arrow.up.right")
                    toolButton(.pen, icon: "pencil.tip")
                    toolButton(.text, icon: "textbox")
                    toolButton(.numberLabel, icon: "1.circle.fill")
                    toolButton(.highlight, icon: "highlighter")
                    toolButton(.mosaic, icon: "checkerboard.rectangle")
                    toolButton(.privacyRedaction, icon: "eye.slash.fill")
                }

                Divider().frame(height: 18)

                // Actions
                Group {
                    Button {
                        onAction(.beautify)
                    } label: {
                        Image(systemName: "sparkles")
                            .font(.system(size: 12))
                            .foregroundColor(state.isBeautifyEnabled ? .accentColor : .primary)
                            .padding(5)
                            .background(state.isBeautifyEnabled ? Color.accentColor.opacity(0.2) : Color.clear)
                            .cornerRadius(4)
                            .padding(4)
                            .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .customTooltip(String(localized: "preview.tooltip.beautify"))

                    actionButton("SM", icon: "camera.metering.unknown", help: String(localized: "preview.tooltip.smartMosaic")) {
                        onAction(.smartMosaic)
                    }

                    actionButton("P", icon: "pin.fill", help: String(localized: "preview.tooltip.pin")) {
                        onAction(.pin)
                    }
                    actionButton("OCR", icon: "text.viewfinder", help: String(localized: "preview.tooltip.ocr")) {
                        onAction(.ocr)
                    }
                    actionButton("Translate", icon: "character.book.closed.fill", help: "Translate (⌘T)") {
                        onAction(.translate)
                    }
                    actionButton("Save", icon: "square.and.arrow.down", help: String(localized: "preview.tooltip.save")) {
                        onAction(.save)
                    }
                    if ImageUploadService.shared.isConfigured {
                        actionButton("Upload", icon: "icloud.and.arrow.up", help: "Upload to Cloud (\(ImageUploadService.shared.settings.activeHost.displayName))") {
                            onAction(.upload)
                        }
                    }
                }

                Divider().frame(height: 18)

                // Cancel / Confirm
                Button {
                    onAction(.cancel)
                } label: {
                    Image(systemName: "xmark")
                        .foregroundColor(.secondary)
                        .padding(5)
                        .padding(4)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .customTooltip("Cancel (Esc)")

                Button {
                    onAction(.copy)
                } label: {
                    HStack(spacing: 3) {
                        Image(systemName: "checkmark")
                        Text("Copy")
                            .font(.system(size: 11, weight: .semibold))
                    }
                    .foregroundColor(.white)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 4)
                    .background(Color.accentColor, in: RoundedRectangle(cornerRadius: 5))
                    .padding(3)
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .customTooltip(String(localized: "preview.tooltip.confirm"))
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 6)
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 8))

            // Secondary Style Bar for annotations
            if state.showStyleBar {
                HStack(spacing: 8) {
                    // Quick Colors
                    ForEach(quickColors, id: \.name) { item in
                        Circle()
                            .fill(item.color)
                            .frame(width: 14, height: 14)
                            .overlay(
                                Circle()
                                    .stroke(Color.primary.opacity(0.4), lineWidth: state.activeColor == item.color ? 2 : 0.5)
                            )
                            .contentShape(Circle())
                            .onTapGesture {
                                state.activeColor = item.color
                                onColorSelected(NSColor(item.color))
                            }
                            .customTooltip(item.name, placement: .bottom)
                    }

                    // Screen eyedropper
                    Button {
                        NSColorSampler().show { picked in
                            guard let picked else { return }
                            state.activeColor = Color(nsColor: picked)
                            onColorSelected(picked)
                        }
                    } label: {
                        Image(systemName: "eyedropper")
                            .font(.system(size: 11))
                            .padding(3)
                    }
                    .buttonStyle(.plain)
                    .contentShape(Rectangle().inset(by: -2))
                    .customTooltip(String(localized: "preview.color.eyedropper"), placement: .bottom)

                    // System color picker
                    ColorPicker(
                        "",
                        selection: Binding(
                            get: { state.activeColor },
                            set: { newColor in
                                state.activeColor = newColor
                                onColorSelected(NSColor(newColor))
                            }
                        ),
                        supportsOpacity: false
                    )
                    .labelsHidden()
                    .frame(width: 18)
                    .contentShape(Rectangle().inset(by: -2))
                    .customTooltip(String(localized: "preview.color.custom"), placement: .bottom)

                    if state.activeTool == .mosaic {
                        Divider().frame(height: 12)

                        // Mosaic intensity presets (block size)
                        ForEach([(String(localized: "mosaic.intensity.light"), 4), (String(localized: "mosaic.intensity.medium"), 10), (String(localized: "mosaic.intensity.strong"), 20)], id: \.1) { item in
                            Text(item.0)
                                .font(.system(size: 10))
                                .padding(.horizontal, 6)
                                .padding(.vertical, 2)
                                .background(state.mosaicBlockSize == item.1 ? Color.accentColor.opacity(0.3) : Color.clear)
                                .cornerRadius(4)
                                .contentShape(Rectangle())
                                .onTapGesture {
                                    state.mosaicBlockSize = item.1
                                    onMosaicIntensitySelected(item.1)
                                }
                                .customTooltip(item.0, placement: .bottom)
                        }
                    }

                    Divider().frame(height: 12)

                    // Line widths
                    ForEach([2.0, 4.0, 6.0], id: \.self) { width in
                        Circle()
                            .fill(Color.primary)
                            .frame(width: CGFloat(width * 2), height: CGFloat(width * 2))
                            .padding(3)
                            .background(
                                state.activeLineWidth == width ? Color.accentColor.opacity(0.3) : Color.clear,
                                in: RoundedRectangle(cornerRadius: 4)
                            )
                            .contentShape(Rectangle())
                            .onTapGesture {
                                state.activeLineWidth = width
                                onLineWidthSelected(width)
                            }
                            .customTooltip("\(String(localized: "settings.stroke.width")): \(Int(width))pt", placement: .bottom)
                    }

                    Spacer()

                    Button {
                        onUndo()
                    } label: {
                        Image(systemName: "arrow.uturn.backward")
                            .font(.caption)
                            .padding(3)
                    }
                    .buttonStyle(.plain)
                    .contentShape(Rectangle().inset(by: -2))
                    .disabled(!state.canUndo)
                    .customTooltip(String(localized: "preview.tooltip.undo"), placement: .bottom)

                    Button {
                        onRedo()
                    } label: {
                        Image(systemName: "arrow.uturn.forward")
                            .font(.caption)
                            .padding(3)
                    }
                    .buttonStyle(.plain)
                    .contentShape(Rectangle().inset(by: -2))
                    .disabled(!state.canRedo)
                    .customTooltip(String(localized: "preview.tooltip.redo"), placement: .bottom)
                }
                .padding(.horizontal, 10)
                .padding(.vertical, 4)
                .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 6))
            }
        }
        .contentShape(Rectangle())
        .cursor(.arrow)
    }

    private func toolButton(_ tool: SelectionInPlaceToolbar.AnnotationTool, icon: String) -> some View {
        Button {
            if state.activeTool == tool {
                state.activeTool = nil
                state.showStyleBar = false
                onToolSelected(nil)
            } else {
                state.activeTool = tool
                state.showStyleBar = true
                onToolSelected(tool)
            }
        } label: {
            // Padding lives inside the label so the whole (larger) area
            // triggers the button — edge clicks included.
            Image(systemName: icon)
                .font(.system(size: 12))
                .foregroundColor(state.activeTool == tool ? .accentColor : .primary)
                .padding(5)
                .background(
                    state.activeTool == tool ? Color.accentColor.opacity(0.2) : Color.clear,
                    in: RoundedRectangle(cornerRadius: 4)
                )
                .padding(4)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .customTooltip(tool.tooltip)
        .accessibilityLabel(tool.tooltip)
    }

    private func actionButton(_ title: String, icon: String, help: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            // Padding lives inside the label so the whole (larger) area
            // triggers the button — edge clicks included.
            Image(systemName: icon)
                .font(.system(size: 12))
                .padding(5)
                .padding(4)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .customTooltip(help)
        .accessibilityLabel(help)
    }
}
