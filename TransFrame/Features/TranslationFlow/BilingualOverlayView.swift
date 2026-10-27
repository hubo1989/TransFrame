import SwiftUI
import AppKit

/// 在截图画布上显示译文，原文由底下的原始截图保留。
struct BilingualOverlayView: View {
    let segments: [BilingualSegment]

    var body: some View {
        Canvas { context, size in
            for segment in segments {
                guard let strip = BilingualOverlayLayout.translationStrip(for: segment, in: size) else { continue }
                let text = Text(segment.translated)
                    .font(.system(size: strip.fontSize, weight: .medium))
                    .foregroundStyle(.white)
                context.fill(
                    Path(roundedRect: strip.frame, cornerRadius: 3),
                    with: .color(.black.opacity(0.78))
                )
                context.draw(context.resolve(text), in: strip.textFrame)
            }
        }
        .allowsHitTesting(false)
        .accessibilityHidden(true)
    }
}

enum BilingualOverlayLayout {
    struct Anchor: Equatable {
        let x: CGFloat
        let y: CGFloat
        let maxWidth: CGFloat
        let fontSize: CGFloat
    }

    struct Strip {
        let frame: CGRect
        let fontSize: CGFloat

        var textFrame: CGRect { frame.insetBy(dx: 4, dy: 2) }
    }

    /// TextSegment 使用左上角原点；译文从原文下边缘开始，按画布宽度换行。
    static func translationAnchor(for boundingBox: CGRect, in displaySize: CGSize) -> Anchor {
        let x = max(0, min(boundingBox.minX * displaySize.width, displaySize.width))
        let belowY = boundingBox.maxY * displaySize.height + 1
        let stripWidth = min(max(boundingBox.width * displaySize.width, 60), max(0, displaySize.width - x))
        let fontSize = min(max(boundingBox.height * displaySize.height * 0.85, 10), 16)
        let estimatedStripHeight = fontSize * 1.6 + 4
        let y: CGFloat
        if belowY + estimatedStripHeight < displaySize.height {
            y = belowY
        } else {
            y = max(0, boundingBox.minY * displaySize.height - estimatedStripHeight - 1)
        }
        return Anchor(x: x, y: y, maxWidth: stripWidth, fontSize: fontSize)
    }

    /// AppKit 与 SwiftUI 共用实际文字测量，避免换行后被条带高度裁掉。
    static func translationStrip(for segment: BilingualSegment, in displaySize: CGSize) -> Strip? {
        let box = segment.boundingBox
        guard displaySize.width > 0, displaySize.height > 0,
              box.minX.isFinite, box.minY.isFinite, box.width.isFinite, box.height.isFinite,
              box.width > 0, box.height > 0,
              !segment.translated.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return nil }
        let anchor = translationAnchor(for: box, in: displaySize)
        guard anchor.maxWidth > 8, anchor.y < displaySize.height else { return nil }
        let measured = (segment.translated as NSString).boundingRect(
            with: CGSize(width: anchor.maxWidth - 8, height: CGFloat.greatestFiniteMagnitude),
            options: [.usesLineFragmentOrigin, .usesFontLeading],
            attributes: [.font: NSFont.systemFont(ofSize: anchor.fontSize, weight: .medium)]
        )
        return Strip(
            frame: CGRect(x: anchor.x, y: anchor.y, width: anchor.maxWidth, height: ceil(measured.height) + 4),
            fontSize: anchor.fontSize
        )
    }
}
