import SwiftUI
import AppKit

struct PreviewAnnotatedImageView: View {
    @Bindable var viewModel: PreviewViewModel
    @Binding var imageDisplaySize: CGSize
    @Binding var imageScale: CGFloat
    @FocusState.Binding var isTextFieldFocused: Bool

    private var displayScaleFactor: CGFloat {
        viewModel.screenshot.sourceDisplay.scaleFactor
    }

    private var imageSize: CGSize {
        CGSize(
            width: CGFloat(viewModel.image.width) / displayScaleFactor,
            height: CGFloat(viewModel.image.height) / displayScaleFactor
        )
    }

    var body: some View {
        ZStack(alignment: .topLeading) {
            Image(viewModel.image, scale: displayScaleFactor, label: Text("preview.screenshot"))
                .accessibilityLabel(Text(
                    "Screenshot preview, \(viewModel.dimensionsText), from \(viewModel.displayName)"
                ))

            AnnotationCanvas(
                annotations: viewModel.annotations,
                currentAnnotation: viewModel.currentAnnotation,
                canvasSize: imageSize,
                scale: 1.0 / displayScaleFactor,
                selectedIndex: viewModel.selectedAnnotationIndex,
                sourceImage: viewModel.image
            )
            .frame(width: imageSize.width, height: imageSize.height)

            if viewModel.isWaitingForTextInput,
               let inputPosition = viewModel.textInputPosition {
                textInputField(at: inputPosition)
            }

            if viewModel.selectedTool != nil {
                drawingGestureOverlay
            }

            if viewModel.selectedTool == nil && !viewModel.isCropMode {
                selectionGestureOverlay
            }

            if viewModel.isCropMode {
                PreviewCropOverlay(viewModel: viewModel, displaySize: imageSize, scale: 1.0 / displayScaleFactor)
            }
        }
        .overlay(alignment: .topLeading) {
            if let tool = viewModel.selectedTool {
                activeToolIndicator(tool: tool)
                    .padding(8)
            } else if viewModel.isCropMode {
                cropModeIndicator
                    .padding(8)
            }
        }
        .overlay(alignment: .bottom) {
            if viewModel.cropRect != nil && !viewModel.isCropSelecting {
                cropActionButtons
                    .padding(12)
            }
        }
        .frame(width: imageSize.width, height: imageSize.height)
        .onAppear {
            imageDisplaySize = imageSize
            imageScale = 1.0
        }
        .contentShape(Rectangle())
        .cursor(cursorForCurrentTool)
    }

    private var cursorForCurrentTool: NSCursor {
        if viewModel.isCropMode {
            return .crosshair
        }

        guard let tool = viewModel.selectedTool else {
            if viewModel.isDraggingAnnotation {
                return .closedHand
            } else if viewModel.selectedAnnotationIndex != nil {
                return .openHand
            }
            return .arrow
        }

        switch tool {
        case .rectangle, .ellipse, .line, .freehand, .arrow, .highlight, .mosaic, .numberLabel, .privacyRedaction, .calloutBubble:
            return .crosshair
        case .text:
            return .iBeam
        }
    }

