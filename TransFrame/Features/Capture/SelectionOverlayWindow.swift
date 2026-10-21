import AppKit
import CoreGraphics
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
        level = .screenSaver
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
            return NSCursor(image: NSImage(size: NSSize(width: 16, height: 16)), hotSpot: NSPoint(x: 8, y: 8))
        case .topRight, .bottomLeft:
            return NSCursor.crosshair
        case .topCenter, .bottomCenter:
            return NSCursor.resizeUpDown
        case .middleLeft, .middleRight:
            return NSCursor.resizeLeftRight
        }
    }
}

// MARK: - SelectionOverlayView

final class SelectionOverlayView: NSView, SelectionToolbarDelegate {
    enum InteractionState {
        case hovering
        case creatingSelection(start: CGPoint, current: CGPoint)
        case selected(rect: CGRect)
        case resizing(handle: ResizeHandle, anchorRect: CGRect, startPoint: CGPoint, currentRect: CGRect)
        case moving(anchorRect: CGRect, startPoint: CGPoint, currentRect: CGRect)
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
    private var activeAnnotationTool: SelectionInPlaceToolbar.AnnotationTool?
    private var annotationColor: NSColor = .systemRed
    private var annotationLineWidth: CGFloat = 3.0
    private var currentDraftAnnotation: Annotation?
    private var freehandPoints: [CGPoint] = []

    private var toolbar: SelectionInPlaceToolbar?
    private let editHistory = EditHistory()

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

        // 3. Draw selection border, resize handles, and dimension tag
        if let sel = currentSelection, !sel.isEmpty {
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
        for annotation in committedAnnotations {
            drawAnnotation(annotation, context: context)
        }
        if let draft = currentDraftAnnotation {
            drawAnnotation(draft, context: context)
        }

        // 5. Draw pixel loupe when hovering
        if case .hovering = state, let mousePos = mousePosition {
            drawLoupe(at: mousePos, context: context)
        }
    }

    // MARK: - Selection Rendering Helpers

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
            context.move(to: a.startPoint)
            context.addLine(to: a.endPoint)
            context.strokePath()

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
            context.setFillColor(NSColor.gray.withAlphaComponent(0.6).cgColor)
            context.fill(m.rect)

        case .highlight(let h):
            context.setFillColor(h.color.nsColor.withAlphaComponent(h.opacity).cgColor)
            context.fill(h.rect)

        case .numberLabel(let n):
            context.setFillColor(n.color.nsColor.cgColor)
            context.fillEllipse(in: n.bounds)
            let numStr = "\(n.number)"
            let font = NSFont.boldSystemFont(ofSize: n.size * 0.6)
            let strSize = measureText(numStr, font: font)
            drawText(
                numStr,
                at: CGPoint(x: n.bounds.midX - strSize.width / 2, y: n.bounds.midY - strSize.height / 2),
                font: font,
                color: .white,
                in: context
            )

        case .privacyRedaction(let pr):
            context.setFillColor(pr.fillColor.nsColor.cgColor)
            context.fill(pr.rect)

