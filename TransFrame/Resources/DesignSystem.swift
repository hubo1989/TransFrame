import SwiftUI

/// TransFrame UI Design System Tokens & Helpers
enum DesignSystem {
    enum Radii {
        /// Small controls, tag chips, mini buttons (4pt)
        static let small: CGFloat = 4
        /// Standard controls, buttons, text fields (8pt)
        static let medium: CGFloat = 8
        /// Cards, popovers, grouped panels (12pt)
        static let large: CGFloat = 12

        // Legacy name aliases — values intentionally reduced to the new token scale
        // (control 8→medium, card 12→large, window 16→explicit)
        static let control: CGFloat = medium
        static let card: CGFloat = large
        static let window: CGFloat = 16
    }
}

extension View {
    /// Standard macOS grouped card container
    func macosGroupedCard(cornerRadius: CGFloat = DesignSystem.Radii.card) -> some View {
        self
            .padding(16)
            .background(
                RoundedRectangle(cornerRadius: cornerRadius)
                    .fill(Color(.controlBackgroundColor))
            )
            .overlay(
                RoundedRectangle(cornerRadius: cornerRadius)
                    .stroke(Color(.separatorColor).opacity(0.5), lineWidth: 0.5)
            )
    }
}

/// Bridge to NSVisualEffectView for SwiftUI
struct VisualEffectView: NSViewRepresentable {
    let material: NSVisualEffectView.Material
    let blendingMode: NSVisualEffectView.BlendingMode

    init(
        material: NSVisualEffectView.Material = .headerView,
        blendingMode: NSVisualEffectView.BlendingMode = .behindWindow
    ) {
        self.material = material
        self.blendingMode = blendingMode
    }

    func makeNSView(context: Context) -> NSVisualEffectView {
        let view = NSVisualEffectView()
        view.material = material
        view.blendingMode = blendingMode
        view.state = .active
        return view
    }

    func updateNSView(_ nsView: NSVisualEffectView, context: Context) {
        nsView.material = material
        nsView.blendingMode = blendingMode
    }
}
