import AppKit
import CoreGraphics
import CoreImage
import CoreText
import SwiftUI
import os

// MARK: - SelectionOverlayDelegate

@MainActor
protocol SelectionOverlayDelegate: AnyObject {
    /// Called when user confirms a selection and chooses an action from the in-place toolbar or hotkey.
    func selectionOverlay(
        didConfirmSelection selectionQuartz: QuartzGlobalRect,
        action: SelectionInPlaceToolbar.Action,
        annotations: [Annotation],
        on snapshot: DisplaySnapshot
    )

    /// Called when user cancels selection.
    func selectionOverlayDidCancel()

    /// Backward compatibility hook
    func selectionOverlay(didSelectRect rect: CGRect, on display: DisplayInfo)
}

extension SelectionOverlayDelegate {
    func selectionOverlay(didSelectRect rect: CGRect, on display: DisplayInfo) {}
}

// MARK: - SnapshotBackgroundView

/// Hardware-accelerated background view that renders the static screen image using CALayer texture.
/// Never redraws in software, eliminating CPU image copy overhead during interactions.
final class SnapshotBackgroundView: NSView {
    override var isFlipped: Bool { true }

    init(frame: NSRect, image: CGImage) {
        super.init(frame: frame)
        wantsLayer = true
        layer?.contents = image
        layer?.contentsGravity = .resize
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }
}

// MARK: - SelectionOverlayWindow

/// Fullscreen overlay window for an individual display displaying a frozen snapshot.
final class SelectionOverlayWindow: NSPanel {
    let targetScreen: NSScreen
    let displaySnapshot: DisplaySnapshot
    let frozenSnapshotSet: FrozenSnapshotSet

    private(set) var overlayView: SelectionOverlayView?

    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { true }

    @MainActor
    init(
        screen: NSScreen,
        snapshot: DisplaySnapshot,
        snapshotSet: FrozenSnapshotSet
    ) {
        self.targetScreen = screen
        self.displaySnapshot = snapshot
        self.frozenSnapshotSet = snapshotSet

        super.init(
            contentRect: screen.frame,
            styleMask: [.borderless],
            backing: .buffered,
            defer: false
        )

        configureWindow()
        setupOverlayView()
    }

    @MainActor
    private func configureWindow() {
        level = NSWindow.Level(NSWindow.Level.statusBar.rawValue + 1)
        sharingType = .readOnly
        isOpaque = true
        backgroundColor = .black
        ignoresMouseEvents = false
        hasShadow = false
        hidesOnDeactivate = false
        collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .ignoresCycle]
        isMovable = false
        acceptsMouseMovedEvents = true
    }

    @MainActor
    private func setupOverlayView() {
        let container = NSView(frame: NSRect(origin: .zero, size: targetScreen.frame.size))
        container.autoresizingMask = [.width, .height]

        let bgView = SnapshotBackgroundView(
            frame: NSRect(origin: .zero, size: targetScreen.frame.size),
            image: displaySnapshot.image
        )
        bgView.autoresizingMask = [.width, .height]
        container.addSubview(bgView)

        let view = SelectionOverlayView(
            frame: NSRect(origin: .zero, size: targetScreen.frame.size),
            displaySnapshot: displaySnapshot,
            snapshotSet: frozenSnapshotSet
        )
        view.autoresizingMask = [.width, .height]
        container.addSubview(view)

        self.contentView = container
        self.overlayView = view
    }

    @MainActor
    func setDelegate(_ delegate: SelectionOverlayDelegate) {
        overlayView?.delegate = delegate
    }

    /// Action to run automatically when the user commits a fresh region
    /// selection (e.g. .translate for the screenshot-translation hotkey).
    @MainActor
    func setAutomaticActionAfterSelection(_ action: SelectionInPlaceToolbar.Action?) {
        overlayView?.automaticActionAfterSelection = action
    }

    @MainActor
    func showOverlay(isKey: Bool = true) {
        if isKey {
            makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            makeFirstResponder(overlayView)
        } else {
            orderFront(nil)
        }
        overlayView?.initializeCursor()
    }
}

// MARK: - Resize Handle

enum ResizeHandle: CaseIterable {
    case topLeft, topCenter, topRight
    case middleLeft, middleRight
    case bottomLeft, bottomCenter, bottomRight

    func cursor() -> NSCursor {
        switch self {
        case .topLeft, .bottomRight:
            return NSCursor.diagonalResizeCursor(northWestSouthEast: true)
        case .topRight, .bottomLeft:
            return NSCursor.diagonalResizeCursor(northWestSouthEast: false)
        case .topCenter, .bottomCenter:
            return NSCursor.resizeUpDown
        case .middleLeft, .middleRight:
            return NSCursor.resizeLeftRight
        }
    }
}

// MARK: - SelectionOverlayView

final class SelectionOverlayView: NSView, SelectionToolbarDelegate, NSTextFieldDelegate {
    private static let sharedCIContext = CIContext()

    enum InteractionState {
        case hovering
        case creatingSelection(start: CGPoint, current: CGPoint)
        case selected(rect: CGRect)
        case resizing(handle: ResizeHandle, anchorRect: CGRect, startPoint: CGPoint, currentRect: CGRect)
        case moving(anchorRect: CGRect, startPoint: CGPoint, currentRect: CGRect)
        case movingAnnotation(index: Int, selectionRect: CGRect, startPoint: CGPoint, originalAnno: Annotation, originalAnnotations: [Annotation])
        case resizingAnnotation(index: Int, selectionRect: CGRect, handle: AnnotationHandle, originalAnnotations: [Annotation])
        case drawingAnnotation(tool: SelectionInPlaceToolbar.AnnotationTool, selectionRect: CGRect, start: CGPoint, current: CGPoint)
    }

    weak var delegate: SelectionOverlayDelegate?

    let displaySnapshot: DisplaySnapshot
    let frozenSnapshotSet: FrozenSnapshotSet

    private var state: InteractionState = .hovering
    private var mousePosition: CGPoint?
    private var candidateWindowsUnderMouse: [WindowInfo] = []
    private var isFreeSelectionLocked: Bool = false

    // Cached image pixel data to avoid repeated memory copies
    private let pixelData: CFData?
    private let pixelBytes: UnsafePointer<UInt8>?

    // Annotations in progress
    private var committedAnnotations: [Annotation] = []
    private var selectedAnnotationIndex: Int?
    private var activeAnnotationTool: SelectionInPlaceToolbar.AnnotationTool?
    private var annotationColor: NSColor = .systemRed
    private var annotationLineWidth: CGFloat = 3.0
    private var mosaicBlockSize: Int = 6
    private var currentDraftAnnotation: Annotation?
    private var freehandPoints: [CGPoint] = []

    // In-place text editing & beautify state
    private var activeTextField: NSTextField?
    private var textInputPosition: CGPoint?
    private var editingAnnotationIndex: Int?
    private(set) var isBeautifyEnabled: Bool = false

    var isEditingText: Bool { activeTextField != nil }

    private var toolbar: SelectionInPlaceToolbar?
    private let editHistory = EditHistory()
    private var statusLabel: NSTextField?
    private var statusHideWorkItem: DispatchWorkItem?

    // In-place results drawer (OCR / translation)
    let resultsModel = InPlaceResultsModel()
    private var resultsPanelHost: NSHostingView<InPlaceResultsPanel>?
    /// In-flight drawer analysis; cancelled on rerun and when the drawer closes
    /// so a dismissed overlay keeps no running network work writing into it.
    private var inPlaceAnalysisTask: Task<Void, Never>?
    private var inPlaceAnalysisGeneration: UInt64 = 0

    /// Selection snapshot captured when a click starts outside the current
    /// selection; tiny clicks (no drag) must restore it instead of re-picking.
    private var selectionBeforeOutsideClick: CGRect?

    /// Action auto-run once after a fresh region selection is committed.
    /// Distinguishes screenshot-translation mode from plain region capture.
    var automaticActionAfterSelection: SelectionInPlaceToolbar.Action?

    private let handleSize: CGFloat = 8.0

    init(
        frame: NSRect,
        displaySnapshot: DisplaySnapshot,
        snapshotSet: FrozenSnapshotSet
    ) {
        self.displaySnapshot = displaySnapshot
        self.frozenSnapshotSet = snapshotSet
        if let data = displaySnapshot.image.dataProvider?.data {
            self.pixelData = data
            self.pixelBytes = CFDataGetBytePtr(data)
        } else {
            self.pixelData = nil
            self.pixelBytes = nil
        }
        super.init(frame: frame)
        wantsLayer = true
        layerContentsRedrawPolicy = .onSetNeedsDisplay
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override var isFlipped: Bool { true }
    override var isOpaque: Bool { false }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }
    override var acceptsFirstResponder: Bool { true }
    override func becomeFirstResponder() -> Bool { true }

    // MARK: - Cursor Initialization

    func initializeCursor() {
        let mouseLoc = NSEvent.mouseLocation
        let windowPoint = window?.convertPoint(fromScreen: mouseLoc) ?? .zero
        let localPoint = convert(windowPoint, from: nil)
        if bounds.contains(localPoint) {
            updateHoverState(at: localPoint)
        }
    }

    // MARK: - Drawing

