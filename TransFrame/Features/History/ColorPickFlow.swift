//
//  ColorPickFlow.swift
//  TransFrame
//
//  Shared entry point for completed screen color picks (spec §4.5): every
//  eyedropper surface (tray menu, preview toolbar, selection-in-place
//  toolbar) funnels through `ColorPickFlow.complete(...)` so all of them get
//  the same clipboard write, history insert, `.colorDidPick` broadcast, and
//  "View History" toast. No surface implements the flow locally.
//

import AppKit

/// Converts an arbitrary picked NSColor into the canonical `#RRGGBB` form,
/// or nil when the color has no sRGB representation. MainActor: the flow
/// touches the pasteboard, the shared history controller, and the toast.
@MainActor
enum ColorPickFlow {
    /// Full pick completion pipeline.
    /// - Parameters:
    ///   - color: the color returned by NSColorSampler
    ///   - writeToPasteboard: whether the HEX should also go to the
    ///     clipboard (annotation eyedroppers keep the clipboard untouched —
    ///     the user is mid-annotation and may have content staged)
    static func complete(_ color: NSColor, writeToPasteboard: Bool = true) {
        guard let hex = hexString(from: color) else { return }

        if writeToPasteboard {
            NSPasteboard.general.clearContents()
            NSPasteboard.general.setString(hex, forType: .string)
        }

        // Touching the shared controller guarantees the singleton history
        // store exists and observes this broadcast (the store is created by
        // HistoryWindowController).
        _ = HistoryWindowController.shared
        NotificationCenter.default.post(name: .colorDidPick, object: hex)

        // spec §4.1 entry #2: completion toast with "View History" link
        ColorPickToastController.shared.show(hex: hex)
    }

    /// sRGB conversion + rounding, shared by all pick surfaces.
    static func hexString(from color: NSColor) -> String? {
        guard let rgbColor = color.usingColorSpace(.sRGB) else { return nil }
        let r = Int(round(rgbColor.redComponent * 255))
        let g = Int(round(rgbColor.greenComponent * 255))
        let b = Int(round(rgbColor.blueComponent * 255))
        return String(format: "#%02X%02X%02X", r, g, b)
    }
}
