import AppKit
import SwiftUI

// MARK: - Toolbar Action Protocol

@MainActor
public protocol SelectionToolbarDelegate: AnyObject {
    func toolbarDidSelectAction(_ action: SelectionInPlaceToolbar.Action)
    func toolbarDidChangeTool(_ tool: SelectionInPlaceToolbar.AnnotationTool?)
    func toolbarDidChangeColor(_ color: NSColor)
    func toolbarDidChangeLineWidth(_ width: CGFloat)
    func toolbarDidRequestUndo()
    func toolbarDidRequestRedo()
}

// MARK: - SelectionInPlaceToolbar

/// In-place floating toolbar displayed immediately adjacent to the selected region on frozen overlay.
public final class SelectionInPlaceToolbar: NSView {
    public enum Action: Sendable {
        case copy
        case pin
        case ocr
        case translate
        case openEditor
        case save
        case cancel
    }

    public enum AnnotationTool: String, CaseIterable, Sendable {
        case rectangle
        case ellipse
        case arrow
        case pen
        case text
        case mosaic
        case privacyRedaction
    }

    public weak var delegate: SelectionToolbarDelegate?

    public private(set) var activeTool: AnnotationTool?
    public private(set) var selectedColor: NSColor = .systemRed
    public private(set) var selectedLineWidth: CGFloat = 3.0

    private var hostingView: NSHostingView<ToolbarContentView>?

    override public var isFlipped: Bool { true }

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
        layer?.cornerRadius = 8
        layer?.masksToBounds = false
        layer?.shadowColor = NSColor.black.cgColor
        layer?.shadowOpacity = 0.35
        layer?.shadowRadius = 8
        layer?.shadowOffset = CGSize(width: 0, height: -2)

        let content = ToolbarContentView(
            onAction: { [weak self] action in
                self?.delegate?.toolbarDidSelectAction(action)
            },
            onToolSelected: { [weak self] tool in
                self?.activeTool = tool
                self?.delegate?.toolbarDidChangeTool(tool)
            },
            onColorSelected: { [weak self] color in
                self?.selectedColor = color
                self?.delegate?.toolbarDidChangeColor(color)
            },
            onLineWidthSelected: { [weak self] width in
                self?.selectedLineWidth = width
                self?.delegate?.toolbarDidChangeLineWidth(width)
            },
            onUndo: { [weak self] in
                self?.delegate?.toolbarDidRequestUndo()
            },
            onRedo: { [weak self] in
                self?.delegate?.toolbarDidRequestRedo()
            }
        )