    override func draw(_ dirtyRect: NSRect) {
        guard let context = NSGraphicsContext.current?.cgContext else { return }

        // 1. Determine active selection or hovered window rect
        let currentSelection: CGRect? = {
            switch state {
            case .creatingSelection(let start, let current):
                return normalizedRect(from: start, to: current)
            case .selected(let rect):
                return rect
            case .resizing(_, _, _, let currentRect):
                return currentRect
            case .moving(_, _, let currentRect):
                return currentRect
            case .movingAnnotation(_, let selRect, _, _, _):
                return selRect
            case .resizingAnnotation(_, let selRect, _, _):
                return selRect
            case .drawingAnnotation(_, let selRect, _, _):
                return selRect
            case .hovering:
                if !isFreeSelectionLocked, let win = candidateWindowsUnderMouse.first {
                    let local = CoordinateMapper.quartzToDisplayLocal(
                        QuartzGlobalRect(win.frame),
                        displayQuartzBounds: displaySnapshot.quartzBounds
                    )
                    return local.rawValue.intersection(bounds)
                }
                return nil
            }
        }()

        // 2. Dimming overlay excluding selection
        context.saveGState()
        context.setFillColor(NSColor.black.withAlphaComponent(0.35).cgColor)
        if let sel = currentSelection, !sel.isEmpty {
            let path = CGMutablePath()
            path.addRect(bounds)
            path.addRect(sel)
            context.addPath(path)
            context.fillPath(using: .evenOdd)
        } else {
            context.fill(bounds)
        }
        context.restoreGState()

        // 3. Draw selection border, beautify preview, resize handles, and dimension tag
        if let sel = currentSelection, !sel.isEmpty {
            if isBeautifyEnabled {
                drawBeautifyPreview(for: sel, context: context)
            }
            if case .selected = state {
                drawBilingualOverlay(for: sel)
            }
            drawSelectionBox(sel, context: context)
            switch state {
            case .selected:
                drawResizeHandles(for: sel, context: context)
                drawDimensionTag(for: sel, context: context)
            case .creatingSelection, .resizing, .moving:
                drawDimensionTag(for: sel, context: context)
            default:
                break
            }
        }

        // 4. Draw committed and draft annotations
        for (idx, annotation) in committedAnnotations.enumerated() {
            if idx == editingAnnotationIndex {
                continue
            }
            drawAnnotation(annotation, context: context)
        }
        if let draft = currentDraftAnnotation {
            drawAnnotation(draft, context: context)
        }

        // 4.1 Draw selection outline and interactive handles around selected annotation
        if let selIdx = selectedAnnotationIndex, selIdx < committedAnnotations.count {
            let selectedAnno = committedAnnotations[selIdx]
            let annoBounds = selectedAnno.bounds.insetBy(dx: -4, dy: -4)
            context.saveGState()
            context.setStrokeColor(NSColor.controlAccentColor.cgColor)
            context.setLineWidth(1.5)
            context.setLineDash(phase: 0, lengths: [4, 4])
            context.stroke(annoBounds)
            context.setLineDash(phase: 0, lengths: [])

            for item in selectedAnno.controlHandles(padding: 4) {
                let isSpecial = item.handle.isSpecialPoint
                let hSize: CGFloat = 8.0
                let hRect = CGRect(
                    x: item.point.x - hSize / 2,
                    y: item.point.y - hSize / 2,
                    width: hSize,
                    height: hSize
                )
                context.setFillColor((isSpecial ? NSColor.systemYellow : NSColor.white).cgColor)
                context.fillEllipse(in: hRect)
                context.setStrokeColor(NSColor.controlAccentColor.cgColor)
                context.setLineWidth(1.5)
                context.strokeEllipse(in: hRect)
            }
            context.restoreGState()
        }

        // 5. Draw pixel loupe when hovering
        if case .hovering = state, let mousePos = mousePosition {
            drawLoupe(at: mousePos, context: context)
        }
    }

    // MARK: - Selection Rendering Helpers

    private func drawBeautifyPreview(for rect: CGRect, context: CGContext) {
        let pad: CGFloat = 18.0
        let outerRect = rect.insetBy(dx: -pad, dy: -pad)
        context.saveGState()
        let framePath = CGMutablePath()
        framePath.addRoundedRect(in: outerRect, cornerWidth: 14, cornerHeight: 14)
        framePath.addRoundedRect(in: rect, cornerWidth: 8, cornerHeight: 8)
        context.addPath(framePath)
        context.clip(using: .evenOdd)

        let colors = BeautifyConfig.BackgroundStyle.Preset.ocean.colors.map { $0.cgColor } as CFArray
        let colorSpace = context.colorSpace ?? CGColorSpace(name: CGColorSpace.sRGB)!
        if let gradient = CGGradient(colorsSpace: colorSpace, colors: colors, locations: [0.0, 1.0]) {
            context.drawLinearGradient(
                gradient,
                start: CGPoint(x: outerRect.minX, y: outerRect.minY),
                end: CGPoint(x: outerRect.maxX, y: outerRect.maxY),
                options: []
            )
        }
        context.restoreGState()
    }

    /// Draws each translated segment as a strip just below its original
    /// text inside the selection — in-image bilingual view; the original
    /// screenshot stays fully visible. Only while the translate tab is active.
    private func drawBilingualOverlay(for selRect: CGRect) {
        guard resultsModel.isVisible,
              resultsModel.activeTab == .translate,
              !resultsModel.segments.isEmpty else { return }

        NSGraphicsContext.saveGraphicsState()
        defer { NSGraphicsContext.restoreGraphicsState() }
        NSBezierPath(rect: selRect).addClip()
        for segment in resultsModel.segments {
            guard let strip = BilingualOverlayLayout.translationStrip(for: segment, in: selRect.size) else { continue }
            let attr = NSAttributedString(string: segment.translated, attributes: [
                .font: NSFont.systemFont(ofSize: strip.fontSize, weight: .medium),
                .foregroundColor: NSColor.white
            ])
            let frame = strip.frame.offsetBy(dx: selRect.minX, dy: selRect.minY)
            NSColor.black.withAlphaComponent(0.78).setFill()
            NSBezierPath(roundedRect: frame, xRadius: 3, yRadius: 3).fill()
            attr.draw(
                with: strip.textFrame.offsetBy(dx: selRect.minX, dy: selRect.minY),
                options: [.usesLineFragmentOrigin, .usesFontLeading]
            )
        }
    }

    private func drawSelectionBox(_ rect: CGRect, context: CGContext) {
        context.saveGState()
        context.setStrokeColor(NSColor.controlAccentColor.cgColor)
        context.setLineWidth(2.0)
        context.stroke(rect)
        context.restoreGState()
    }

    private func drawResizeHandles(for rect: CGRect, context: CGContext) {
        context.saveGState()
        context.setFillColor(NSColor.white.cgColor)
        context.setStrokeColor(NSColor.black.withAlphaComponent(0.6).cgColor)
        context.setLineWidth(1.0)

        for handle in ResizeHandle.allCases {
            let p = point(for: handle, in: rect)
            let hRect = CGRect(x: p.x - handleSize / 2, y: p.y - handleSize / 2, width: handleSize, height: handleSize)
            context.fill(hRect)
            context.stroke(hRect)
        }
        context.restoreGState()
    }

    private func measureText(_ string: String, font: NSFont) -> CGSize {
        let ctFont = font as CTFont
        let attributes: [CFString: Any] = [
            kCTFontAttributeName: ctFont
        ]
        guard let attrString = CFAttributedStringCreate(kCFAllocatorDefault, string as CFString, attributes as CFDictionary) else {
            return .zero
        }
        let line = CTLineCreateWithAttributedString(attrString)
        var ascent: CGFloat = 0
        var descent: CGFloat = 0
        var leading: CGFloat = 0
        let width = CTLineGetTypographicBounds(line, &ascent, &descent, &leading)
        return CGSize(width: ceil(width), height: ceil(ascent + descent))
    }

    private func drawText(
        _ string: String,
        at point: CGPoint,
        font: NSFont,
        color: NSColor,
        in context: CGContext
    ) {
        let ctFont = font as CTFont
        let attributes: [CFString: Any] = [
            kCTFontAttributeName: ctFont,
            kCTForegroundColorAttributeName: color.cgColor
        ]
        guard let attrString = CFAttributedStringCreate(kCFAllocatorDefault, string as CFString, attributes as CFDictionary) else {
            return
        }
        let line = CTLineCreateWithAttributedString(attrString)
        var ascent: CGFloat = 0
        var descent: CGFloat = 0
        var leading: CGFloat = 0
        _ = CTLineGetTypographicBounds(line, &ascent, &descent, &leading)

        context.saveGState()
        context.translateBy(x: point.x, y: point.y + ascent)
        context.scaleBy(x: 1.0, y: -1.0)
        context.textPosition = .zero
        CTLineDraw(line, context)
        context.restoreGState()
    }

    private func drawDimensionTag(for rect: CGRect, context: CGContext) {
        let scale = displaySnapshot.scaleFactor
        let pixelW = Int(round(rect.width * scale))
        let pixelH = Int(round(rect.height * scale))
        let text = "\(pixelW) × \(pixelH)"

        let font = NSFont.monospacedDigitSystemFont(ofSize: 11, weight: .semibold)
        let textSize = measureText(text, font: font)
        let tagRect = CGRect(x: rect.minX, y: max(4, rect.minY - textSize.height - 8), width: textSize.width + 12, height: textSize.height + 6)

        context.saveGState()
        context.setFillColor(NSColor.black.withAlphaComponent(0.75).cgColor)
        let path = CGPath(roundedRect: tagRect, cornerWidth: 4, cornerHeight: 4, transform: nil)
        context.addPath(path)
        context.fillPath()
        context.restoreGState()

        drawText(text, at: CGPoint(x: tagRect.minX + 6, y: tagRect.minY + 3), font: font, color: .white, in: context)
    }

    // MARK: - Pixel Loupe (Magnifier)

