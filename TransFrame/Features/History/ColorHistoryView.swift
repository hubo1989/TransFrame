//
//  ColorHistoryView.swift
//  TransFrame
//
//  Independent color-history window content: grid of large color swatches
//  with HEX captions, search / favorites / copy-format controls, keyboard
//  navigation, and live top-insertion highlighting (spec §4).
//

import SwiftUI
import AppKit

/// Copy format for the color history grid (spec §4.3), persisted per user.
enum ColorCopyFormat: String, Codable, CaseIterable, Sendable {
    case hex
    case rgb

    var displayName: String {
        switch self {
        case .hex:
            return String(localized: "colorHistory.format.hex")
        case .rgb:
            return String(localized: "colorHistory.format.rgb")
        }
    }
}

/// Pure HEX helpers — the shared acceptance-test vectors (spec §5.3) call
/// these, so they must stay free of UI dependencies.
enum ColorHex {
    /// Normalizes user-facing hex text into the canonical `#RRGGBB` form
    /// (uppercase, 6 digits). Accepts `#abc` shorthand and lower/upper case,
    /// with or without the leading `#`. Returns nil when the string is not
    /// a valid hex color (spec §5.3: lower → upper, 3-digit → 6-digit).
    static func normalize(_ text: String) -> String? {
        var hex = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if hex.hasPrefix("#") {
            hex.removeFirst()
        }
        guard !hex.isEmpty else { return nil }

        // 3-digit shorthand expands by doubling each digit
        if hex.count == 3 {
            let chars = Array(hex)
            hex = "\(chars[0])\(chars[0])\(chars[1])\(chars[1])\(chars[2])\(chars[2])"
        }

        let upper = hex.uppercased()
        guard upper.count == 6, upper.allSatisfy({ $0.isHexDigit }) else { return nil }
        return "#" + upper
    }

    /// Parses `#RRGGBB` into 0–255 RGB components. Invalid input yields nil.
    static func rgbComponents(_ normalizedHex: String) -> (r: Int, g: Int, b: Int)? {
        guard let normalized = normalize(normalizedHex),
              normalized.count == 7 else { return nil }
        let start = normalized.index(normalized.startIndex, offsetBy: 1)
        let hex = String(normalized[start...])
        guard let value = UInt32(hex, radix: 16) else { return nil }
        return (Int((value >> 16) & 0xFF), Int((value >> 8) & 0xFF), Int(value & 0xFF))
    }

    /// `RGB(r, g, b)` string used by the RGB copy format (spec §4.3).
    static func rgbString(_ normalizedHex: String) -> String? {
        guard let rgb = rgbComponents(normalizedHex) else { return nil }
        return "RGB(\(rgb.r), \(rgb.g), \(rgb.b))"
    }
}

// MARK: - Main View

/// Main view for the independent color history window (spec §4).
struct ColorHistoryView: View {
    @ObservedObject var store: HistoryStore

    /// Copy format persisted across launches (spec §4.3).
    @AppStorage("TransFrame.colorHistoryCopyFormat") private var copyFormat: ColorCopyFormat = .hex

    /// Live top-insertion highlight: id of the most recently picked color
    /// (spec §4.5), cleared after the animation window.
    @State private var highlightedEntryID: UUID?
    @AppStorage("TransFrame.colorHistorySearch") private var searchText = ""
    @State private var favoritesOnly = false

