import SwiftUI
import AppKit

/// Selectable, read-only text block backed by NSTextView.
/// Supports continuous selection across lines (unlike per-row SwiftUI Text)
/// and copies a whole line to the pasteboard on a plain click (PRD 5.5).
struct SelectableTextBlock: NSViewRepresentable {
    let attributedText: NSAttributedString
    /// Called after a plain click copied one line; used for UI feedback.
    var onLineCopied: ((String) -> Void)? = nil

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    func makeNSView(context: Context) -> NSScrollView {
        Self.makeScrollView(
            textView: Self.makeConfiguredTextView(onLineCopied: onLineCopied)
        )
    }

    func updateNSView(_ scroll: NSScrollView, context: Context) {
        guard let view = scroll.documentView as? ResultsTextView else { return }
        view.onLineCopied = onLineCopied
        // Plain string compare avoids rebuilding an attributed copy every
        // SwiftUI update pass; content is fully replaced on change.
        if context.coordinator.lastAppliedString != attributedText.string {
            context.coordinator.lastAppliedString = attributedText.string
            view.textStorage?.setAttributedString(attributedText)
            // Fresh results invalidate any prior selection instead of
            // restoring a stale range over different text.
            view.setSelectedRange(NSRange(location: 0, length: 0))
            view.fitToViewport(width: scroll.contentView.bounds.width)
        }
    }

    final class Coordinator {
        var lastAppliedString: String?
    }

    /// Read-only selectable text view configured to wrap at the scroll
    /// view's width and grow vertically with its content, so long results
    /// stay scrollable instead of being clipped.
    static func makeConfiguredTextView(onLineCopied: ((String) -> Void)? = nil) -> ResultsTextView {
        let textView = ResultsTextView()
        textView.drawsBackground = false
        textView.isEditable = false
        textView.isSelectable = true
        textView.isRichText = true
        textView.allowsUndo = false
        textView.textContainerInset = .zero
        textView.textContainer?.lineFragmentPadding = 0
        textView.isVerticallyResizable = true
        textView.isHorizontallyResizable = false
        textView.textContainer?.widthTracksTextView = true
        textView.textContainer?.heightTracksTextView = false
        textView.textContainer?.containerSize = NSSize(width: 0, height: CGFloat.greatestFiniteMagnitude)
        textView.autoresizingMask = [.width]
        textView.onLineCopied = onLineCopied
        return textView
    }

    /// Native NSScrollView keeps drag/selection gestures in AppKit —
    /// SwiftUI's ScrollView would steal them before NSTextView sees them.
    static func makeScrollView(textView: ResultsTextView) -> NSScrollView {
        let scroll = NSScrollView()
        scroll.documentView = textView
        scroll.drawsBackground = false
        scroll.hasVerticalScroller = true
        scroll.hasHorizontalScroller = false
        scroll.autohidesScrollers = true
        scroll.borderType = .noBorder
        return scroll
    }
}

// MARK: - ResultsTextView

/// Read-only text view: drag selects across lines; a plain click copies the
/// clicked line to the pasteboard.
final class ResultsTextView: NSTextView {
    var onLineCopied: ((String) -> Void)?

    private var clickOrigin: NSPoint?
    private var pendingLineCopy: DispatchWorkItem?

    override func mouseDown(with event: NSEvent) {
        // A second/third click cancels the pending single-click line copy:
        // the word/paragraph selection must not clobber the pasteboard first.
        if event.clickCount >= 2 {
            pendingLineCopy?.cancel()
            pendingLineCopy = nil
        }
        clickOrigin = event.locationInWindow
        super.mouseDown(with: event)
    }

    override func mouseUp(with event: NSEvent) {
        super.mouseUp(with: event)
        defer { clickOrigin = nil }

        guard let origin = clickOrigin else { return }
        let travel = hypot(event.locationInWindow.x - origin.x, event.locationInWindow.y - origin.y)
        guard Self.shouldCopyLine(
            clickCount: event.clickCount,
            modifierFlags: event.modifierFlags,
            travel: travel
        ) else { return }

        let local = convert(event.locationInWindow, from: nil)
        let index = characterIndexForInsertion(at: local)
        guard let line = Self.lineString(at: index, in: string) else { return }

        // Defer past the double-click interval: only a true single click
        // copies the line.
        let work = DispatchWorkItem { [weak self] in
            guard let self else { return }
            NSPasteboard.general.clearContents()
            NSPasteboard.general.setString(line, forType: .string)
            self.onLineCopied?(line)
        }
        pendingLineCopy?.cancel()
        pendingLineCopy = work
        DispatchQueue.main.asyncAfter(
            deadline: .now() + NSEvent.doubleClickInterval,
            execute: work
        )
    }

    override func setFrameSize(_ newSize: NSSize) {
        let widthChanged = abs(newSize.width - frame.width) > 0.5
        super.setFrameSize(newSize)
        // Clip-view / SwiftUI resizes change the wrap width: re-measure the
        // content height so long results stay fully visible via scrolling.
        if widthChanged {
            fitToViewport(width: newSize.width)
        }
    }

    /// Whether this release should copy its line: a plain single click only —
    /// shift+click extends the selection, drags end in one, and a double
    /// click's first release is followed by word selection.
    static func shouldCopyLine(
        clickCount: Int,
        modifierFlags: NSEvent.ModifierFlags,
        travel: CGFloat
    ) -> Bool {
        clickCount == 1 && !modifierFlags.contains(.shift) && travel < 3
    }

    /// Wraps the container to `width` and sets the frame height from the laid
    /// out content, so the scroll view scrolls instead of clipping.
    func fitToViewport(width: CGFloat) {
        guard width > 0, let container = textContainer, let layoutManager else { return }
        if abs(frame.width - width) > 0.5 {
            super.setFrameSize(NSSize(width: width, height: frame.height))
        }
        container.containerSize = NSSize(width: width, height: CGFloat.greatestFiniteMagnitude)
        layoutManager.ensureLayout(for: container)
        let usedHeight = layoutManager.usedRect(for: container).height
        let height = ceil(usedHeight) + textContainerInset.height * 2 + 2
        super.setFrameSize(NSSize(width: width, height: max(height, 1)))
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