        let hosting = NSHostingView(rootView: content)
        hosting.autoresizingMask = [.width, .height]
        hosting.frame = bounds
        addSubview(hosting)
        self.hostingView = hosting
    }

    /// Calculates ideal toolbar frame relative to selection rectangle in flipped local view coordinates (Y-down).
    public static func preferredFrame(
        for selectionRect: CGRect,
        in bounds: CGRect,
        toolbarSize: CGSize = CGSize(width: 480, height: 42)
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
    let onAction: (SelectionInPlaceToolbar.Action) -> Void
    let onToolSelected: (SelectionInPlaceToolbar.AnnotationTool?) -> Void
    let onColorSelected: (NSColor) -> Void
    let onLineWidthSelected: (CGFloat) -> Void
    let onUndo: () -> Void
    let onRedo: () -> Void

    @State private var activeTool: SelectionInPlaceToolbar.AnnotationTool?
    @State private var activeColor: Color = .red
    @State private var activeLineWidth: CGFloat = 3.0
    @State private var showStyleBar: Bool = false

    var body: some View {
        VStack(spacing: 4) {
            HStack(spacing: 5) {
                // Annotation Tools
                Group {
                    toolButton(.rectangle, icon: "rectangle")
                    toolButton(.ellipse, icon: "circle")
                    toolButton(.arrow, icon: "arrow.up.right")
                    toolButton(.pen, icon: "pencil.tip")
                    toolButton(.text, icon: "textformat")
                    toolButton(.mosaic, icon: "checkerboard.rectangle")
                    toolButton(.privacyRedaction, icon: "eye.slash.fill")
                }

                Divider().frame(height: 18)

                // Actions
                Group {
                    actionButton("P", icon: "pin.fill", help: "Pin (P)") {
                        onAction(.pin)
                    }
                    actionButton("OCR", icon: "text.viewfinder", help: "OCR Recognition") {
                        onAction(.ocr)
                    }
                    actionButton("T", icon: "character.book.closed.fill", help: "Translate (T)") {
                        onAction(.translate)
                    }
                    actionButton("Edit", icon: "slider.horizontal.3", help: "Open in Full Editor") {
                        onAction(.openEditor)
                    }
                    actionButton("Save", icon: "square.and.arrow.down", help: "Save (⌘S)") {
                        onAction(.save)
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
                }
                .buttonStyle(.plain)
                .help("Cancel (Esc)")

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
                    .background(Color.accentColor)
                    .cornerRadius(5)
                }
                .buttonStyle(.plain)
                .help("Copy to Clipboard (Enter)")
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 6)
            .background(.regularMaterial)
            .cornerRadius(8)

            // Secondary Style Bar for annotations
            if showStyleBar {
                HStack(spacing: 8) {
                    // Quick Colors
                    ForEach([Color.red, Color.yellow, Color.green, Color.blue, Color.black, Color.white], id: \.self) { color in
                        Circle()
                            .fill(color)
                            .frame(width: 14, height: 14)
                            .overlay(
                                Circle()
                                    .stroke(Color.primary.opacity(0.4), lineWidth: activeColor == color ? 2 : 0.5)
                            )
                            .onTapGesture {
                                activeColor = color
                                onColorSelected(NSColor(color))
                            }
                    }

                    Divider().frame(height: 12)

                    // Line widths
                    ForEach([2.0, 4.0, 6.0], id: \.self) { width in
                        Circle()
                            .fill(Color.primary)
                            .frame(width: CGFloat(width * 2), height: CGFloat(width * 2))
                            .padding(2)
                            .background(activeLineWidth == width ? Color.accentColor.opacity(0.3) : Color.clear)
                            .cornerRadius(4)
                            .onTapGesture {
                                activeLineWidth = width
                                onLineWidthSelected(width)
                            }
                    }

                    Spacer()

                    Button {
                        onUndo()
                    } label: {
                        Image(systemName: "arrow.uturn.backward")
                            .font(.caption)
                    }
                    .buttonStyle(.plain)

                    Button {
                        onRedo()
                    } label: {
                        Image(systemName: "arrow.uturn.forward")
                            .font(.caption)
                    }
                    .buttonStyle(.plain)
                }
                .padding(.horizontal, 10)
                .padding(.vertical, 4)
                .background(.ultraThinMaterial)
                .cornerRadius(6)
            }
        }
    }

    private func toolButton(_ tool: SelectionInPlaceToolbar.AnnotationTool, icon: String) -> some View {
        Button {
            if activeTool == tool {
                activeTool = nil
                showStyleBar = false
                onToolSelected(nil)
            } else {
                activeTool = tool
                showStyleBar = true
                onToolSelected(tool)
            }
        } label: {
            Image(systemName: icon)
                .font(.system(size: 12))
                .foregroundColor(activeTool == tool ? .accentColor : .primary)
                .padding(5)
                .background(activeTool == tool ? Color.accentColor.opacity(0.2) : Color.clear)
                .cornerRadius(4)
        }
        .buttonStyle(.plain)
    }

    private func actionButton(_ title: String, icon: String, help: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: icon)
                .font(.system(size: 12))
                .padding(5)
        }
        .buttonStyle(.plain)
        .help(help)
    }
}
