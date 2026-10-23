import SwiftUI
import AppKit

/// Selectable, read-only text block backed by NSTextView.
/// Supports continuous selection across lines (unlike per-row SwiftUI Text)
/// and copies a whole line to the pasteboard on a plain click (PRD 5.5).
struct SelectableTextBlock: NSViewRepresentable {
    let attributedText: NSAttributedString
    /// Called after a plain click copied one line; used for UI feedback.
    var onLineCopied: ((String) -> Void)? = nil

    func makeNSView(context: Context) -> ResultsTextView {
        let view = ResultsTextView()
        view.drawsBackground = false
        view.isEditable = false
        view.isSelectable = true
        view.isRichText = true
        view.allowsUndo = false
        view.textContainerInset = .zero
        view.textContainer?.lineFragmentPadding = 0
        view.textContainer?.widthTracksTextView = true
        view.isHorizontallyResizable = false
        view.isVerticallyResizable = false
        view.autoresizingMask = [.width]
        view.onLineCopied = onLineCopied
        return view
    }

    func updateNSView(_ view: ResultsTextView, context: Context) {
        view.onLineCopied = onLineCopied
        if view.attributedString() != attributedText {
            let selection = view.selectedRange()
            view.textStorage?.setAttributedString(attributedText)
            // Keep the selection valid across content refreshes
            if selection.location + selection.length <= attributedText.length {
                view.setSelectedRange(selection)
            } else {
                view.setSelectedRange(NSRange(location: 0, length: 0))
            }
        }
    }

    func sizeThatFits(
        _ proposal: ProposedViewSize,
        nsView view: ResultsTextView,
        context: Context
    ) -> NSSize? {
        guard let width = proposal.width, width > 0,
              let layoutManager = view.layoutManager,
              let container = view.textContainer else {
            return nil
        }
        container.containerSize = NSSize(width: width, height: .greatestFiniteMagnitude)
        let used = layoutManager.usedRect(for: container)
        return NSSize(width: width, height: ceil(used.height) + 2)
    }
}

// MARK: - ResultsTextView

/// Read-only text view: drag selects across lines; a plain click copies the
/// clicked line to the pasteboard.
final class ResultsTextView: NSTextView {
    var onLineCopied: ((String) -> Void)?

    private var clickOrigin: NSPoint?

    override func mouseDown(with event: NSEvent) {
        clickOrigin = event.locationInWindow
        super.mouseDown(with: event)
    }

    override func mouseUp(with event: NSEvent) {
        super.mouseUp(with: event)
        defer { clickOrigin = nil }

        guard event.clickCount == 1, let origin = clickOrigin else { return }
        let travel = hypot(event.locationInWindow.x - origin.x, event.locationInWindow.y - origin.y)
        guard travel < 3 else { return }   // drag = selection, click = copy line

        let local = convert(event.locationInWindow, from: nil)
        let index = characterIndexForInsertion(at: local)
        guard let line = Self.lineString(at: index, in: string) else { return }

        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(line, forType: .string)
        onLineCopied?(line)
    }

    /// Returns the full line containing `characterIndex`, or nil when empty.
    static func lineString(at characterIndex: Int, in string: String) -> String? {
        guard !string.isEmpty else { return nil }
        let ns = string as NSString
        let clamped = max(0, min(characterIndex, ns.length - 1))
        var lineStart = 0, lineEnd = 0, contentsEnd = 0
        ns.getLineStart(&lineStart, end: &lineEnd, contentsEnd: &contentsEnd, for: NSRange(location: clamped, length: 0))
        let line = ns.substring(with: NSRange(location: lineStart, length: contentsEnd - lineStart))
        return line.isEmpty ? nil : line
    }
}

// MARK: - Attributed builders

enum ResultsTextBuilder {
    /// OCR tab: one continuous block so selection can span lines.
    static func ocrText(lines: [String]) -> NSAttributedString {
        NSAttributedString(
            string: lines.joined(separator: "\n"),
            attributes: [
                .font: NSFont.systemFont(ofSize: 13),
                .foregroundColor: NSColor.labelColor
            ]
        )
    }

    /// Translation tab: 原文/译文对照 in one block (styles preserved).
    static func translationText(segments: [BilingualSegment]) -> NSAttributedString {
        let result = NSMutableAttributedString()
        let bodyAttrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 13),
            .foregroundColor: NSColor.labelColor
        ]
        let captionAttrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 11),
            .foregroundColor: NSColor.secondaryLabelColor
        ]
        for (index, segment) in segments.enumerated() {
            if index > 0 {
                result.append(NSAttributedString(string: "\n", attributes: bodyAttrs))
            }
            result.append(NSAttributedString(string: segment.original.text, attributes: captionAttrs))
            result.append(NSAttributedString(string: "\n", attributes: captionAttrs))
            result.append(NSAttributedString(string: segment.translated, attributes: bodyAttrs))
        }
        return result
    }
}
