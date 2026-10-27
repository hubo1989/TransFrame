import SwiftUI
import AppKit

/// Extension to add cursor support to SwiftUI views
extension View {
    /// Sets the cursor to use when hovering over this view
    /// - Parameter cursor: The NSCursor to display
    /// - Returns: A view that changes the cursor on hover and updates immediately on state changes
    func cursor(_ cursor: NSCursor) -> some View {
        self.modifier(CursorViewModifier(cursor: cursor))
    }

    /// Adds scroll wheel zoom handling to the view
    /// - Parameter action: Closure called with the scroll delta (positive = up/zoom in)
    func onScrollWheelZoom(action: @escaping (CGFloat) -> Void) -> some View {
        self.background(
            ScrollWheelZoomView(action: action)
        )
    }
}

private struct CursorViewModifier: ViewModifier {
    let cursor: NSCursor
    @State private var isHovering = false

    func body(content: Content) -> some View {
        content
            .onHover { hovering in
                isHovering = hovering
                // Paired push/pop keeps nested .cursor views correct: leaving
                // the inner view pops back to the outer cursor instead of
                // forcing the arrow.
                if hovering {
                    cursor.push()
                } else {
                    NSCursor.pop()
                }
            }
            .onChange(of: cursor) { _, newCursor in
                if isHovering {
                    NSCursor.pop()
                    newCursor.push()
                }
            }
            .onDisappear {
                if isHovering {
                    isHovering = false
                    NSCursor.pop()
                }
            }
    }
}

// MARK: - NSCursor Symbol & Resize Extension

extension NSCursor {
    nonisolated(unsafe) private static var cursorCache: [String: NSCursor] = [:]
    private static let cacheLock = NSLock()

    /// Creates a custom cursor rendered from an SF Symbol with high-contrast white fill and dark outline
    static func symbolCursor(systemName: String, hotSpot: NSPoint = NSPoint(x: 10, y: 10)) -> NSCursor? {
        let cacheKey = "\(systemName)_\(hotSpot.x)_\(hotSpot.y)"
        cacheLock.lock()
        if let cached = cursorCache[cacheKey] {
            cacheLock.unlock()
            return cached
        }
        cacheLock.unlock()

        let config = NSImage.SymbolConfiguration(pointSize: 14, weight: .semibold)
        guard let rawSymbol = NSImage(systemSymbolName: systemName, accessibilityDescription: nil)?.withSymbolConfiguration(config) else {
            return nil
        }

        let iconSize = NSSize(width: 16, height: 16)
        let whiteSymbol = tintedImage(rawSymbol, color: .white, size: iconSize)
        let blackSymbol = tintedImage(rawSymbol, color: NSColor.black.withAlphaComponent(0.9), size: iconSize)

        let cursorSize = NSSize(width: 20, height: 20)
        let finalImage = NSImage(size: cursorSize, flipped: false) { _ in
            let baseRect = NSRect(x: 2, y: 2, width: 16, height: 16)
            let offsets: [(CGFloat, CGFloat)] = [
                (-1, 0), (1, 0), (0, -1), (0, 1),
                (-0.8, -0.8), (0.8, -0.8), (-0.8, 0.8), (0.8, 0.8)
            ]
            for (dx, dy) in offsets {
                blackSymbol.draw(in: baseRect.offsetBy(dx: dx, dy: dy))
            }
            whiteSymbol.draw(in: baseRect)
            return true
        }
        finalImage.isTemplate = false

        let cursor = NSCursor(image: finalImage, hotSpot: hotSpot)
        cacheLock.lock()
        cursorCache[cacheKey] = cursor
        cacheLock.unlock()
        return cursor
    }

    /// Diagonal resize cursor (NW-SE ↖↘ or NE-SW ↗↙)
    static func diagonalResizeCursor(northWestSouthEast: Bool) -> NSCursor {
        let symbolName = northWestSouthEast
            ? "arrow.up.left.and.arrow.down.right"
            : "arrow.up.right.and.arrow.down.left"
        if let cursor = symbolCursor(systemName: symbolName, hotSpot: NSPoint(x: 10, y: 10)) {
            return cursor
        }
        return .crosshair
    }

    private static func tintedImage(_ image: NSImage, color: NSColor, size: NSSize) -> NSImage {
        let tinted = NSImage(size: size, flipped: false) { rect in
            image.draw(in: rect)
            color.set()
            rect.fill(using: .sourceAtop)
            return true
        }
        tinted.isTemplate = false
        return tinted
    }
}

/// NSViewRepresentable that captures scroll wheel events for zooming
private struct ScrollWheelZoomView: NSViewRepresentable {
    let action: (CGFloat) -> Void

    func makeNSView(context: Context) -> ScrollWheelCaptureView {
        let view = ScrollWheelCaptureView()
        view.onScrollWheel = action
        return view
    }

    func updateNSView(_ nsView: ScrollWheelCaptureView, context: Context) {
        nsView.onScrollWheel = action
    }
}

/// Custom NSView that captures scroll wheel events
private class ScrollWheelCaptureView: NSView {
    var onScrollWheel: ((CGFloat) -> Void)?

    override func scrollWheel(with event: NSEvent) {
        // Only handle scroll wheel zoom when Command key is held
        // or when using a mouse (not trackpad for scrolling)
        if event.modifierFlags.contains(.command) || event.phase.isEmpty {
            // Use deltaY for vertical scroll (zoom)
            let delta = event.scrollingDeltaY
            if abs(delta) > 0.1 {
                onScrollWheel?(delta)
            }
        } else {
            // Pass through to parent for normal scrolling
            super.scrollWheel(with: event)
        }
    }

    override var acceptsFirstResponder: Bool { true }
}