    private var drawingGestureOverlay: some View {
        Color.clear
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { value in
                        let point = convertToImageCoordinates(value.location)
                        if value.translation == .zero {
                            viewModel.beginDrawing(at: point)
                        } else {
                            viewModel.continueDrawing(to: point)
                        }
                    }
                    .onEnded { value in
                        let point = convertToImageCoordinates(value.location)
                        viewModel.endDrawing(at: point)
                    }
            )
    }

    private var selectionGestureOverlay: some View {
        Color.clear
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { value in
                        let point = convertToImageCoordinates(value.location)
                        if value.translation == .zero {
                            if let hitIndex = viewModel.hitTest(at: point) {
                                viewModel.selectAnnotation(at: hitIndex)
                                viewModel.beginDraggingAnnotation(at: point)
                            } else {
                                viewModel.deselectAnnotation()
                            }
                        } else if viewModel.isDraggingAnnotation {
                            viewModel.continueDraggingAnnotation(to: point)
                        }
                    }
                    .onEnded { _ in
                        viewModel.endDraggingAnnotation()
                    }
            )
    }

    private func convertToImageCoordinates(_ point: CGPoint) -> CGPoint {
        CGPoint(x: point.x * displayScaleFactor, y: point.y * displayScaleFactor)
    }

    private func textInputField(at position: CGPoint) -> some View {
        let displayPosition = CGPoint(
            x: position.x / displayScaleFactor,
            y: position.y / displayScaleFactor
        )
        return TextField(String(localized: "preview.enter.text"), text: $viewModel.textInputContent)
            .textFieldStyle(.plain)
            .font(.system(size: 14))
            .foregroundColor(AppSettings.shared.strokeColor.color)
            .padding(4)
            .background(Color.white.opacity(0.9))
            .cornerRadius(4)
            .frame(minWidth: 100, maxWidth: 300)
            .position(x: displayPosition.x + 50, y: displayPosition.y + 10)
            .focused($isTextFieldFocused)
            .onAppear {
                isTextFieldFocused = true
            }
            .onSubmit {
                viewModel.commitTextInput()
                isTextFieldFocused = false
            }
            .onExitCommand {
                viewModel.cancelCurrentDrawing()
                isTextFieldFocused = false
            }
    }

    private func activeToolIndicator(tool: AnnotationToolType) -> some View {
        HStack(spacing: 4) {
            Image(systemName: tool.systemImage)
            Text(tool.displayName)
                .font(.caption)
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
        .background(.ultraThinMaterial)
        .cornerRadius(6)
        .foregroundStyle(.secondary)
        .accessibilityElement(children: .combine)
        .accessibilityLabel(Text("\(String(localized: "preview.active.tool")): \(tool.displayName)"))
    }

    private var cropModeIndicator: some View {
        HStack(spacing: 4) {
            Image(systemName: "crop")
            Text("preview.crop")
                .font(.caption)
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
        .background(.ultraThinMaterial)
        .cornerRadius(6)
        .foregroundStyle(.secondary)
        .accessibilityElement(children: .combine)
        .accessibilityLabel(Text("preview.crop.mode.active"))
    }

    private var cropActionButtons: some View {
        HStack(spacing: 12) {
            Button {
                viewModel.cancelCrop()
            } label: {
                Label(String(localized: "action.cancel"), systemImage: "xmark")
            }
            .buttonStyle(.bordered)
            .keyboardShortcut(.escape, modifiers: [])

            Button {
                viewModel.applyCrop()
            } label: {
                Label(String(localized: "preview.crop.apply"), systemImage: "checkmark")
            }
            .buttonStyle(.borderedProminent)
            .keyboardShortcut(.return, modifiers: [])
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
        .background(.ultraThinMaterial)
        .cornerRadius(10)
    }
}

struct PreviewCropOverlay: View {
    @Bindable var viewModel: PreviewViewModel
    let displaySize: CGSize
    let scale: CGFloat

    var body: some View {
        ZStack {
            if let cropRect = viewModel.cropRect, cropRect.width > 0, cropRect.height > 0 {
                let scaledRect = CGRect(
                    x: cropRect.origin.x * scale,
                    y: cropRect.origin.y * scale,
                    width: cropRect.width * scale,
                    height: cropRect.height * scale
                )

                CropDimOverlay(cropRect: scaledRect)
                    .fill(Color.black.opacity(0.5))
                    .allowsHitTesting(false)
                    .transaction { $0.animation = nil }

                Rectangle()
                    .stroke(Color.white, lineWidth: 2)
                    .frame(width: scaledRect.width, height: scaledRect.height)
                    .position(x: scaledRect.midX, y: scaledRect.midY)
                    .allowsHitTesting(false)

                ForEach(0..<4, id: \.self) { corner in
                    let position = cornerPosition(for: corner, in: scaledRect)
                    RoundedRectangle(cornerRadius: 2)
                        .fill(Color.white)
                        .frame(width: 10, height: 10)
                        .position(position)
                        .allowsHitTesting(false)
                }

                cropDimensionsLabel(for: cropRect, scaledRect: scaledRect)
            }

            Color.clear
                .contentShape(Rectangle())
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { value in
                            let point = CGPoint(x: value.location.x / scale, y: value.location.y / scale)
                            if value.translation == .zero {
                                viewModel.beginCropSelection(at: point)
                            } else {
                                viewModel.continueCropSelection(to: point)
                            }
                        }
                        .onEnded { value in
                            let point = CGPoint(x: value.location.x / scale, y: value.location.y / scale)
                            viewModel.endCropSelection(at: point)
                        }
                )
        }
    }

    private func cornerPosition(for corner: Int, in rect: CGRect) -> CGPoint {
        switch corner {
        case 0: return CGPoint(x: rect.minX, y: rect.minY)
        case 1: return CGPoint(x: rect.maxX, y: rect.minY)
        case 2: return CGPoint(x: rect.minX, y: rect.maxY)
        case 3: return CGPoint(x: rect.maxX, y: rect.maxY)
        default: return .zero
        }
    }

    private func cropDimensionsLabel(for cropRect: CGRect, scaledRect: CGRect) -> some View {
        let width = Int(cropRect.width)
        let height = Int(cropRect.height)

        return Text("\(width) × \(height)")
            .font(.system(size: 12, weight: .medium, design: .monospaced))
            .foregroundColor(.white)
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(Color.black.opacity(0.75))
            .cornerRadius(4)
            .position(
                x: scaledRect.midX,
                y: max(scaledRect.minY - 20, 15)
            )
            .allowsHitTesting(false)
    }
}