        case .calloutBubble(let cb):
            context.setFillColor(cb.bubbleColor.nsColor.cgColor)
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
        dismissToolbar()
        delegate?.selectionOverlayDidCancel()
    }

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)

        // 1. Double click: immediately confirm capture for target window/selection
        if event.clickCount >= 2 {
            handleDoubleClick(at: p)
            return
        }

        if let tool = activeAnnotationTool, case .selected(let selRect) = state, selRect.contains(p) {
            // Start drawing annotation within selected region
            dismissToolbar()
            freehandPoints = [p]
            state = .drawingAnnotation(tool: tool, selectionRect: selRect, start: p, current: p)
            updateDraftAnnotation(start: p, current: p, tool: tool)
            needsDisplay = true
            return
        }

        switch state {
        case .hovering:
            dismissToolbar()
            state = .creatingSelection(start: p, current: p)

        case .selected(let selRect):
            dismissToolbar()
            if let handle = hitTestHandle(at: p, in: selRect) {
                state = .resizing(handle: handle, anchorRect: selRect, startPoint: p, currentRect: selRect)
            } else if selRect.contains(p) {
                state = .moving(anchorRect: selRect, startPoint: p, currentRect: selRect)
            } else {
                // Click outside: reset selection back to creating selection
                state = .creatingSelection(start: p, current: p)
            }

        default:
            break
        }
        needsDisplay = true
    }

    override func mouseDragged(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        mousePosition = p

        switch state {
        case .creatingSelection(let start, _):
            state = .creatingSelection(start: start, current: p)

        case .resizing(let handle, let anchor, let start, _):
            let dx = p.x - start.x
            let dy = p.y - start.y
            let newRect = resizeRect(anchor, handle: handle, dx: dx, dy: dy)
            state = .resizing(handle: handle, anchorRect: anchor, startPoint: start, currentRect: newRect)

        case .moving(let anchor, let start, _):
            let dx = p.x - start.x
            let dy = p.y - start.y
            let moved = anchor.offsetBy(dx: dx, dy: dy).intersection(bounds)
            state = .moving(anchorRect: anchor, startPoint: start, currentRect: moved)

        case .drawingAnnotation(let tool, let selRect, let start, _):
            freehandPoints.append(p)
            state = .drawingAnnotation(tool: tool, selectionRect: selRect, start: start, current: p)
            updateDraftAnnotation(start: start, current: p, tool: tool)

        default:
            break
        }
        needsDisplay = true
    }

    override func mouseUp(with event: NSEvent) {
        switch state {
        case .creatingSelection(let start, let current):
            let rect = normalizedRect(from: start, to: current)
            if rect.width >= 5 && rect.height >= 5 {
                commitSelection(rect)
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

        case .drawingAnnotation(let tool, let selRect, _, _):
            if let draft = currentDraftAnnotation {
                committedAnnotations.append(draft)
                editHistory.record(
                    description: "Add \(tool.rawValue)",
                    undo: { [weak self] in
                        _ = self?.committedAnnotations.popLast()
                        self?.needsDisplay = true
                    },
                    redo: { [weak self] in
                        self?.committedAnnotations.append(draft)
                        self?.needsDisplay = true
                    }
                )
            }
            currentDraftAnnotation = nil
            freehandPoints.removeAll()
            commitSelection(selRect)

        default:
            break
        }
        needsDisplay = true
    }

    private func handleDoubleClick(at point: CGPoint) {
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

        dismissToolbar()
        delegate?.selectionOverlay(
            didConfirmSelection: quartzSel,
            action: .copy,
            annotations: committedAnnotations,
            on: displaySnapshot
        )
    }

    private func commitSelection(_ rect: CGRect) {
        state = .selected(rect: rect)
        showToolbar(for: rect)
    }

    private func updateDraftAnnotation(start: CGPoint, current: CGPoint, tool: SelectionInPlaceToolbar.AnnotationTool) {
        let style = StrokeStyle(color: CodableColor(Color(nsColor: annotationColor)), lineWidth: annotationLineWidth)
        let rect = normalizedRect(from: start, to: current)

        switch tool {
        case .rectangle:
            currentDraftAnnotation = .rectangle(RectangleAnnotation(rect: rect, style: style))
        case .ellipse:
            currentDraftAnnotation = .ellipse(EllipseAnnotation(rect: rect, style: style))
        case .arrow:
            currentDraftAnnotation = .arrow(ArrowAnnotation(startPoint: start, endPoint: current, style: style))
        case .pen:
            currentDraftAnnotation = .freehand(FreehandAnnotation(points: freehandPoints.isEmpty ? [start, current] : freehandPoints, style: style))
        case .text:
            currentDraftAnnotation = .text(TextAnnotation(position: rect.origin, content: "Text"))
        case .mosaic:
            currentDraftAnnotation = .mosaic(MosaicAnnotation(rect: rect))
        case .privacyRedaction:
            currentDraftAnnotation = .privacyRedaction(PrivacyRedactionAnnotation(rect: rect))
        }
    }

    // MARK: - Toolbar & Actions

    private func showToolbar(for selectionRect: CGRect) {
        dismissToolbar()
        let bar = SelectionInPlaceToolbar(frame: .zero)
        bar.delegate = self
        let targetFrame = SelectionInPlaceToolbar.preferredFrame(for: selectionRect, in: bounds)
        bar.frame = targetFrame
        addSubview(bar)
        self.toolbar = bar
    }

    private func dismissToolbar() {
        toolbar?.removeFromSuperview()
        toolbar = nil
    }

    func toolbarDidSelectAction(_ action: SelectionInPlaceToolbar.Action) {
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

        // Convert DisplayLocal selection rect to Quartz global rect
        let quartzSel = CoordinateMapper.displayLocalToQuartz(
            DisplayLocalRect(rect),
            displayQuartzBounds: displaySnapshot.quartzBounds
        )

        delegate?.selectionOverlay(
            didConfirmSelection: quartzSel,
            action: action,
            annotations: committedAnnotations,
            on: displaySnapshot
        )
    }

    func toolbarDidChangeTool(_ tool: SelectionInPlaceToolbar.AnnotationTool?) {
        activeAnnotationTool = tool
    }

    func toolbarDidChangeColor(_ color: NSColor) {
        annotationColor = color
    }

    func toolbarDidChangeLineWidth(_ width: CGFloat) {
        annotationLineWidth = width
    }

    func toolbarDidRequestUndo() {
        editHistory.undo()
    }

    func toolbarDidRequestRedo() {
        editHistory.redo()
    }

    // MARK: - Keyboard Controls

    override func keyDown(with event: NSEvent) {
        switch event.keyCode {
        case 53: // Escape
            dismissToolbar()
            delegate?.selectionOverlayDidCancel()

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

    private func updateCursor(at point: CGPoint) {
        if case .selected(let selRect) = state {
            if let handle = hitTestHandle(at: point, in: selRect) {
                handle.cursor().set()
                return
            }
            if selRect.contains(point) {
                NSCursor.openHand.set()
                return
            }
        }
        NSCursor.crosshair.set()
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
