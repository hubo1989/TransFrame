//
//  ColorPickToastController.swift
//  TransFrame
//
//  Lightweight non-activating toast shown after a successful screen color
//  pick (spec §4.1): "copied #HEX" plus a "view history" link, auto-dismiss
//  after 1.5 seconds.
//

import AppKit
import SwiftUI

/// Shows a short toast near the current cursor after each completed pick.
/// A newer pick replaces the previous toast (and cancels its timer).
@MainActor
final class ColorPickToastController {
    // MARK: - Singleton

    static let shared = ColorPickToastController()

    // MARK: - Properties

    private var panel: NSPanel?
    private var hostingView: NSHostingView<ColorPickToastView>?
    private var dismissTask: Task<Void, Never>?
    private var lastHex: String = ""

    private init() {}

    // MARK: - Public API

    /// Shows (or refreshes) the toast for a freshly picked hex color.
    /// - Parameter hex: The picked color in `#RRGGBB` form
    func show(hex: String) {
        lastHex = hex
        let anchor = ColorPickToastAnchor.currentCursorRect()

        if panel == nil {
            createPanel(hex: hex)
        } else {
            refresh(hex: hex)
        }

        positionPanel(near: anchor)
        panel?.orderFrontRegardless()

        // spec §4.1: toast disappears after 1.5s; a newer pick resets it
        dismissTask?.cancel()
        dismissTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(1.5))
            guard !Task.isCancelled else { return }
            self?.dismiss()
        }
    }

    /// Dismisses the toast immediately (also called by the history link).
    func dismiss() {
        dismissTask?.cancel()
        dismissTask = nil
        panel?.close()
        panel = nil
        hostingView = nil
    }

    // MARK: - Private

    private func refresh(hex: String) {
        guard let hostingView else { return }
        hostingView.rootView = makeToastView(hex: hex)
    }

    private func makeToastView(hex: String) -> ColorPickToastView {
        ColorPickToastView(
            hex: hex,
            onViewHistory: { [weak self] in
                self?.dismiss()
                ColorHistoryWindowController.shared.showColorHistory()
            }
        )
    }

    private func createPanel(hex: String) {
        let contentView = makeToastView(hex: hex)
        let hostingView = NSHostingView(rootView: contentView)
        hostingView.frame = NSRect(x: 0, y: 0, width: 260, height: 44)

        let panel = NSPanel(
            contentRect: hostingView.frame,
            styleMask: [.borderless, .nonactivatingPanel],
            backing: .buffered,
            defer: false
        )

        panel.contentView = hostingView
        panel.level = .statusBar
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = true
        // spec §4.2/§4.4 interaction stays with the toast: the history link
        // must be clickable, unlike a pure status flash.
        panel.ignoresMouseEvents = false
        panel.hidesOnDeactivate = false
        panel.isReleasedWhenClosed = false
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .ignoresCycle]

        self.panel = panel
        self.hostingView = hostingView
    }

    private func positionPanel(near anchor: CGRect) {
        let point = CGPoint(x: anchor.midX, y: anchor.midY)
        guard let panel, let screen = NSScreen.screens.first(where: { $0.frame.contains(point) }) ?? NSScreen.main else { return }

        let contentSize = hostingView?.frame.size ?? NSSize(width: 260, height: 44)
        // Place above the cursor (or at the anchor's top edge); flip below
        // when running into the menu bar.
        var origin = CGPoint(
            x: anchor.midX - contentSize.width / 2,
            y: anchor.maxY + 8
        )

        origin.x = max(screen.visibleFrame.minX + 8, min(origin.x, screen.visibleFrame.maxX - contentSize.width - 8))
        if origin.y + contentSize.height > screen.visibleFrame.maxY {
            origin.y = anchor.minY - contentSize.height - 8
        }
        origin.y = max(screen.visibleFrame.minY + 8, origin.y)

        panel.setFrame(NSRect(origin: origin, size: contentSize), display: false)
    }
}

// MARK: - Anchor

/// Locates where the toast should appear: the mouse position at pick time.
private enum ColorPickToastAnchor {
    static func currentCursorRect() -> CGRect {
        let location = NSEvent.mouseLocation
        return CGRect(x: location.x, y: location.y - 2, width: 4, height: 4)
    }
}

// MARK: - Toast View

private struct ColorPickToastView: View {
    let hex: String
    let onViewHistory: () -> Void

    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: "eyedropper")
                .font(.system(size: 12))
                .foregroundStyle(.secondary)

            Text(hex)
                .font(.system(size: 12, design: .monospaced))
                .lineLimit(1)

            Button(action: onViewHistory) {
                Text("colorHistory.toast.viewHistory")
                    .font(.system(size: 11, weight: .medium))
                    .foregroundStyle(Color.accentColor)
            }
            .buttonStyle(.plain)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(
            RoundedRectangle(cornerRadius: DesignSystem.Radii.medium)
                .fill(.ultraThinMaterial)
        )
        .overlay(
            RoundedRectangle(cornerRadius: DesignSystem.Radii.medium)
                .stroke(Color(nsColor: .separatorColor).opacity(0.5), lineWidth: 0.5)
        )
        .shadow(color: .black.opacity(0.2), radius: 6, y: 2)
    }
}
