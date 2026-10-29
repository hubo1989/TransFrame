import AppKit
import SwiftUI

struct AdvancedSettingsContent: View {
    @Bindable var viewModel: SettingsViewModel
    @State private var showResetConfirmation = false

    var body: some View {
        VStack(spacing: 20) {
            StrokeColorPicker(viewModel: viewModel)
            Divider().opacity(0.1)
            StrokeWidthSlider(viewModel: viewModel)
            Divider().opacity(0.1)
            TextSizeSlider(viewModel: viewModel)
        }
        .macos26LiquidGlass()

        Button(role: .destructive) {
            showResetConfirmation = true
        } label: {
            Text(localized("settings.reset.all"))
                .font(.system(.callout, design: .rounded, weight: .semibold))
                .frame(maxWidth: .infinity)
                .padding(.vertical, 14)
                .background(.red.opacity(0.1))
                .clipShape(RoundedRectangle(cornerRadius: DesignSystem.Radii.control))
        }
        .buttonStyle(.plain)
        .foregroundStyle(.red)
        .alert(
            localized("settings.reset.all"),
            isPresented: $showResetConfirmation
        ) {
            Button(localized("button.cancel"), role: .cancel) {}
            Button(localized("button.confirm"), role: .destructive) {
                viewModel.resetAllToDefaults()
            }
        }
    }
}

struct StrokeColorPicker: View {
    @Bindable var viewModel: SettingsViewModel

    var body: some View {
        HStack {
            Text(localized("settings.stroke.color"))

            Spacer()

            HStack(spacing: 4) {
                ForEach(SettingsViewModel.presetColors, id: \.self) { color in
                    Button {
                        viewModel.strokeColor = color
                    } label: {
                        Circle()
                            .fill(color)
                            .frame(width: 20, height: 20)
                            .overlay {
                                if colorsAreEqual(viewModel.strokeColor, color) {
                                    Circle()
                                        .stroke(Color.primary, lineWidth: 2)
                                }
                            }
                            .overlay {
                                if color == .white || color == .yellow {
                                    Circle()
                                        .stroke(Color.gray.opacity(0.3), lineWidth: 1)
                                }
                            }
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel(Text(colorName(for: color)))
                }
            }

            ColorPicker(localized("settings.stroke.color"), selection: $viewModel.strokeColor, supportsOpacity: false)
                .labelsHidden()
                .frame(width: 30)
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(Text(localized("settings.stroke.color")))
    }

    private func colorsAreEqual(_ firstColor: Color, _ secondColor: Color) -> Bool {
        let nsA = NSColor(firstColor).usingColorSpace(.deviceRGB)
        let nsB = NSColor(secondColor).usingColorSpace(.deviceRGB)
        guard let colorA = nsA, let colorB = nsB else { return false }

        let tolerance: CGFloat = 0.01
        return abs(colorA.redComponent - colorB.redComponent) < tolerance
            && abs(colorA.greenComponent - colorB.greenComponent) < tolerance
            && abs(colorA.blueComponent - colorB.blueComponent) < tolerance
    }

    private func colorName(for color: Color) -> String {
        switch color {
        case .red: return localized("color.red")
        case .orange: return localized("color.orange")
        case .yellow: return localized("color.yellow")
        case .green: return localized("color.green")
        case .blue: return localized("color.blue")
        case .purple: return localized("color.purple")
        case .pink: return localized("color.pink")
        case .white: return localized("color.white")
        case .black: return localized("color.black")
        default: return localized("color.custom")
        }
    }
}

struct StrokeWidthSlider: View {
    @Bindable var viewModel: SettingsViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(localized("settings.stroke.width"))
                Spacer()
                Text("\(viewModel.strokeWidth, specifier: "%.1f") pt")
                    .foregroundStyle(.secondary)
                    .monospacedDigit()
            }

            HStack(spacing: 12) {
                Slider(
                    value: $viewModel.strokeWidth,
                    in: SettingsViewModel.strokeWidthRange,
                    step: 0.5
                ) {
                    Text(localized("settings.stroke.width"))
                }
                .accessibilityValue(Text("\(viewModel.strokeWidth, specifier: "%.1f") points"))

                RoundedRectangle(cornerRadius: viewModel.strokeWidth / 2)
                    .fill(viewModel.strokeColor)
                    .frame(width: 40, height: viewModel.strokeWidth)
            }
        }
    }
}

struct TextSizeSlider: View {
    @Bindable var viewModel: SettingsViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(localized("settings.text.size"))
                Spacer()
                Text("\(Int(viewModel.textSize)) pt")
                    .foregroundStyle(.secondary)
                    .monospacedDigit()
            }

            HStack(spacing: 12) {
                Slider(
                    value: $viewModel.textSize,
                    in: SettingsViewModel.textSizeRange,
                    step: 1
                ) {
                    Text(localized("settings.text.size"))
                }
                .accessibilityValue(Text("\(Int(viewModel.textSize)) points"))

                Text("Aa")
                    .font(.system(size: min(viewModel.textSize, 24)))
                    .foregroundStyle(viewModel.strokeColor)
                    .frame(width: 40)
            }
        }
    }
}
