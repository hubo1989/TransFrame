import SwiftUI
import AppKit

struct PreviewToolBar: View {
    @Bindable var viewModel: PreviewViewModel

    var body: some View {
        HStack(spacing: 4) {
            ForEach(AnnotationToolType.allCases) { tool in
                toolButton(for: tool)
            }

            if viewModel.selectedTool != nil || viewModel.selectedAnnotationIndex != nil {
                Divider()
                    .frame(height: 16)

                PreviewStyleCustomizationBar(viewModel: viewModel)
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel(Text("Annotation tools"))
    }

    private func toolButton(for tool: AnnotationToolType) -> some View {
        let isSelected = viewModel.selectedTool == tool
        return Button {
            if isSelected {
                viewModel.selectTool(nil)
            } else {
                viewModel.selectTool(tool)
            }
        } label: {
            Image(systemName: tool.systemImage)
                .frame(width: 24, height: 24)
        }
        .buttonStyle(.accessoryBar)
        .background(
            isSelected
                ? Color.accentColor.opacity(0.2)
                : Color.clear
        )
        .clipShape(RoundedRectangle(cornerRadius: 4))
        .contentShape(Rectangle().inset(by: -2))
        .customTooltip("\(tool.displayName) (\(String(tool.keyboardShortcut).uppercased()))")
        .accessibilityLabel(Text(tool.displayName))
        .accessibilityHint(Text("Press \(String(tool.keyboardShortcut).uppercased()) to toggle"))
        .accessibilityAddTraits(isSelected ? [.isSelected] : [])
    }
}

struct PreviewStyleCustomizationBar: View {
    @Bindable var viewModel: PreviewViewModel

    private var isEditingAnnotation: Bool {
        viewModel.selectedAnnotationIndex != nil
    }

    private var effectiveToolType: AnnotationToolType? {
        isEditingAnnotation ? viewModel.selectedAnnotationType : viewModel.selectedTool
    }

    private var presetColors: [Color] {
        [.red, .yellow, .green, .blue, .black]
    }

    private var isShapeTool: Bool {
        effectiveToolType == .rectangle || effectiveToolType == .ellipse
    }

    private var isShapeFilled: Bool {
        isEditingAnnotation
            ? (viewModel.selectedAnnotationIsFilled ?? false)
            : (effectiveToolType == .ellipse ? AppSettings.shared.ellipseFilled : AppSettings.shared.rectangleFilled)
    }

    var body: some View {
        HStack(spacing: 8) {
            if isEditingAnnotation {
                Text("preview.edit.label")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            if effectiveToolType != .mosaic {
                colorRow(for: .stroke)
                Divider()
                    .frame(height: 16)
            }

            if isShapeTool {
                shapeFillToggle
                Divider()
                    .frame(height: 16)
            }

            if isShapeTool && isShapeFilled {
                colorRow(for: .fill)
                Divider()
                    .frame(height: 16)
            }

            if shouldShowStrokeWidth {
                strokeWidthControl
            }

            if effectiveToolType == .text || effectiveToolType == .calloutBubble {
                textSizeControl
            }

            if effectiveToolType == .mosaic {
                blockSizeControl
            }

            if isEditingAnnotation {
                if effectiveToolType == .text || effectiveToolType == .calloutBubble {
                    Divider()
                        .frame(height: 16)
                    editTextButton
                }

                Divider()
                    .frame(height: 16)

                deleteButton
            }
        }
    }

    private var shouldShowStrokeWidth: Bool {
        if effectiveToolType == .freehand || effectiveToolType == .arrow || effectiveToolType == .line {
            return true
        }

        if isShapeTool {
            return !isShapeFilled
        }

        return false
    }

    // MARK: - Color Rows

    /// Which annotation color a swatch/picker applies to
    private enum ColorTarget {
        case stroke
        case fill
    }

    private func colorRow(for target: ColorTarget) -> some View {
        HStack(spacing: 2) {
            if target == .fill {
                Image(systemName: "paintpalette")
                    .font(.system(size: 9))
                    .foregroundStyle(.secondary)
            }

            ForEach(presetColors, id: \.self) { color in
                colorButton(for: color, target: target)
            }

            ColorPicker(
                "",
                selection: Binding(
                    get: { currentColor(for: target)?.color ?? .red },
                    set: { newColor in
                        applyColor(CodableColor(newColor), to: target)
                    }
                ),
                supportsOpacity: target == .fill
            )
            .labelsHidden()
            .frame(width: 24)
            .customTooltip(String(localized: "preview.color.custom"))

            eyedropperButton(target: target)
        }
    }

    private func currentColor(for target: ColorTarget) -> CodableColor? {
        if isEditingAnnotation {
            switch target {
            case .stroke:
                return viewModel.selectedAnnotationColor
            case .fill:
                return viewModel.selectedAnnotationFillColor
            }
        }
        return target == .stroke ? AppSettings.shared.strokeColor : AppSettings.shared.shapeFillColor
    }

    private func applyColor(_ color: CodableColor, to target: ColorTarget) {
        if isEditingAnnotation {
            switch target {
            case .stroke:
                viewModel.updateSelectedAnnotationColor(color)
            case .fill:
                viewModel.updateSelectedAnnotationFillColor(color)
            }
        } else {
            switch target {
            case .stroke:
                AppSettings.shared.strokeColor = color
            case .fill:
                AppSettings.shared.shapeFillColor = color
            }
        }
    }

    private func colorButton(for color: Color, target: ColorTarget) -> some View {
        Button {
            applyColor(CodableColor(color), to: target)
        } label: {
            Circle()
                .fill(color)
                .frame(width: 16, height: 16)
                .overlay {
                    let active = currentColor(for: target)?.color ?? .clear
                    if colorsAreEqual(active, color) {
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
        .customTooltip(target == .fill ? "\(colorName(for: color)) (\(String(localized: "preview.fill.color")))" : colorName(for: color))
    }

    /// Screen eyedropper: samples a pixel anywhere on screen into the target color
    private func eyedropperButton(target: ColorTarget) -> some View {
        Button {
            NSColorSampler().show { selectedColor in
                guard let nsColor = selectedColor else { return }
                applyColor(CodableColor(Color(nsColor: nsColor)), to: target)
            }
        } label: {
            Image(systemName: "eyedropper")
                .font(.system(size: 11))
                .frame(width: 20, height: 20)
        }
        .buttonStyle(.plain)
        .contentShape(Rectangle().inset(by: -2))
        .customTooltip(String(localized: "preview.color.eyedropper"))
    }

    private var shapeFillToggle: some View {
        let isFilled = isEditingAnnotation
            ? (viewModel.selectedAnnotationIsFilled ?? false)
            : (effectiveToolType == .ellipse ? AppSettings.shared.ellipseFilled : AppSettings.shared.rectangleFilled)

        return Button {
            if isEditingAnnotation {
                viewModel.updateSelectedAnnotationFilled(!isFilled)
            } else {
                if effectiveToolType == .ellipse {
                    AppSettings.shared.ellipseFilled.toggle()
                } else {
                    AppSettings.shared.rectangleFilled.toggle()
                }
            }
        } label: {
            Image(systemName: isFilled ? getFillIconName() : getHollowIconName())
                .frame(width: 24, height: 24)
        }
        .buttonStyle(.accessoryBar)
        .background(
            isFilled
                ? Color.accentColor.opacity(0.2)
                : Color.clear
        )
        .clipShape(RoundedRectangle(cornerRadius: 4))
        .customTooltip(isFilled ? String(localized: "preview.shape.filled") : String(localized: "preview.shape.hollow"))
    }
    
    private func getFillIconName() -> String {
        effectiveToolType == .ellipse ? "circle.fill" : "rectangle.fill"
    }
    
    private func getHollowIconName() -> String {
        effectiveToolType == .ellipse ? "circle" : "rectangle"
    }

    private var strokeWidthControl: some View {
        HStack(spacing: 4) {
            Image(systemName: "lineweight")
                .font(.caption)
                .foregroundStyle(.secondary)

            Slider(
                value: Binding(
                    get: {
                        if isEditingAnnotation {
                            return viewModel.selectedAnnotationStrokeWidth ?? 3.0
                        }
                        return AppSettings.shared.strokeWidth
                    },
                    set: { newWidth in
                        if isEditingAnnotation {
                            viewModel.updateSelectedAnnotationStrokeWidth(newWidth)
                        } else {
                            AppSettings.shared.strokeWidth = newWidth
                        }
                    }
                ),
                in: 1.0...20.0,
                step: 0.5
            )
            .frame(width: 80)
            .customTooltip(String(localized: "settings.stroke.width"))

            let width = isEditingAnnotation
                ? Int(viewModel.selectedAnnotationStrokeWidth ?? 3)
                : Int(AppSettings.shared.strokeWidth)
            Text("\(width)")
                .font(.caption)
                .foregroundStyle(.secondary)
                .frame(width: 20)
        }
    }

    private var textSizeControl: some View {
        HStack(spacing: 4) {
            Image(systemName: "textformat.size")
                .font(.caption)
                .foregroundStyle(.secondary)

            Slider(
                value: Binding(
                    get: {
                        if isEditingAnnotation {
                            return viewModel.selectedAnnotationFontSize ?? 16.0
                        }
                        return AppSettings.shared.textSize
                    },
                    set: { newSize in
                        if isEditingAnnotation {
                            viewModel.updateSelectedAnnotationFontSize(newSize)
                        } else {
                            AppSettings.shared.textSize = newSize
                        }
                    }
                ),
                in: 8.0...72.0,
                step: 1
            )
            .frame(width: 80)
            .customTooltip(String(localized: "settings.text.size"))

            let size = isEditingAnnotation
                ? Int(viewModel.selectedAnnotationFontSize ?? 16)
                : Int(AppSettings.shared.textSize)
            Text("\(size)")
                .font(.caption)
                .foregroundStyle(.secondary)
                .frame(width: 20)
        }
    }

    private var blockSizeControl: some View {
        HStack(spacing: 4) {
            Image(systemName: "square.grid.3x3")
                .font(.caption)
                .foregroundStyle(.secondary)

            Slider(
                value: Binding(
                    get: {
                        if isEditingAnnotation {
                            return Double(viewModel.selectedAnnotationBlockSize ?? 10)
                        }
                        return Double(AppSettings.shared.mosaicBlockSize)
                    },
                    set: { newSize in
                        if isEditingAnnotation {
                            viewModel.updateSelectedAnnotationBlockSize(Int(newSize))
                        } else {
                            AppSettings.shared.mosaicBlockSize = CGFloat(newSize)
                        }
                    }
                ),
                in: 1.0...32.0,
                step: 1
            )
            .frame(width: 80)
            .customTooltip(String(localized: "settings.mosaic.blockSize"))

            let size = isEditingAnnotation
                ? Int(viewModel.selectedAnnotationBlockSize ?? 10)
                : Int(AppSettings.shared.mosaicBlockSize)
            Text("\(size)")
                .font(.caption)
                .foregroundStyle(.secondary)
                .frame(width: 20)
        }
    }

    private var editTextButton: some View {
        Button {
            viewModel.editSelectedText()
        } label: {
            Image(systemName: "character.cursor.ibeam")
                .frame(width: 20, height: 20)
        }
        .buttonStyle(.plain)
        .customTooltip(String(localized: "preview.tooltip.editText"))
    }

    private var deleteButton: some View {
        Button {
            viewModel.deleteSelectedAnnotation()
        } label: {
            Image(systemName: "trash")
                .foregroundStyle(.red)
        }
        .buttonStyle(.plain)
        .customTooltip(String(localized: "preview.tooltip.delete"))
    }

    private func colorsAreEqual(_ colorA: Color, _ colorB: Color) -> Bool {
        let nsColorA = NSColor(colorA).usingColorSpace(.deviceRGB)
        let nsColorB = NSColor(colorB).usingColorSpace(.deviceRGB)
        guard let convertedA = nsColorA, let convertedB = nsColorB else { return false }

        let tolerance: CGFloat = 0.01
        return abs(convertedA.redComponent - convertedB.redComponent) < tolerance &&
               abs(convertedA.greenComponent - convertedB.greenComponent) < tolerance &&
               abs(convertedA.blueComponent - convertedB.blueComponent) < tolerance
    }

    private func colorName(for color: Color) -> String {
        switch color {
        case .red: return String(localized: "color.red")
        case .orange: return String(localized: "color.orange")
        case .yellow: return String(localized: "color.yellow")
        case .green: return String(localized: "color.green")
        case .blue: return String(localized: "color.blue")
        case .purple: return String(localized: "color.purple")
        case .white: return String(localized: "color.white")
        case .black: return String(localized: "color.black")
        default: return String(localized: "color.custom")
        }
    }
}

// MARK: - Custom Tooltip for Floating Windows

enum TooltipPlacement {
    case top
    case bottom
}

extension View {
    /// Custom tooltip that works reliably in high-level borderless floating panels where native .help() may not display
    func customTooltip(_ text: String, placement: TooltipPlacement = .top) -> some View {
        self.modifier(CustomTooltipModifier(text: text, placement: placement))
    }
}

struct CustomTooltipModifier: ViewModifier {
    let text: String
    var placement: TooltipPlacement = .top
    @State private var isHovered = false
    @State private var showTooltip = false

    func body(content: Content) -> some View {
        content
            .onHover { hovering in
                isHovered = hovering
                if hovering {
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) {
                        if isHovered {
                            showTooltip = true
                        }
                    }
                } else {
                    showTooltip = false
                }
            }
            .overlay(alignment: placement == .top ? .top : .bottom) {
                if showTooltip {
                    Text(text)
                        .font(.system(size: 11, weight: .medium))
                        .padding(.horizontal, 6)
                        .padding(.vertical, 3)
                        .background(
                            RoundedRectangle(cornerRadius: 4)
                                .fill(Color(nsColor: .windowBackgroundColor))
                                .shadow(color: .black.opacity(0.25), radius: 3, x: 0, y: 1)
                        )
                        .overlay(
                            RoundedRectangle(cornerRadius: 4)
                                .stroke(Color.primary.opacity(0.15), lineWidth: 0.5)
                        )
                        .foregroundStyle(.primary)
                        .offset(y: placement == .top ? -28 : 28)
                        .transition(.opacity)
                        .fixedSize()
                        .allowsHitTesting(false)
                        .zIndex(999)
                }
            }
            .zIndex(showTooltip ? 100 : 0)
    }
}