    var body: some View {
        VStack(spacing: 0) {
            ColorHistoryToolBar(
                searchText: Binding(
                    get: { searchText },
                    set: { searchText = $0 }
                ),
                favoritesOnly: Binding(
                    get: { favoritesOnly },
                    set: { favoritesOnly = $0 }
                ),
                copyFormat: $copyFormat,
                onClearAll: {
                    // spec §4.3: confirmation dialog; only the color category
                    // is cleared inside ColorHistoryViewModel-style flow —
                    // the alert is presented from `showClearAlert`.
                    showClearAlert = true
                }
            )

            Divider()

            let entries = store.colorEntries(
                matching: searchText,
                favoritesOnly: favoritesOnly
            )

            if entries.isEmpty {
                ColorHistoryEmptyView(searchActive: !searchText.isEmpty)
            } else {
                ScrollView {
                    ColorHistoryGrid(
                        entries: entries,
                        copyFormat: copyFormat,
                        highlightedEntryID: highlightedEntryID,
                        onCopy: { entry in
                            copy(entry)
                        },
                        onToggleFavorite: { entry in
                            store.toggleFavorite(for: entry.id)
                        },
                        onDelete: { entry in
                            store.remove(entry)
                        }
                    )
                    .padding(12)
                }
            }
        }
        .frame(minWidth: 420, minHeight: 320)
        .background(VisualEffectView(material: .sidebar, blendingMode: .behindWindow))
        // spec §4.4: Esc clears the search from anywhere in the window
        .onExitCommand {
            if !searchText.isEmpty {
                searchText = ""
            }
        }
        .alert(
            localized("colorHistory.clear.alert.title"),
            isPresented: $showClearAlert
        ) {
            Button(localized("button.cancel"), role: .cancel) {}
            Button(localized("button.clear"), role: .destructive) {
                store.clearColorHistory()
            }
        } message: {
            Text(localized("colorHistory.clear.alert.message"))
        }
        .onReceive(store.$entries) { newEntries in
            // spec §4.5: while the window is open, a fresh pick must appear
            // at the top with a highlight animation. New picks land at index
            // 0 with the newest timestamp.
            guard let first = newEntries.first, first.category == .color else { return }
            guard first.id != lastKnownTopID else { return }
            let isFresh = first.timestamp.timeIntervalSinceNow > -2
            if isFresh {
                withAnimation(.easeInOut(duration: 0.6)) {
                    highlightedEntryID = first.id
                }
                let id = first.id
                DispatchQueue.main.asyncAfter(deadline: .now() + 1.2) {
                    if highlightedEntryID == id {
                        withAnimation(.easeOut(duration: 0.3)) {
                            highlightedEntryID = nil
                        }
                    }
                }
            }
            lastKnownTopID = first.id
        }
        .onAppear {
            if let first = store.entries.first {
                lastKnownTopID = first.id
            }
        }
    }

    // MARK: - Private State

    @State private var showClearAlert = false
    @State private var lastKnownTopID: UUID?

    // MARK: - Actions

    /// Copies in the active format and flashes the "copied" feedback on the
    /// swatch (spec §4.4: single click copies + 1s "Copied" badge).
    private func copy(_ entry: TranslationHistory) {
        guard let normalized = ColorHex.normalize(entry.translatedText) else { return }
        let text: String
        switch copyFormat {
        case .hex:
            text = normalized
        case .rgb:
            text = ColorHex.rgbString(normalized) ?? normalized
        }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }
}

// MARK: - Tool Bar

/// Top bar: search field, favorites-only toggle, copy-format picker,
/// clear-all button (spec §4.3).
private struct ColorHistoryToolBar: View {
    @Binding var searchText: String
    @Binding var favoritesOnly: Bool
    @Binding var copyFormat: ColorCopyFormat

    let onClearAll: () -> Void

    @FocusState private var searchFocused: Bool

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: "magnifyingglass")
                .foregroundStyle(.secondary)

            TextField(String(localized: "colorHistory.search.placeholder"), text: $searchText)
                .focused($searchFocused)
                .textFieldStyle(.plain)
                .onExitCommand {
                    // spec §4.4: Esc clears the search
                    if !searchText.isEmpty {
                        searchText = ""
                    } else {
                        searchFocused = false
                    }
                }

            if !searchText.isEmpty {
                Button {
                    searchText = ""
                    searchFocused = true
                } label: {
                    Image(systemName: "xmark.circle.fill")
                        .foregroundStyle(.secondary)
                }
                .buttonStyle(.plain)
                .help(String(localized: "history.clear.search"))
            }

            Spacer(minLength: 8)

            // Favorites-only filter (spec §4.3), stacks with search
            Button {
                favoritesOnly.toggle()
            } label: {
                Image(systemName: favoritesOnly ? "star.fill" : "star")
                    .foregroundStyle(favoritesOnly ? .yellow : .secondary)
            }
            .buttonStyle(.plain)
            .help(String(localized: "colorHistory.filter.favoritesOnly"))

            // Copy format toggle (spec §4.3), persisted via AppStorage
            Picker(String(localized: "colorHistory.copy.format"), selection: $copyFormat) {
                ForEach(ColorCopyFormat.allCases, id: \.self) { format in
                    Text(format.displayName).tag(format)
                }
            }
            .pickerStyle(.menu)
            .frame(width: 92)
            .help(String(localized: "colorHistory.copy.format"))

            // Clear all color entries (spec §4.3) — confirmation upstream
            Button(role: .destructive) {
                onClearAll()
            } label: {
                Image(systemName: "trash")
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.plain)
            .help(String(localized: "colorHistory.clear.all"))
        }
        .padding(12)
    }
}