    private func drawLoupe(at point: CGPoint, context: CGContext) {
        guard let ptr = pixelBytes else { return }

        let loupeSize: CGFloat = 110.0
        let gridCount = 11
        let pixelSize = loupeSize / CGFloat(gridCount)

        var loupeOrigin = CGPoint(x: point.x + 20, y: point.y + 20)
        if loupeOrigin.x + loupeSize > bounds.maxX - 10 {
            loupeOrigin.x = point.x - loupeSize - 20
        }
        if loupeOrigin.y + loupeSize + 30 > bounds.maxY - 10 {
            loupeOrigin.y = point.y - loupeSize - 30
        }

        let loupeRect = CGRect(origin: loupeOrigin, size: CGSize(width: loupeSize, height: loupeSize))

        context.saveGState()
        let clipPath = CGPath(ellipseIn: loupeRect, transform: nil)
        context.addPath(clipPath)
        context.clip()

        let scale = displaySnapshot.scaleFactor
        let centerImageX = Int(point.x * scale)
        let centerImageY = Int(point.y * scale)

        var centerRGB: (r: UInt8, g: UInt8, b: UInt8) = (0, 0, 0)
        let bpr = displaySnapshot.image.bytesPerRow
        let bpp = displaySnapshot.image.bitsPerPixel / 8

        let half = gridCount / 2
        for gy in 0..<gridCount {
            for gx in 0..<gridCount {
                let ix = centerImageX + (gx - half)
                let iy = centerImageY + (gy - half)

                var color = NSColor(srgbRed: 0, green: 0, blue: 0, alpha: 1.0)
                if ix >= 0 && ix < displaySnapshot.image.width && iy >= 0 && iy < displaySnapshot.image.height {
                    let offset = iy * bpr + ix * bpp
                    let rByte = ptr[offset]
                    let gByte = ptr[offset + 1]
                    let bByte = ptr[offset + 2]
                    color = NSColor(srgbRed: CGFloat(rByte) / 255.0, green: CGFloat(gByte) / 255.0, blue: CGFloat(bByte) / 255.0, alpha: 1.0)
                    if gx == half && gy == half {
                        centerRGB = (rByte, gByte, bByte)
                    }
                }

                let cellRect = CGRect(
                    x: loupeRect.minX + CGFloat(gx) * pixelSize,
                    y: loupeRect.minY + CGFloat(gy) * pixelSize,
                    width: pixelSize,
                    height: pixelSize
                )
                context.setFillColor(color.cgColor)
                context.fill(cellRect)
            }
        }

        context.setStrokeColor(NSColor.red.cgColor)
        context.setLineWidth(1.0)
        let centerCell = CGRect(
            x: loupeRect.minX + CGFloat(gridCount / 2) * pixelSize,
            y: loupeRect.minY + CGFloat(gridCount / 2) * pixelSize,
            width: pixelSize,
            height: pixelSize
        )
        context.stroke(centerCell)
        context.restoreGState()

        context.saveGState()
        context.setStrokeColor(NSColor.white.cgColor)
        context.setLineWidth(2.5)
        context.strokeEllipse(in: loupeRect)

        let hexString = String(
            format: "#%02X%02X%02X",
            centerRGB.r,
            centerRGB.g,
            centerRGB.b
        )
        let font = NSFont.monospacedDigitSystemFont(ofSize: 10, weight: .bold)
        let tagSize = measureText(hexString, font: font)
        let tagRect = CGRect(x: loupeRect.midX - tagSize.width / 2 - 6, y: loupeRect.maxY + 4, width: tagSize.width + 12, height: tagSize.height + 4)
        context.setFillColor(NSColor.black.withAlphaComponent(0.8).cgColor)
        context.fill(tagRect)
        drawText(hexString, at: CGPoint(x: tagRect.minX + 6, y: tagRect.minY + 2), font: font, color: .white, in: context)
        context.restoreGState()
    }

    // MARK: - Annotation Drawing Helpers

    private func drawAnnotation(_ annotation: Annotation, context: CGContext) {
        context.saveGState()
        switch annotation {
        case .rectangle(let r):
            context.setStrokeColor(r.style.color.nsColor.cgColor)
            context.setLineWidth(r.style.lineWidth)
            if r.isFilled {
                context.setFillColor(r.style.color.nsColor.cgColor)
                context.fill(r.rect)
            }
            context.stroke(r.rect)

        case .ellipse(let e):
            context.setStrokeColor(e.style.color.nsColor.cgColor)
            context.setLineWidth(e.style.lineWidth)
            if e.isFilled {
                context.setFillColor(e.style.color.nsColor.cgColor)
                context.fillEllipse(in: e.rect)
            }
            context.strokeEllipse(in: e.rect)

        case .line(let l):
            context.setStrokeColor(l.style.color.nsColor.cgColor)
            context.setLineWidth(l.style.lineWidth)
            context.move(to: l.startPoint)
            context.addLine(to: l.endPoint)
            context.strokePath()

        case .arrow(let a):
            context.setStrokeColor(a.style.color.nsColor.cgColor)
            context.setLineWidth(a.style.lineWidth)
            let angle: CGFloat
            if let cp = a.controlPoint {
                context.move(to: a.startPoint)
                context.addQuadCurve(to: a.endPoint, control: cp)
                context.strokePath()
                let dx = a.endPoint.x - cp.x
                let dy = a.endPoint.y - cp.y
                angle = atan2(dy, dx)
            } else {
                context.move(to: a.startPoint)
                context.addLine(to: a.endPoint)
                context.strokePath()
                let dx = a.endPoint.x - a.startPoint.x
                let dy = a.endPoint.y - a.startPoint.y
                angle = atan2(dy, dx)
            }
            drawArrowHead(at: a.endPoint, angle: angle, length: max(10, a.style.lineWidth * 3.5), context: context, color: a.style.color.nsColor)

        case .freehand(let f):
            guard f.points.count >= 2 else { break }
            context.setStrokeColor(f.style.color.nsColor.cgColor)
            context.setLineWidth(f.style.lineWidth)
            context.setLineCap(.round)
            context.setLineJoin(.round)
            context.move(to: f.points[0])
            for p in f.points.dropFirst() {
                context.addLine(to: p)
            }
            context.strokePath()

        case .text(let t):
            let attrs: [NSAttributedString.Key: Any] = [
                .font: NSFont.systemFont(ofSize: t.style.fontSize),
                .foregroundColor: t.style.color.nsColor
            ]
            (t.content as NSString).draw(in: t.bounds, withAttributes: attrs)

        case .mosaic(let m):
            drawMosaicAnnotation(m, context: context)

        case .highlight(let h):
            context.setFillColor(h.color.nsColor.withAlphaComponent(h.opacity).cgColor)
            context.fill(h.rect)

        case .numberLabel(let n):
            let circleRect = n.circleRect
            if let lead = n.leadPoint {
                context.setStrokeColor(n.color.nsColor.cgColor)
                context.setLineWidth(2.0)
                context.move(to: CGPoint(x: circleRect.midX, y: circleRect.midY))
                context.addLine(to: lead)
                context.strokePath()

                context.setFillColor(n.color.nsColor.cgColor)
                context.fillEllipse(in: CGRect(x: lead.x - 3.5, y: lead.y - 3.5, width: 7, height: 7))
            }
            context.setFillColor(n.color.nsColor.cgColor)
            context.fillEllipse(in: circleRect)
            let numStr = "\(n.number)"
            let font = NSFont.boldSystemFont(ofSize: n.size * 0.6)
            let strSize = measureText(numStr, font: font)
            drawText(
                numStr,
                at: CGPoint(x: circleRect.midX - strSize.width / 2, y: circleRect.midY - strSize.height / 2),
                font: font,
                color: .white,
                in: context
            )

        case .privacyRedaction(let pr):
            context.setFillColor(pr.fillColor.nsColor.cgColor)
            context.fill(pr.rect)

        case .calloutBubble(let cb):
            context.setFillColor(cb.bubbleColor.nsColor.cgColor)
            if !cb.rect.contains(cb.targetPoint) {
                let center = CGPoint(x: cb.rect.midX, y: cb.rect.midY)
                let dx = cb.targetPoint.x - center.x
                let dy = cb.targetPoint.y - center.y
                let dist = max(1, hypot(dx, dy))
                let perpX = -dy / dist
                let perpY = dx / dist
                let tailHalfWidth = min(10.0, min(cb.rect.width, cb.rect.height) * 0.22)

                let base1 = CGPoint(x: center.x + perpX * tailHalfWidth, y: center.y + perpY * tailHalfWidth)
                let base2 = CGPoint(x: center.x - perpX * tailHalfWidth, y: center.y - perpY * tailHalfWidth)

                let tailPath = CGMutablePath()
                tailPath.move(to: base1)
                tailPath.addLine(to: cb.targetPoint)
                tailPath.addLine(to: base2)
                tailPath.closeSubpath()
                context.addPath(tailPath)
                context.fillPath()
            }
            let path = CGPath(roundedRect: cb.rect, cornerWidth: 8, cornerHeight: 8, transform: nil)
            context.addPath(path)
            context.fillPath()
            let attrs: [NSAttributedString.Key: Any] = [
                .font: NSFont.systemFont(ofSize: cb.style.fontSize),
                .foregroundColor: cb.style.color.nsColor
            ]
            (cb.text as NSString).draw(in: cb.rect.insetBy(dx: 6, dy: 4), withAttributes: attrs)
        }
        context.restoreGState()
    }

    private func drawMosaicAnnotation(_ mosaic: MosaicAnnotation, context: CGContext) {
        let rect = mosaic.rect
        guard rect.width > 1, rect.height > 1 else { return }

        let scale = displaySnapshot.scaleFactor
        let imageBounds = CGRect(
            x: 0,
            y: 0,
            width: CGFloat(displaySnapshot.image.width),
            height: CGFloat(displaySnapshot.image.height)
        )
        let pixelRect = CGRect(
            x: rect.origin.x * scale,
            y: rect.origin.y * scale,
            width: rect.width * scale,
            height: rect.height * scale
        ).intersection(imageBounds)

        guard pixelRect.width > 1, pixelRect.height > 1 else {
            context.setFillColor(NSColor.gray.withAlphaComponent(0.6).cgColor)
            context.fill(rect)
            return
        }

        let pixelBlockSize = max(2, Int(round(CGFloat(mosaic.blockSize) * scale)))
        let cacheKey = MosaicCache.shared.key(
            rect: pixelRect,
            blockSize: pixelBlockSize,
            imageID: ObjectIdentifier(displaySnapshot.image)
        )

        let pixelatedCG: CGImage?
        if let cached = MosaicCache.shared.image(for: cacheKey) {
            pixelatedCG = cached
        } else if let cropped = displaySnapshot.image.cropping(to: pixelRect) {
            let ciImage = CIImage(cgImage: cropped)
            if let filter = CIFilter(name: "CIPixellate") {
                filter.setValue(ciImage, forKey: kCIInputImageKey)
                filter.setValue(
                    CIVector(x: ciImage.extent.midX, y: ciImage.extent.midY),
                    forKey: kCIInputCenterKey
                )
                filter.setValue(NSNumber(value: pixelBlockSize), forKey: kCIInputScaleKey)
                if let output = filter.outputImage {
                    let croppedOutput = output.cropped(to: ciImage.extent)
                    let generated = Self.sharedCIContext.createCGImage(croppedOutput, from: ciImage.extent)
                    if let generated {
                        MosaicCache.shared.setImage(generated, for: cacheKey)
                    }
                    pixelatedCG = generated
                } else {
                    pixelatedCG = nil
                }
            } else {
                pixelatedCG = nil
            }
        } else {
            pixelatedCG = nil
        }

        if let pixelatedCG {
            context.saveGState()
            context.translateBy(x: rect.minX, y: rect.maxY)
            context.scaleBy(x: 1.0, y: -1.0)
            context.draw(pixelatedCG, in: CGRect(origin: .zero, size: rect.size))
            context.restoreGState()
        } else {
            context.setFillColor(NSColor.gray.withAlphaComponent(0.6).cgColor)
            context.fill(rect)
        }
    }

    private func drawArrowHead(at point: CGPoint, angle: CGFloat, length: CGFloat, context: CGContext, color: NSColor) {
        let arrowAngle: CGFloat = .pi / 6.0
        let p1 = CGPoint(x: point.x - length * cos(angle - arrowAngle), y: point.y - length * sin(angle - arrowAngle))
        let p2 = CGPoint(x: point.x - length * cos(angle + arrowAngle), y: point.y - length * sin(angle + arrowAngle))

        context.saveGState()
        context.setFillColor(color.cgColor)
        context.move(to: point)
        context.addLine(to: p1)
        context.addLine(to: p2)
        context.closePath()
        context.fillPath()
        context.restoreGState()
    }

    // MARK: - Mouse Interaction

    override func scrollWheel(with event: NSEvent) {
        // Block all wheel events to prevent scrolling underlying applications
    }

    func updateHoverState(at localPoint: CGPoint) {
        mousePosition = localPoint

        if case .hovering = state {
            let quartzPoint = CoordinateMapper.displayLocalToQuartz(
                DisplayLocalRect(CGRect(origin: localPoint, size: .zero)),
                displayQuartzBounds: displaySnapshot.quartzBounds
            ).rawValue.origin

            let windowCandidates = WindowDetector.candidatesUnderPoint(
                quartzPoint,
                in: frozenSnapshotSet.candidateWindows
            )

            // Only filter out non-normal floating layers that cover almost the entire screen (e.g. background manager overlays)
            // Keep normal application windows (layer == 0) even if maximized, and keep normal dialogs/settings windows
            let qBounds = displaySnapshot.quartzBounds.rawValue
            let validCandidates = windowCandidates.filter { win in
                let isFullscreenNonNormal = win.windowLayer != 0
                    && win.frame.width >= qBounds.width * 0.98
                    && win.frame.height >= qBounds.height * 0.98
                return !isFullscreenNonNormal
            }
            candidateWindowsUnderMouse = validCandidates
        }

        updateCursor(at: localPoint)
        needsDisplay = true
    }

    override func mouseMoved(with event: NSEvent) {
        let localPoint = convert(event.locationInWindow, from: nil)
        updateHoverState(at: localPoint)
    }

    override func rightMouseDown(with event: NSEvent) {
        commitTextInput()
        dismissToolbar()
        delegate?.selectionOverlayDidCancel()
    }