// MARK: - Grid

/// Adaptive color-swatch grid: every cell ≥ 48×48pt with the HEX caption
/// in a monospaced font (spec §4.2).
///
/// Keyboard model (spec §4.4): each cell is focusable; Tab cycles cells,
/// arrow keys move focus to grid neighbors, Enter/Space/Delete act on the
/// focused cell, Esc clears the search (handled by the search field).
private struct ColorHistoryGrid: View {
    let entries: [TranslationHistory]
    let copyFormat: ColorCopyFormat
    let highlightedEntryID: UUID?
    let onCopy: (TranslationHistory) -> Void
    let onToggleFavorite: (TranslationHistory) -> Void
    let onDelete: (TranslationHistory) -> Void

    /// Cell side length: at least 48pt (spec §4.2), growing with the window.
    private let cellSize: CGFloat = 64

    /// The currently focused (keyboard-selected) cell.
    @FocusState private var focusedCellID: UUID?

    /// Measured grid width, used to compute the column count for
    /// up/down keyboard navigation.
    @State private var gridWidth: CGFloat = 560

    private var columns: [GridItem] {
        [GridItem(.adaptive(minimum: cellSize, maximum: 96), spacing: 12)]
    }

    var body: some View {
        LazyVGrid(columns: columns, spacing: 12) {
                ForEach(entries) { entry in
                    ColorSwatchCell(
                        entry: entry,
                        copyFormat: copyFormat,
                        isSelected: focusedCellID == entry.id,
                        isHighlighted: highlightedEntryID == entry.id,
                        onCopy: { onCopy(entry) },
                        onToggleFavorite: { onToggleFavorite(entry) },
                        onDelete: { onDelete(entry) }
                    )
                    .id(entry.id)
                    .focused($focusedCellID, equals: entry.id)
                    .onMoveCommand { direction in
                        moveFocus(from: entry, direction: direction)
                    }
                }
            }
        .background(GeometryReader { geo in
            Color.clear.onAppear { gridWidth = geo.size.width }
                .onChange(of: geo.size.width) { _, width in gridWidth = width }
        })
    }

    // MARK: - Keyboard Navigation

    /// Moves focus from `entry` to its grid neighbor in `direction`.
    private func moveFocus(from entry: TranslationHistory, direction: MoveCommandDirection) {
        guard let currentIndex = entries.firstIndex(where: { $0.id == entry.id }) else { return }
        let count = entries.count
        let stride = max(1, columnCount)

        let nextIndex: Int
        switch direction {
        case .left:
            nextIndex = max(0, currentIndex - 1)
        case .right:
            nextIndex = min(count - 1, currentIndex + 1)
        case .up:
            nextIndex = max(0, currentIndex - stride)
        case .down:
            nextIndex = min(count - 1, currentIndex + stride)
        @unknown default:
            nextIndex = currentIndex
        }

        guard nextIndex != currentIndex else { return }
        focusedCellID = entries[nextIndex].id
    }

    /// Columns currently laid out, derived from the measured grid width.
    private var columnCount: Int {
        max(1, Int(gridWidth / (cellSize + 12)))
    }
}

// MARK: - Swatch Cell