    private func hitTestAnnotation(at point: CGPoint) -> Int? {
        committedAnnotations.indices.reversed().first { idx in
            committedAnnotations[idx].hitTest(point: point, tolerance: 8.0)
        }
    }

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)

        if isEditingText {
            commitTextInput()
        }

        // 1. Double click: re-edit text annotation if hit, otherwise confirm capture
        if event.clickCount >= 2 {
            if case .selected = state,
               let hitIdx = hitTestAnnotation(at: p),
               case .text = committedAnnotations[hitIdx] {
                beginEditingTextAnnotation(at: hitIdx)
                updateCursor(at: p)
                return
            }
            handleDoubleClick(at: p)
            return
        }

        switch state {
        case .hovering:
            hideToolbar()
            selectedAnnotationIndex = nil
            state = .creatingSelection(start: p, current: p)

        case .selected(let selRect):
            // 1. Check control handles of currently selected annotation
            if let selIdx = selectedAnnotationIndex,
               selIdx < committedAnnotations.count,
               let annoHandle = committedAnnotations[selIdx].hitTestHandle(at: p, padding: 4, tolerance: 10) {
                hideToolbar()
                state = .resizingAnnotation(
                    index: selIdx,
                    selectionRect: selRect,
                    handle: annoHandle,
                    originalAnnotations: committedAnnotations
                )
                updateCursor(at: p)
                needsDisplay = true
                return
            }

            // 2. Check selection box resize handles
            if let handle = hitTestHandle(at: p, in: selRect) {
                hideToolbar()
                selectedAnnotationIndex = nil
                state = .resizing(handle: handle, anchorRect: selRect, startPoint: p, currentRect: selRect)
                updateCursor(at: p)
                needsDisplay = true
                return
            }

            // 3. Check hit on any existing annotation (allows selecting/dragging existing annotations even when a tool is active)
            //    Single click selects and drags; double click (handled above) re-edits text
            if let hitIdx = hitTestAnnotation(at: p) {
                selectedAnnotationIndex = hitIdx
                hideToolbar()
                state = .movingAnnotation(
                    index: hitIdx,
                    selectionRect: selRect,
                    startPoint: p,
                    originalAnno: committedAnnotations[hitIdx],
                    originalAnnotations: committedAnnotations
                )
                updateCursor(at: p)
                needsDisplay = true
                return
            }

            // 4. Inside selection rect: start drawing with active tool or move selection box
            if selRect.contains(p) {
                if let tool = activeAnnotationTool {
                    if tool == .text {
                        beginTextInput(at: p, editingIndex: nil)
                        updateCursor(at: p)
                        return
                    }
                    selectedAnnotationIndex = nil
                    hideToolbar()
                    freehandPoints = [p]
                    state = .drawingAnnotation(tool: tool, selectionRect: selRect, start: p, current: p)
                    updateDraftAnnotation(start: p, current: p, tool: tool)
                } else {
                    selectedAnnotationIndex = nil
                    hideToolbar()
                    state = .moving(anchorRect: selRect, startPoint: p, currentRect: selRect)
                }
            } else {
                // Click outside the selection: only a real drag starts a new
                // selection — a plain click must leave the selection untouched.
                // The results drawer is hidden later, on confirmed drag/new
                // selection, so a stray click doesn't discard OCR results.
                selectionBeforeOutsideClick = selRect
                selectedAnnotationIndex = nil
                hideToolbar()
                state = .creatingSelection(start: p, current: p)
            }

        default:
            break
        }
        updateCursor(at: p)
        needsDisplay = true
    }

    override func mouseDragged(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        mousePosition = p

        switch state {
        case .creatingSelection(let start, _):
            state = .creatingSelection(start: start, current: p)
            // A real drag is starting: retire the previous selection's drawer.
            if resultsModel.isVisible, hypot(p.x - start.x, p.y - start.y) > 4 {
                hideResultsPanel()
            }

        case .resizing(let handle, let anchor, let start, _):
            let dx = p.x - start.x
            let dy = p.y - start.y
            let newRect = resizeRect(anchor, handle: handle, dx: dx, dy: dy)
            state = .resizing(handle: handle, anchorRect: anchor, startPoint: start, currentRect: newRect)
            layoutResultsPanel(for: newRect)

        case .moving(let anchor, let start, _):
            let dx = p.x - start.x
            let dy = p.y - start.y
            let moved = anchor.offsetBy(dx: dx, dy: dy).intersection(bounds)
            state = .moving(anchorRect: anchor, startPoint: start, currentRect: moved)
            layoutResultsPanel(for: moved)

        case .movingAnnotation(let index, _, let start, let originalAnno, _):
            let dx = p.x - start.x
            let dy = p.y - start.y
            if index < committedAnnotations.count {
                committedAnnotations[index] = originalAnno.offsetBy(dx: dx, dy: dy)
            }

        case .resizingAnnotation(let index, _, let handle, _):
            if index < committedAnnotations.count {
                // ⌘ held: lock the current aspect ratio
                let options = HandleDragOptions(
                    preserveAspectRatio: NSEvent.modifierFlags.contains(.command)
                )
                committedAnnotations[index] = committedAnnotations[index].applyingHandleDrag(handle, to: p, options: options)
            }

        case .drawingAnnotation(let tool, let selRect, let start, _):
            freehandPoints.append(p)
            state = .drawingAnnotation(tool: tool, selectionRect: selRect, start: start, current: p)
            updateDraftAnnotation(start: start, current: p, tool: tool)

        default:
            break
        }
        updateCursor(at: p)
        needsDisplay = true
    }

    override func mouseUp(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)

        switch state {
        case .creatingSelection(let start, let current):
            let rect = normalizedRect(from: start, to: current)
            if rect.width >= 5 && rect.height >= 5 {
                selectionBeforeOutsideClick = nil
                hideResultsPanel()
                commitSelection(rect)
                performAutomaticSelectionAction(in: rect)
            } else if let previous = selectionBeforeOutsideClick {
                // Plain click outside the selection: no reaction, keep the
                // selection and its toolbar exactly as they were.
                selectionBeforeOutsideClick = nil
                commitSelection(previous)
            } else if !isFreeSelectionLocked {
                if let win = candidateWindowsUnderMouse.first {
                    let local = CoordinateMapper.quartzToDisplayLocal(
                        QuartzGlobalRect(win.frame),
                        displayQuartzBounds: displaySnapshot.quartzBounds
                    )
                    commitSelection(local.rawValue.intersection(bounds))
                } else {
                    state = .hovering
                }
            } else {
                state = .hovering
            }

        case .resizing(_, _, _, let currentRect):
            commitSelection(currentRect)

        case .moving(_, _, let currentRect):
            commitSelection(currentRect)

        case .movingAnnotation(_, let selRect, _, _, let originalAnnotations):
            recordAnnotationHistory(
                description: "Move annotation",
                from: originalAnnotations,
                to: committedAnnotations
            )
            commitSelection(selRect)

        case .resizingAnnotation(_, let selRect, _, let originalAnnotations):
            recordAnnotationHistory(
                description: "Resize annotation",
                from: originalAnnotations,
                to: committedAnnotations
            )
            commitSelection(selRect)

        case .drawingAnnotation(let tool, let selRect, _, _):
            if let draft = currentDraftAnnotation {
                let oldSnapshot = committedAnnotations
                committedAnnotations.append(draft)
                selectedAnnotationIndex = committedAnnotations.count - 1
                recordAnnotationHistory(
                    description: "Add \(tool.rawValue)",
                    from: oldSnapshot,
                    to: committedAnnotations
                )
            }
            currentDraftAnnotation = nil
            freehandPoints.removeAll()
            commitSelection(selRect)

        default:
            break
        }
        updateCursor(at: p)
        needsDisplay = true
    }

    private func handleDoubleClick(at point: CGPoint) {
        commitTextInput()

        let targetRect: CGRect
        switch state {
        case .selected(let rect):
            guard rect.contains(point) else { return }
            targetRect = rect
        case .hovering, .creatingSelection:
            if !isFreeSelectionLocked {
                if let win = candidateWindowsUnderMouse.first {
                    let local = CoordinateMapper.quartzToDisplayLocal(
                        QuartzGlobalRect(win.frame),
                        displayQuartzBounds: displaySnapshot.quartzBounds
                    )
                    targetRect = local.rawValue.intersection(bounds)
                } else {
                    targetRect = bounds
                }
            } else {
                targetRect = bounds
            }
        default:
            return
        }

        guard targetRect.width >= 5 && targetRect.height >= 5 else { return }

        let quartzSel = CoordinateMapper.displayLocalToQuartz(
            DisplayLocalRect(targetRect),
            displayQuartzBounds: displaySnapshot.quartzBounds
        )

        let exportAnnotations = transformedAnnotationsForExport(in: targetRect)
        dismissToolbar()
        delegate?.selectionOverlay(
            didConfirmSelection: quartzSel,
            action: .copy,
            annotations: exportAnnotations,
            on: displaySnapshot
        )
    }

    private func commitSelection(_ rect: CGRect) {
        state = .selected(rect: rect)
        showToolbar(for: rect)
        // Drawer and selection form one interaction unit: keep them attached
        if resultsModel.isVisible {
            resultsPanelHost?.isHidden = false
            layoutResultsPanel(for: rect)
        }
    }

    private func updateDraftAnnotation(start: CGPoint, current: CGPoint, tool: SelectionInPlaceToolbar.AnnotationTool) {
        let style = StrokeStyle(color: CodableColor(Color(nsColor: annotationColor)), lineWidth: annotationLineWidth)
        var rect = normalizedRect(from: start, to: current)

        // ⌘ held: constrain rectangle/ellipse to a square / circle (1:1)
        if NSEvent.modifierFlags.contains(.command), tool == .rectangle || tool == .ellipse {
            rect = squareRect(from: start, to: current)
        }

        switch tool {
        case .rectangle:
            currentDraftAnnotation = .rectangle(RectangleAnnotation(rect: rect, style: style))
        case .ellipse:
            currentDraftAnnotation = .ellipse(EllipseAnnotation(rect: rect, style: style))
        case .line:
            currentDraftAnnotation = .line(LineAnnotation(startPoint: start, endPoint: current, style: style))
        case .arrow:
            currentDraftAnnotation = .arrow(ArrowAnnotation(startPoint: start, endPoint: current, style: style))
        case .pen:
            currentDraftAnnotation = .freehand(FreehandAnnotation(points: freehandPoints.isEmpty ? [start, current] : freehandPoints, style: style))
        case .text:
            break
        case .numberLabel:
            let nextNum = committedAnnotations.filter {
                if case .numberLabel = $0 { return true } else { return false }
            }.count + 1
            currentDraftAnnotation = .numberLabel(
                NumberLabelAnnotation(
                    position: start,
                    number: nextNum,
                    size: 26,
                    color: CodableColor(Color(nsColor: annotationColor)),
                    leadPoint: (hypot(current.x - start.x, current.y - start.y) > 4) ? current : nil
                )
            )
        case .highlight:
            currentDraftAnnotation = .highlight(
                HighlightAnnotation(
                    rect: rect,
                    color: CodableColor(Color(nsColor: annotationColor)),
                    opacity: 0.4
                )
            )
        case .mosaic:
            currentDraftAnnotation = .mosaic(MosaicAnnotation(rect: rect, blockSize: mosaicBlockSize))
        case .privacyRedaction:
            currentDraftAnnotation = .privacyRedaction(PrivacyRedactionAnnotation(rect: rect))
        }
    }

    // MARK: - In-Place Text Editing

    private func beginEditingTextAnnotation(at index: Int) {
        guard index >= 0, index < committedAnnotations.count else { return }
        if case .text(let textAnno) = committedAnnotations[index] {
            selectedAnnotationIndex = index
            beginTextInput(at: textAnno.position, editingIndex: index, initialText: textAnno.content, fontSize: textAnno.style.fontSize, color: textAnno.style.color.nsColor)
        }
    }

    private func beginTextInput(
        at point: CGPoint,
        editingIndex: Int?,
        initialText: String = "",
        fontSize: CGFloat? = nil,
        color: NSColor? = nil
    ) {
        if isEditingText {
            commitTextInput()
        }

        let resolvedFontSize = fontSize ?? max(14.0, annotationLineWidth * 5.0)
        let resolvedColor = color ?? annotationColor
        let font = NSFont.systemFont(ofSize: resolvedFontSize)

        editingAnnotationIndex = editingIndex
        textInputPosition = point

        let tf = NSTextField(frame: .zero)
        tf.isBordered = false
        tf.drawsBackground = true
        tf.backgroundColor = NSColor.textBackgroundColor.withAlphaComponent(0.9)
        tf.focusRingType = .none
        tf.font = font
        tf.textColor = resolvedColor
        tf.placeholderString = String(localized: "preview.enter.text")
        tf.stringValue = initialText
        tf.delegate = self
        tf.wantsLayer = true
        tf.layer?.cornerRadius = 4
        tf.layer?.borderWidth = 1.5
        tf.layer?.borderColor = NSColor.controlAccentColor.cgColor

        addSubview(tf)
        activeTextField = tf
        updateActiveTextFieldFrame()
        window?.makeFirstResponder(tf)
        if let editor = tf.currentEditor() {
            editor.selectedRange = NSRange(location: tf.stringValue.count, length: 0)
        }
        needsDisplay = true
    }

    private func updateActiveTextFieldFrame() {
        guard let tf = activeTextField, let pos = textInputPosition else { return }
        let font = tf.font ?? NSFont.systemFont(ofSize: 16)
        let sample = tf.stringValue.isEmpty ? (tf.placeholderString ?? "Text") : tf.stringValue
        let measured = measureText(sample, font: font)
        let width = max(90, measured.width + 20)
        let height = max(24, measured.height + 8)
        let clampedX = max(4, min(bounds.maxX - width - 4, pos.x))
        let clampedY = max(4, min(bounds.maxY - height - 4, pos.y))
        tf.frame = CGRect(x: clampedX, y: clampedY, width: width, height: height)
    }

    func commitTextInput() {
        guard let tf = activeTextField else { return }
        let rawText = tf.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        let editIdx = editingAnnotationIndex
        let pos = textInputPosition
        let fontSize = tf.font?.pointSize ?? max(14.0, annotationLineWidth * 5.0)
        let textColor = tf.textColor ?? annotationColor

        tf.delegate = nil
        tf.removeFromSuperview()
        activeTextField = nil
        editingAnnotationIndex = nil
        textInputPosition = nil
        window?.makeFirstResponder(self)

        if let editIdx, editIdx >= 0, editIdx < committedAnnotations.count {
            if case .text(var textAnno) = committedAnnotations[editIdx] {
                let oldSnapshot = committedAnnotations
                if rawText.isEmpty {
                    committedAnnotations.remove(at: editIdx)
                    selectedAnnotationIndex = nil
                    recordAnnotationHistory(description: "Delete text", from: oldSnapshot, to: committedAnnotations)
                } else if textAnno.content != rawText {
                    textAnno.content = rawText
                    committedAnnotations[editIdx] = .text(textAnno)
                    recordAnnotationHistory(description: "Edit text", from: oldSnapshot, to: committedAnnotations)
                }
            }
        } else if let pos, !rawText.isEmpty {
            let oldSnapshot = committedAnnotations
            let style = TextStyle(
                color: CodableColor(Color(nsColor: textColor)),
                fontSize: fontSize,
                fontName: ".AppleSystemUIFont"
            )
            let newAnno = Annotation.text(TextAnnotation(position: pos, content: rawText, style: style))
            committedAnnotations.append(newAnno)
            selectedAnnotationIndex = committedAnnotations.count - 1
            recordAnnotationHistory(description: "Add text", from: oldSnapshot, to: committedAnnotations)
        }

        needsDisplay = true
    }

    func cancelTextInput() {
        guard let tf = activeTextField else { return }
        tf.delegate = nil
        tf.removeFromSuperview()
        activeTextField = nil
        editingAnnotationIndex = nil
        textInputPosition = nil
        window?.makeFirstResponder(self)
        needsDisplay = true
    }

    func controlTextDidChange(_ obj: Notification) {
        updateActiveTextFieldFrame()
    }

    func controlTextDidEndEditing(_ obj: Notification) {
        if activeTextField != nil {
            commitTextInput()
        }
    }

    func control(_ control: NSControl, textView: NSTextView, doCommandBy commandSelector: Selector) -> Bool {
        if commandSelector == #selector(NSResponder.insertNewline(_:)) {
            commitTextInput()
            return true
        }
        if commandSelector == #selector(NSResponder.cancelOperation(_:)) {
            cancelTextInput()
            return true
        }
        return false
    }

    // MARK: - Undo/Redo History Helpers

    private func recordAnnotationHistory(
        description: String,
        from oldAnnotations: [Annotation],
        to newAnnotations: [Annotation]
    ) {
        guard oldAnnotations != newAnnotations else { return }
        editHistory.record(
            description: description,
            undo: { [weak self] in
                guard let self else { return }
                self.committedAnnotations = oldAnnotations
                if let sel = self.selectedAnnotationIndex, sel >= self.committedAnnotations.count {
                    self.selectedAnnotationIndex = nil
                }
                self.syncToolbarUndoState()
                self.needsDisplay = true
            },
            redo: { [weak self] in
                guard let self else { return }
                self.committedAnnotations = newAnnotations
                if let sel = self.selectedAnnotationIndex, sel >= self.committedAnnotations.count {
                    self.selectedAnnotationIndex = nil
                }
                self.syncToolbarUndoState()
                self.needsDisplay = true
            }
        )
        syncToolbarUndoState()
    }

    private func syncToolbarUndoState() {
        toolbar?.state.canUndo = editHistory.canUndo
        toolbar?.state.canRedo = editHistory.canRedo
    }

    private func transformedAnnotationsForExport(in selectionRect: CGRect) -> [Annotation] {
        let originX = selectionRect.origin.x
        let originY = selectionRect.origin.y
        let scale = displaySnapshot.scaleFactor
        return committedAnnotations.map { anno in
            anno.offsetBy(dx: -originX, dy: -originY).scaled(by: scale)
        }
    }

    // MARK: - Toolbar & Actions

    private func showToolbar(for selectionRect: CGRect) {
        let bar: SelectionInPlaceToolbar
        if let existing = toolbar {
            bar = existing
        } else {
            let newBar = SelectionInPlaceToolbar(frame: .zero)
            newBar.delegate = self
            addSubview(newBar)
            self.toolbar = newBar
            bar = newBar
        }
        syncToolbarUndoState()
        bar.state.isBeautifyEnabled = isBeautifyEnabled
        let targetFrame = SelectionInPlaceToolbar.preferredFrame(
            for: selectionRect,
            in: bounds,
            toolbarSize: bar.preferredSize
        )
        bar.frame = targetFrame
        bar.isHidden = false
    }

    private func hideToolbar() {
        toolbar?.isHidden = true
    }

    private func dismissToolbar() {
        hideResultsPanel()
        toolbar?.removeFromSuperview()
        toolbar = nil
    }

    func toolbarDidUpdateLayout() {
        guard case .selected(let rect) = state, let bar = toolbar, !bar.isHidden else { return }
        bar.frame = SelectionInPlaceToolbar.preferredFrame(
            for: rect,
            in: bounds,
            toolbarSize: bar.preferredSize
        )
    }

    func toolbarDidSelectAction(_ action: SelectionInPlaceToolbar.Action) {
        commitTextInput()

        guard case .selected(let rect) = state else {
            if action == .cancel {
                delegate?.selectionOverlayDidCancel()
            }
            return
        }

        if action == .cancel {
            delegate?.selectionOverlayDidCancel()
            return
        }

        if action == .beautify {
            isBeautifyEnabled.toggle()
            toolbar?.state.isBeautifyEnabled = isBeautifyEnabled
            needsDisplay = true
            return
        }

        if action == .smartMosaic {
            runSmartRedaction(in: rect)
            return
        }

        // OCR / Translate: expand the in-place results drawer, never leave this window
        if action == .ocr {
            runInPlaceAnalysis(tab: .ocr, in: rect)
            return
        }
        if action == .translate {
            runInPlaceAnalysis(tab: .translate, in: rect)
            return
        }

        // Convert DisplayLocal selection rect to Quartz global rect
        let quartzSel = CoordinateMapper.displayLocalToQuartz(
            DisplayLocalRect(rect),
            displayQuartzBounds: displaySnapshot.quartzBounds
        )

        let exportAnnotations = transformedAnnotationsForExport(in: rect)

        delegate?.selectionOverlay(
            didConfirmSelection: quartzSel,
            action: action,
            annotations: exportAnnotations,
            on: displaySnapshot
        )
    }

    func toolbarDidChangeTool(_ tool: SelectionInPlaceToolbar.AnnotationTool?) {
        commitTextInput()
        activeAnnotationTool = tool
    }

    func toolbarDidChangeColor(_ color: NSColor) {
        annotationColor = color
        if let tf = activeTextField {
            tf.textColor = color
        }
        if let idx = selectedAnnotationIndex, idx < committedAnnotations.count {
            let oldSnapshot = committedAnnotations
            committedAnnotations[idx] = committedAnnotations[idx].updatingColor(CodableColor(Color(nsColor: color)))
            recordAnnotationHistory(description: "Change color", from: oldSnapshot, to: committedAnnotations)
            needsDisplay = true
        }
    }

    // MARK: - Smart Privacy Mosaic

    /// Detects sensitive privacy regions inside the selection and adds mosaic
    /// annotations in one undoable step. Requires a configured VLM API key.
    private func runSmartRedaction(in selectionRect: CGRect) {

        let scale = displaySnapshot.scaleFactor
        let imageBounds = CGRect(
            x: 0,
            y: 0,
            width: CGFloat(displaySnapshot.image.width),
            height: CGFloat(displaySnapshot.image.height)
        )
        let pixelRect = CGRect(
            x: selectionRect.origin.x * scale,
            y: selectionRect.origin.y * scale,
            width: selectionRect.width * scale,
            height: selectionRect.height * scale
        ).intersection(imageBounds)
        guard pixelRect.width > 1, pixelRect.height > 1,
              let cropped = displaySnapshot.image.cropping(to: pixelRect) else {
            showStatusMessage(String(localized: "smartMosaic.error.failed"), isError: true)
            return
        }

        showStatusMessage(String(localized: "smartMosaic.detecting"), isError: false)

        Task { @MainActor [weak self] in
            guard let self else { return }
            do {
                let regions = try await SmartPrivacyRedactionService.shared.detectPrivacyRegions(in: cropped)
                guard !regions.isEmpty else {
                    self.showStatusMessage(String(localized: "smartMosaic.noneFound"), isError: false)
                    return
                }

                // Convert crop-pixel rects back to display-local coordinates
                let localRects = regions.map { region -> CGRect in
                    CGRect(
                        x: selectionRect.origin.x + region.origin.x / scale,
                        y: selectionRect.origin.y + region.origin.y / scale,
                        width: region.width / scale,
                        height: region.height / scale
                    )
                }

                let oldSnapshot = self.committedAnnotations
                for rect in localRects {
                    self.committedAnnotations.append(
                        .mosaic(MosaicAnnotation(rect: rect, blockSize: self.mosaicBlockSize))
                    )
                }
                self.recordAnnotationHistory(
                    description: "Smart redaction",
                    from: oldSnapshot,
                    to: self.committedAnnotations
                )
                self.needsDisplay = true
                self.showStatusMessage(
                    String(format: String(localized: "smartMosaic.redacted"), localRects.count),
                    isError: false
                )
            } catch {
                self.showStatusMessage(String(localized: "smartMosaic.error.failed"), isError: true)
            }
        }
    }

    /// Transient status message near the toolbar
    private func showStatusMessage(_ message: String, isError: Bool) {
        statusHideWorkItem?.cancel()

        let label = statusLabel ?? {
            let field = NSTextField(labelWithString: "")
            field.wantsLayer = true
            field.layer?.cornerRadius = 6
            field.layer?.backgroundColor = NSColor.black.withAlphaComponent(0.78).cgColor
            field.textColor = .white
            field.font = .systemFont(ofSize: 12, weight: .medium)
            field.alignment = .center
            addSubview(field)
            statusLabel = field
            return field
        }()

        label.stringValue = message
        label.textColor = isError ? NSColor.systemOrange : NSColor.white
        label.sizeToFit()
        let size = NSSize(width: label.frame.width + 20, height: label.frame.height + 10)
        let anchor = toolbar?.frame ?? NSRect(x: bounds.midX, y: 40, width: 0, height: 0)
        label.frame = NSRect(
            x: max(8, anchor.midX - size.width / 2),
            y: anchor.minY + anchor.height + 8,
            width: size.width,
            height: size.height
        )
        label.isHidden = false

        let work = DispatchWorkItem { [weak self] in
            self?.statusLabel?.isHidden = true
        }
        statusHideWorkItem = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.8, execute: work)
    }

    // MARK: - In-Place Results Drawer (OCR / Translation)

    /// Runs the mode's automatic action after a fresh selection commit.
    /// A plain click (selection restore) never reaches here.
    private func performAutomaticSelectionAction(in selectionRect: CGRect) {
        guard let action = automaticActionAfterSelection else { return }
        switch action {
        case .translate:
            runInPlaceAnalysis(tab: .translate, in: selectionRect)
        case .ocr:
            runInPlaceAnalysis(tab: .ocr, in: selectionRect)
        default:
            toolbarDidSelectAction(action)
        }
    }

    /// Runs OCR or translation on the selection and shows results in the
    /// embedded right-side drawer. The capture window stays open.
    private func runInPlaceAnalysis(tab: InPlaceResultsModel.Tab, in selectionRect: CGRect, forceRerun: Bool = false) {
        if !forceRerun, resultsModel.isVisible, resultsModel.activeTab == tab, !resultsModel.isLoading {
            hideResultsPanel()
            return
        }

        guard let cropped = cropSelection(selectionRect) else {
            showStatusMessage(String(localized: "error.ocr.failed"), isError: true)
            return
        }

        showResultsPanel(for: selectionRect, tab: tab)
        resultsModel.isLoading = true
        resultsModel.ocrLines = []
        resultsModel.segments = []
        resultsModel.statusMessage = nil

        // Cancel any in-flight run so a slow old result can't overwrite a new one
        inPlaceAnalysisTask?.cancel()
        inPlaceAnalysisGeneration &+= 1
        let generation = inPlaceAnalysisGeneration
        inPlaceAnalysisTask = Task { @MainActor [weak self] in
            guard let self, !Task.isCancelled else { return }
            defer {
                if self.inPlaceAnalysisGeneration == generation {
                    self.resultsModel.isLoading = false
                    self.layoutResultsPanel(for: selectionRect)
                    self.needsDisplay = true
                }
            }
            do {
                switch tab {
                case .ocr:
                    let result = try await OCRService.shared.recognize(
                        cropped,
                        // 识别语言跟随设置的源语言；auto / 不支持语言回退自动检测
                        languages: Set(
                            [AppSettings.shared.translationSourceLanguage]
                                .compactMap { OCREngine.RecognitionLanguage(translationLanguage: $0) }
                        )
                    )
                    if Task.isCancelled { return }
                    let lines = result.observations
                        .sorted { $0.boundingBox.minY < $1.boundingBox.minY }
                        .map(\.text)
                    self.resultsModel.ocrLines = lines
                    if lines.isEmpty {
                        self.resultsModel.statusMessage = String(localized: "error.ocr.no.text")
                    }
                case .translate:
                    let pipeline = ImageTranslationPipeline()
                    let context = PipelineContext()
                    let analysis: ScreenAnalysisResult
                    do {
                        analysis = try await pipeline.analyze(image: cropped, context: context)
                    } catch {
                        guard self.inPlaceAnalysisGeneration == generation else { return }
                        self.resultsModel.statusMessage = String(
                            format: String(localized: "error.pipeline.ocrFailed"),
                            Self.pipelineErrorMessage(error)
                        )
                        return
                    }
                    let filtered = analysis.filteredForTranslation()
                    guard !filtered.segments.isEmpty else {
                        self.resultsModel.statusMessage = String(localized: "error.ocr.no.text")
                        return
                    }
                    let config = ImageTranslationPipelineConfig.fromAppSettings(scene: nil)
                    do {
                        let segments = try await pipeline.translate(
                            analysisResult: filtered,
                            config: config,
                            context: context
                        )
                        if Task.isCancelled { return }
                        guard self.inPlaceAnalysisGeneration == generation else { return }
                        self.resultsModel.segments = segments
                    } catch {
                        guard self.inPlaceAnalysisGeneration == generation else { return }
                        self.resultsModel.statusMessage = String(
                            format: String(localized: "error.pipeline.translateFailed"),
                            Self.pipelineErrorMessage(error)
                        )
                    }
                }
            } catch {
                guard self.inPlaceAnalysisGeneration == generation else { return }
                let message = Self.pipelineErrorMessage(error)
                self.resultsModel.statusMessage = message
            }
        }
    }

    private static func pipelineErrorMessage(_ error: Error) -> String {
        (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
    }

    /// Crops the selection region from the frozen snapshot (pixel space).
    private func cropSelection(_ selectionRect: CGRect) -> CGImage? {
        let scale = displaySnapshot.scaleFactor
        let imageBounds = CGRect(
            x: 0,
            y: 0,
            width: CGFloat(displaySnapshot.image.width),
            height: CGFloat(displaySnapshot.image.height)
        )
        let pixelRect = CGRect(
            x: selectionRect.origin.x * scale,
            y: selectionRect.origin.y * scale,
            width: selectionRect.width * scale,
            height: selectionRect.height * scale
        ).intersection(imageBounds)
        guard pixelRect.width > 1, pixelRect.height > 1 else { return nil }
        return displaySnapshot.image.cropping(to: pixelRect)
    }

    private func showResultsPanel(for selectionRect: CGRect, tab: InPlaceResultsModel.Tab) {
        if resultsPanelHost == nil {
            resultsModel.onRerun = { [weak self] in
                guard let self, case .selected(let rect) = self.state else { return }
                self.runInPlaceAnalysis(tab: self.resultsModel.activeTab, in: rect, forceRerun: true)
            }
            resultsModel.onClose = { [weak self] in
                self?.hideResultsPanel()
            }
            let host = NSHostingView(rootView: InPlaceResultsPanel(model: resultsModel))
            host.autoresizingMask = []
            addSubview(host)
            resultsPanelHost = host
        }
        resultsModel.visualInvalidator = { [weak self] in self?.needsDisplay = true }
        resultsModel.activeTab = tab
        resultsModel.isVisible = true
        resultsPanelHost?.isHidden = false
        layoutResultsPanel(for: selectionRect)
    }

    private func layoutResultsPanel(for selectionRect: CGRect) {
        guard let host = resultsPanelHost, resultsModel.isVisible else { return }
        host.frame = Self.resultsPanelFrame(
            selectionRect: selectionRect,
            bounds: bounds,
            handleSize: handleSize
        )
    }

    /// Frame for the merged results drawer, in flipped (Y-down) local coords.
    /// Sits beside the selection's right edge with a gap that clears the
    /// resize handles (whose hit areas reach `handleSize + 2` past the edge —
    /// child views win hit testing, so an overlapping handle would swallow
    /// its clicks), and never enters the toolbar zone below the selection
    /// (toolbar top sits at maxY + 8).
    static func resultsPanelFrame(
        selectionRect: CGRect,
        bounds: CGRect,
        handleSize: CGFloat
    ) -> CGRect {
        let gap: CGFloat = handleSize + 6
        let width: CGFloat = 300
        let desiredHeight = max(220, min(selectionRect.height, bounds.maxY - 16))
        let x: CGFloat = {
            let preferred = selectionRect.maxX + gap
            if preferred + width <= bounds.maxX - 8 { return preferred }
            // Left side preferred; when neither side fits (near-fullscreen
            // selection) hug the screen's right edge.
            let left = selectionRect.minX - width - gap
            return left >= 8 ? left : bounds.maxX - width - 8
        }()
        let y = max(8, min(selectionRect.maxY - desiredHeight, bounds.maxY - desiredHeight - 8))
        // Cap the height so the drawer bottom never crosses the toolbar top.
        let toolbarZoneTop = selectionRect.maxY + 8
        let height = min(desiredHeight, max(40, toolbarZoneTop - y), bounds.maxY - 8 - y)
        return NSRect(x: x, y: y, width: width, height: height)
    }

    private func hideResultsPanel() {
        inPlaceAnalysisTask?.cancel()
        resultsModel.isVisible = false
        resultsPanelHost?.isHidden = true
        needsDisplay = true
    }

    func toolbarDidChangeMosaicIntensity(_ blockSize: Int) {
        mosaicBlockSize = blockSize
        if let idx = selectedAnnotationIndex, idx < committedAnnotations.count,
           case .mosaic(var mosaic) = committedAnnotations[idx] {
            let oldSnapshot = committedAnnotations
            mosaic.blockSize = blockSize
            committedAnnotations[idx] = .mosaic(mosaic)
            recordAnnotationHistory(description: "Change mosaic intensity", from: oldSnapshot, to: committedAnnotations)
            needsDisplay = true
        }
    }

    func toolbarDidChangeLineWidth(_ width: CGFloat) {
        annotationLineWidth = width
        if let tf = activeTextField {
            tf.font = NSFont.systemFont(ofSize: max(14.0, width * 5.0))
            updateActiveTextFieldFrame()
        }
        if let idx = selectedAnnotationIndex, idx < committedAnnotations.count {
            let oldSnapshot = committedAnnotations
            committedAnnotations[idx] = committedAnnotations[idx].updatingLineWidth(width)
            recordAnnotationHistory(description: "Change line width", from: oldSnapshot, to: committedAnnotations)
            needsDisplay = true
        }
    }

    func toolbarDidRequestUndo() {
        commitTextInput()
        editHistory.undo()
        syncToolbarUndoState()
    }

    func toolbarDidRequestRedo() {
        commitTextInput()
        editHistory.redo()
        syncToolbarUndoState()
    }

    // MARK: - Keyboard Controls

    override func keyDown(with event: NSEvent) {
        // While the results drawer owns keyboard focus, its controls (and the
        // text view) keep their keys: only Escape falls through, closing the
        // drawer instead of cancelling the whole capture session.
        if resultsPanelHasKeyboardFocus {
            if event.keyCode == 53 {
                hideResultsPanel()
                window?.makeFirstResponder(self)
                return
            }
            super.keyDown(with: event)
            return
        }

        if isEditingText {
            if event.keyCode == 53 {
                cancelTextInput()
                return
            }
            if event.keyCode == 36 || event.keyCode == 76 {
                commitTextInput()
                return
            }
            super.keyDown(with: event)
            return
        }

        // ⌘Z / ⌘⇧Z for Undo / Redo
        if event.modifierFlags.contains(.command) && event.keyCode == 6 {
            if event.modifierFlags.contains(.shift) {
                toolbarDidRequestRedo()
            } else {
                toolbarDidRequestUndo()
            }
            return
        }

        switch event.keyCode {
        case 53: // Escape
            dismissToolbar()
            delegate?.selectionOverlayDidCancel()

        case 51: // Backspace / Delete
            if let idx = selectedAnnotationIndex, idx < committedAnnotations.count {
                let oldSnapshot = committedAnnotations
                committedAnnotations.remove(at: idx)
                selectedAnnotationIndex = nil
                recordAnnotationHistory(description: "Delete annotation", from: oldSnapshot, to: committedAnnotations)
                needsDisplay = true
            }

        case 1: // S -> Save
            if event.modifierFlags.contains(.command) {
                toolbarDidSelectAction(.save)
            }

        case 36: // Enter
            toolbarDidSelectAction(.copy)

        case 35: // P -> Pin
            toolbarDidSelectAction(.pin)

        case 17: // T -> Translate
            toolbarDidSelectAction(.translate)

        case 48: // Tab -> Cycle overlapping window candidates
            if !candidateWindowsUnderMouse.isEmpty {
                candidateWindowsUnderMouse.rotate(1)
                needsDisplay = true
            }

        case 49: // Space -> Toggle free selection vs window snapping
            isFreeSelectionLocked.toggle()
            needsDisplay = true

        case 8: // C -> Copy color under cursor
            if let p = mousePosition {
                copyHexColor(at: p)
            }

        // Arrow keys for fine-grained 1px / 10px nudging
        case 123, 124, 125, 126:
            nudgeSelection(keyCode: event.keyCode, shift: event.modifierFlags.contains(.shift))

        default:
            super.keyDown(with: event)
        }
    }

    private func nudgeSelection(keyCode: UInt16, shift: Bool) {
        guard case .selected(let r) = state else { return }
        let step: CGFloat = shift ? 10.0 : 1.0
        var offset = CGPoint.zero
        if keyCode == 123 { offset.x = -step }
        if keyCode == 124 { offset.x = step }
        if keyCode == 125 { offset.y = step }
        if keyCode == 126 { offset.y = -step }

        let moved = r.offsetBy(dx: offset.x, dy: offset.y).intersection(bounds)
        commitSelection(moved)
        needsDisplay = true
    }

    private func copyHexColor(at point: CGPoint) {
        let scale = displaySnapshot.scaleFactor
        let ix = Int(point.x * scale)
        let iy = Int(point.y * scale)
        guard let ptr = pixelBytes else { return }

        let bpr = displaySnapshot.image.bytesPerRow
        let bpp = displaySnapshot.image.bitsPerPixel / 8
        if ix >= 0 && ix < displaySnapshot.image.width && iy >= 0 && iy < displaySnapshot.image.height {
            let offset = iy * bpr + ix * bpp
            let hex = String(format: "#%02X%02X%02X", ptr[offset], ptr[offset + 1], ptr[offset + 2])
            NSPasteboard.general.clearContents()
            NSPasteboard.general.setString(hex, forType: .string)
        }
    }

    // MARK: - Geometry & Handle Helpers

    private func point(for handle: ResizeHandle, in rect: CGRect) -> CGPoint {
        switch handle {
        case .topLeft: return CGPoint(x: rect.minX, y: rect.minY)
        case .topCenter: return CGPoint(x: rect.midX, y: rect.minY)
        case .topRight: return CGPoint(x: rect.maxX, y: rect.minY)
        case .middleLeft: return CGPoint(x: rect.minX, y: rect.midY)
        case .middleRight: return CGPoint(x: rect.maxX, y: rect.midY)
        case .bottomLeft: return CGPoint(x: rect.minX, y: rect.maxY)
        case .bottomCenter: return CGPoint(x: rect.midX, y: rect.maxY)
        case .bottomRight: return CGPoint(x: rect.maxX, y: rect.maxY)
        }
    }

    private func hitTestHandle(at point: CGPoint, in rect: CGRect) -> ResizeHandle? {
        let hitDist: CGFloat = handleSize + 2
        for handle in ResizeHandle.allCases {
            let hp = self.point(for: handle, in: rect)
            if hypot(point.x - hp.x, point.y - hp.y) <= hitDist {
                return handle
            }
        }
        return nil
    }

    private func resizeRect(_ rect: CGRect, handle: ResizeHandle, dx: CGFloat, dy: CGFloat) -> CGRect {
        var minX = rect.minX
        var minY = rect.minY
        var maxX = rect.maxX
        var maxY = rect.maxY

        switch handle {
        case .topLeft:
            minX += dx; minY += dy
        case .topCenter:
            minY += dy
        case .topRight:
            maxX += dx; minY += dy
        case .middleLeft:
            minX += dx
        case .middleRight:
            maxX += dx
        case .bottomLeft:
            minX += dx; maxY += dy
        case .bottomCenter:
            maxY += dy
        case .bottomRight:
            maxX += dx; maxY += dy
        }

        return normalizedRect(from: CGPoint(x: minX, y: minY), to: CGPoint(x: maxX, y: maxY))
    }

    /// True when a control inside the results drawer (e.g. its text view or
    /// the language menu) is the window's first responder.
    private var resultsPanelHasKeyboardFocus: Bool {
        guard let host = resultsPanelHost, !host.isHidden,
              let responder = window?.firstResponder as? NSView else { return false }
        var current: NSView? = responder
        while let view = current {
            if view === host { return true }
            current = view.superview
        }
        return false
    }

    private func updateCursor(at point: CGPoint) {
        if let tf = activeTextField, !tf.isHidden, tf.frame.contains(point) {
            NSCursor.iBeam.set()
            return
        }

        if let bar = toolbar, !bar.isHidden, bar.frame.contains(point) {
            NSCursor.arrow.set()
            return
        }

        // The results drawer hosts its own cursor management (arrow/iBeam);
        // never show selection cursors (crosshair/grab) on top of it.
        if let panel = resultsPanelHost, !panel.isHidden, panel.frame.contains(point) {
            NSCursor.arrow.set()
            return
        }

        switch state {
        case .moving, .movingAnnotation:
            NSCursor.closedHand.set()
            return

        case .resizing(let handle, _, _, _):
            handle.cursor().set()
            return

        case .resizingAnnotation(_, _, let handle, _):
            handle.cursor.set()
            return

        case .drawingAnnotation(let tool, _, _, _):
            tool.cursor.set()
            return

        case .creatingSelection:
            NSCursor.crosshair.set()
            return

        case .selected(let selRect):
            if let selIdx = selectedAnnotationIndex,
               selIdx < committedAnnotations.count,
               let annoHandle = committedAnnotations[selIdx].hitTestHandle(at: point, padding: 4, tolerance: 10) {
                annoHandle.cursor.set()
                return
            }

            if let handle = hitTestHandle(at: point, in: selRect) {
                handle.cursor().set()
                return
            }

            if let hitIdx = hitTestAnnotation(at: point) {
                if activeAnnotationTool == .text, case .text = committedAnnotations[hitIdx] {
                    NSCursor.iBeam.set()
                    return
                }
                NSCursor.openHand.set()
                return
            }

            if selRect.contains(point) {
                if let tool = activeAnnotationTool {
                    tool.cursor.set()
                    return
                }
                NSCursor.openHand.set()
                return
            }

            NSCursor.crosshair.set()

        case .hovering:
            NSCursor.crosshair.set()
        }
    }

    /// 1:1 rectangle from start to current point (⌘ constraint)
    private func squareRect(from start: CGPoint, to end: CGPoint) -> CGRect {
        GeometryConstraint.squareRect(from: start, to: end)
    }

    private func normalizedRect(from start: CGPoint, to end: CGPoint) -> CGRect {
        let minX = min(start.x, end.x)
        let minY = min(start.y, end.y)
        let width = abs(end.x - start.x)
        let height = abs(end.y - start.y)
        return CGRect(x: minX, y: minY, width: width, height: height)
    }
}

private extension Array {
    mutating func rotate(_ shift: Int) {
        guard !isEmpty else { return }
        let amount = shift % count
        self = Array(self[amount...] + self[..<amount])
    }
}