/// A single color cell: swatch + monospaced HEX caption, hover actions,
/// favorite star badge, copied flash, and tooltip (spec §4.2/§4.4).
private struct ColorSwatchCell: View {
    let entry: TranslationHistory
    let copyFormat: ColorCopyFormat
    let isSelected: Bool
    let isHighlighted: Bool

    let onCopy: () -> Void
    let onToggleFavorite: () -> Void
    let onDelete: () -> Void

    @State private var isHovered = false
    @State private var showCopiedBadge = false

    private var normalizedHex: String {
        ColorHex.normalize(entry.translatedText) ?? entry.translatedText
    }

    private var swatchColor: Color {
        // A malformed stored hex renders as a neutral placeholder instead of
        // crashing the grid.
        guard let nsColor = nsColorFromHex(normalizedHex) else {
            return Color(nsColor: .systemGray)
        }
        return Color(nsColor: nsColor)
    }

    var body: some View {
        VStack(spacing: 4) {
            swatch
                .frame(height: 48)
                .frame(maxWidth: .infinity)
                .overlay(alignment: .topLeading) { favoriteBadge }
                .overlay(alignment: .center) { copiedBadge }
                .overlay(alignment: .bottomTrailing) { hoverActions }
                .clipShape(RoundedRectangle(cornerRadius: DesignSystem.Radii.small))
                .overlay(
                    RoundedRectangle(cornerRadius: DesignSystem.Radii.small)
                        .strokeBorder(
                            isSelected ? Color.accentColor :
                                (isHighlighted ? Color.accentColor.opacity(0.8) : Color(nsColor: .separatorColor).opacity(0.5)),
                            lineWidth: isSelected ? 2 : (isHighlighted ? 2 : 0.5)
                        )
                )
                .contentShape(Rectangle())
                .onTapGesture {
                    // spec §4.4: single click copies in the current format
                    // and flashes the "Copied" badge for 1s
                    onCopy()
                    flashCopiedBadge()
                }
                .onHover { hovering in
                    isHovered = hovering
                }
                .contextMenu {
                    Button {
                        if let hex = ColorHex.normalize(entry.translatedText) { try? ClipboardService().copyText(hex) }
                    } label: {
                        Label(String(localized: "colorHistory.copy.hex"), systemImage: "doc.on.doc")
                    }
                    Button {
                        copyRGB()
                    } label: {
                        Label(String(localized: "colorHistory.copy.rgb"), systemImage: "doc.on.doc.fill")
                    }
                    Divider()
                    Button {
                        onToggleFavorite()
                    } label: {
                        Label(
                            entry.isFavorite
                                ? String(localized: "colorHistory.unfavorite")
                                : String(localized: "colorHistory.favorite"),
                            systemImage: entry.isFavorite ? "star.slash" : "star"
                        )
                    }
                    Button(role: .destructive) {
                        onDelete()
                    } label: {
                        Label(String(localized: "history.delete"), systemImage: "trash")
                    }
                }
                .help(tooltipText)

            Text(normalizedHex)
                .font(.system(size: 11, design: .monospaced))
                .foregroundStyle(.secondary)
                .lineLimit(1)
                .truncationMode(.middle)
        }
        .padding(4)
        .background(
            RoundedRectangle(cornerRadius: DesignSystem.Radii.small)
                .fill(isHighlighted ? Color.accentColor.opacity(0.12) : Color.clear)
        )
        // spec §4.4: cells participate in Tab navigation; Enter / Space /
        // Delete act on the focused cell directly.
        .focusable()
        .focusEffectDisabled()
        .onKeyPress(.return) {
            onCopy()
            flashCopiedBadge()
            return .handled
        }
        .onKeyPress(.space) {
            onToggleFavorite()
            return .handled
        }
        .onKeyPress(.delete, phases: .down) { _ in
            onDelete()
            return .handled
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(Text("\(String(localized: "colorHistory.title")) \(normalizedHex)"))
        .accessibilityHint(Text(String(localized: "colorHistory.copy.format")))
        .accessibilityAddTraits(isSelected ? [.isSelected] : [])
    }

    // MARK: - Pieces

    /// The large color block (≥48pt tall per spec §4.2).
    private var swatch: some View {
        Rectangle()
            .fill(swatchColor)
    }

    /// Favorite entries always show a star badge (spec §4.2).
    private var favoriteBadge: some View {
        Group {
            if entry.isFavorite {
                Image(systemName: "star.fill")
                    .font(.system(size: 10))
                    .foregroundStyle(.yellow)
                    .shadow(color: .black.opacity(0.4), radius: 1)
                    .padding(4)
            }
        }
    }

    /// "Copied" flash overlay (spec §4.4, 1s).
    private var copiedBadge: some View {
        Group {
            if showCopiedBadge {
                Text(String(localized: "colorHistory.copied"))
                    .font(.system(size: 10, weight: .semibold))
                    .foregroundStyle(.white)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 3)
                    .background(Color.black.opacity(0.65))
                    .clipShape(Capsule())
                    .transition(.opacity)
            }
        }
    }

    /// Hover actions: copy / favorite / delete (spec §4.2).
    private var hoverActions: some View {
        Group {
            if isHovered {
                HStack(spacing: 2) {
                    actionButton(icon: "doc.on.doc", help: String(localized: "colorHistory.copy.hex")) {
                        onCopy()
                        flashCopiedBadge()
                    }
                    actionButton(
                        icon: entry.isFavorite ? "star.fill" : "star",
                                        help: entry.isFavorite
                                            ? String(localized: "colorHistory.unfavorite")
                                            : String(localized: "colorHistory.favorite")
                    ) {
                        onToggleFavorite()
                    }
                    actionButton(icon: "trash", help: String(localized: "history.delete"), role: .destructive) {
                        onDelete()
                    }
                }
                .padding(3)
                .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: DesignSystem.Radii.small))
                .transition(.opacity)
            }
        }
    }

    private func actionButton(icon: String, help: String, role: ButtonRole? = nil, action: @escaping () -> Void) -> some View {
        Button(role: role, action: action) {
            Image(systemName: icon)
                .font(.system(size: 10))
                .foregroundStyle(role == .destructive ? Color.red : Color.primary)
                .frame(width: 18, height: 18)
        }
        .buttonStyle(.plain)
        .help(help)
    }

    /// Tooltip with full HEX / RGB / pick time (spec §4.2).
    private var tooltipText: String {
        var lines: [String] = [normalizedHex]
        if let rgb = ColorHex.rgbString(normalizedHex) {
            lines.append(rgb)
        }
        lines.append(entry.fullDateString)
        return lines.joined(separator: "\n")
    }

    // MARK: - Actions

    private func copyRGB() {
        guard let rgb = ColorHex.rgbString(normalizedHex) else { return }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(rgb, forType: .string)
    }

    private func flashCopiedBadge() {
        withAnimation(.easeIn(duration: 0.1)) {
            showCopiedBadge = true
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
            withAnimation(.easeOut(duration: 0.2)) {
                showCopiedBadge = false
            }
        }
    }
}

// MARK: - Empty State

/// Empty state: centered illustration + guidance + eyedropper shortcut hint
/// (spec §4.2).
private struct ColorHistoryEmptyView: View {
    let searchActive: Bool

    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: "eyedropper")
                .font(.system(size: 48))
                .foregroundStyle(.secondary)

            if searchActive {
                Text("colorHistory.no.results.title")
                    .font(.headline)
                    .foregroundStyle(.secondary)

                Text("colorHistory.no.results.message")
                    .font(.body)
                    .foregroundStyle(.tertiary)
            } else {
                Text("colorHistory.empty.title")
                    .font(.headline)
                    .foregroundStyle(.secondary)

                Text("colorHistory.empty.message")
                    .font(.body)
                    .foregroundStyle(.tertiary)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

// MARK: - Hex → NSColor

/// Converts a normalized hex string into an sRGB NSColor.
private func nsColorFromHex(_ hex: String) -> NSColor? {
    guard let rgb = ColorHex.rgbComponents(hex) else { return nil }
    return NSColor(
        srgbRed: CGFloat(rgb.r) / 255.0,
        green: CGFloat(rgb.g) / 255.0,
        blue: CGFloat(rgb.b) / 255.0,
        alpha: 1.0
    )
}
