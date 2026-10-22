import SwiftUI
import AppKit

struct PreviewAnnotatedImageView: View {
    @Bindable var viewModel: PreviewViewModel
    @Binding var imageDisplaySize: CGSize
    @Binding var imageScale: CGFloat
    @FocusState.Binding var isTextFieldFocused: Bool

    @State private var hoverImageLocation: CGPoint?
    @State private var didBeginGesture: Bool = false
    @State private var lastTapTime: Date = .distantPast
    @State private var lastTappedAnnotationIndex: Int?

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
                editingIndex: viewModel.isWaitingForTextInput ? viewModel.editingAnnotationIndex : nil,
                sourceImage: viewModel.image
            )
            .frame(width: imageSize.width, height: imageSize.height)

            if viewModel.selectedTool != nil {
                drawingGestureOverlay
            }

            if viewModel.selectedTool == nil && !viewModel.isCropMode {
                selectionGestureOverlay
            }

            if viewModel.isWaitingForTextInput,
               let inputPosition = viewModel.textInputPosition {
                textInputField(at: inputPosition)
                    .zIndex(10)
            }

            if viewModel.isCropMode {
                PreviewCropOverlay(viewModel: viewModel, displaySize: imageSize, scale: 1.0 / displayScaleFactor)
            }
        }
        .overlay(alignment: .topLeading) {
            if let tool = viewModel.selectedTool {
                activeToolIndicator(tool: tool)
                    .padding(8)
                    .allowsHitTesting(false)
            } else if viewModel.isCropMode {
                cropModeIndicator
                    .padding(8)
                    .allowsHitTesting(false)
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
        .onContinuousHover { phase in
            switch phase {
            case .active(let location):
                hoverImageLocation = convertToImageCoordinates(location)
            case .ended:
                hoverImageLocation = nil
            }
        }
        .contentShape(Rectangle())
        .cursor(cursorForCurrentTool)
    }

    private var cursorForCurrentTool: NSCursor {
        if viewModel.isCropMode {
            return .crosshair
        }

        if let activeHandle = viewModel.activeResizeHandle {
            return activeHandle.cursor
        }

        if viewModel.isDraggingAnnotation {
            return .closedHand
        }

        if let hoverPt = hoverImageLocation {
            if let handle = viewModel.hitTestSelectedAnnotationHandle(at: hoverPt) {
                return handle.cursor
            }

            if let tool = viewModel.selectedTool {
                if (tool == .text || tool == .calloutBubble),
                   let hitIdx = viewModel.hitTest(at: hoverPt) {
                    switch viewModel.annotations[hitIdx] {
                    case .text, .calloutBubble:
                        return .iBeam
                    default:
                        break
                    }
                }
                if let selIdx = viewModel.selectedAnnotationIndex,
                   viewModel.hitTest(at: hoverPt) == selIdx {
                    return .openHand
                }
                return tool.cursor
            } else {
                if let hitIdx = viewModel.hitTest(at: hoverPt) {
                    if case .text = viewModel.annotations[hitIdx],
                       viewModel.selectedAnnotationIndex == hitIdx {
                        return .openHand
                    }
                    return .openHand
                }
                return .arrow
            }
        }

        if let tool = viewModel.selectedTool {
            return tool.cursor
        }

        return .arrow
    }

    private var drawingGestureOverlay: some View {
        Color.clear
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { value in
                        let startPoint = convertToImageCoordinates(value.startLocation)
                        let currentPoint = convertToImageCoordinates(value.location)

                        if !didBeginGesture {
                            didBeginGesture = true
                            // Commit any active text input if user clicks elsewhere on canvas
                            if viewModel.isWaitingForTextInput {
                                viewModel.commitTextInput()
                                isTextFieldFocused = false
                                // If the user was just finishing text input by clicking canvas, don't immediately spawn a new box at that click unless they drag or click again
                                if viewModel.selectedTool == .text {
                                    return
                                }
                            }
                            if let handle = viewModel.hitTestSelectedAnnotationHandle(at: startPoint) {
                                viewModel.beginResizingAnnotation(handle: handle, at: startPoint)
                                return
                            }
                            // Clicking an existing annotation selects and drags it;
                            // double click re-edits text/callout content in place
                            if let hitIdx = viewModel.hitTest(at: startPoint) {
                                let now = Date()
                                let isDoubleTap = now.timeIntervalSince(lastTapTime) < 0.35
                                    && lastTappedAnnotationIndex == hitIdx
                                lastTapTime = now
                                lastTappedAnnotationIndex = hitIdx
                                if isDoubleTap {
                                    switch viewModel.annotations[hitIdx] {
                                    case .text, .calloutBubble:
                                        viewModel.beginEditingTextAnnotation(at: hitIdx)
                                        return
                                    default:
                                        break
                                    }
                                }
                                viewModel.selectedAnnotationIndex = hitIdx
                                viewModel.beginDraggingAnnotation(at: startPoint)
                                return
                            }
                            lastTappedAnnotationIndex = nil
                            viewModel.beginDrawing(at: startPoint)
                        } else {
                            if viewModel.isResizingAnnotation {
                                viewModel.continueResizingAnnotation(to: currentPoint)
                            } else if !viewModel.isWaitingForTextInput {
                                viewModel.continueDrawing(to: currentPoint)
                            }
                        }
                    }
                    .onEnded { value in
                        defer { didBeginGesture = false }
                        let endPoint = convertToImageCoordinates(value.location)
                        if viewModel.isResizingAnnotation {
                            viewModel.endResizingAnnotation()
                        } else if !viewModel.isWaitingForTextInput {
                            viewModel.endDrawing(at: endPoint)
                        }
                    }
            )
    }

    private var selectionGestureOverlay: some View {
        Color.clear
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { value in
                        let startPoint = convertToImageCoordinates(value.startLocation)
                        let currentPoint = convertToImageCoordinates(value.location)

                        if !didBeginGesture {
                            didBeginGesture = true

                            if viewModel.isWaitingForTextInput {
                                viewModel.commitTextInput()
                                isTextFieldFocused = false
                            }

                            // 1. Check if clicking a resize/control handle on currently selected annotation
                            if let handle = viewModel.hitTestSelectedAnnotationHandle(at: startPoint) {
                                viewModel.beginResizingAnnotation(handle: handle, at: startPoint)
                                return
                            }

                            // 2. Check if clicking an existing annotation
                            if let hitIndex = viewModel.hitTest(at: startPoint) {
                                let now = Date()
                                if now.timeIntervalSince(lastTapTime) < 0.35 && lastTappedAnnotationIndex == hitIndex {
                                    switch viewModel.annotations[hitIndex] {
                                    case .text, .calloutBubble:
                                        lastTappedAnnotationIndex = nil
                                        viewModel.beginEditingTextAnnotation(at: hitIndex)
                                        return
                                    default:
                                        break
                                    }
                                }
                                lastTapTime = now
                                lastTappedAnnotationIndex = hitIndex
                                viewModel.selectAnnotation(at: hitIndex)
                                viewModel.beginDraggingAnnotation(at: startPoint)
                            } else {
                                lastTappedAnnotationIndex = nil
                                viewModel.deselectAnnotation()
                            }
                        } else {
                            if viewModel.isResizingAnnotation {
                                viewModel.continueResizingAnnotation(to: currentPoint)
                            } else if viewModel.isDraggingAnnotation {
                                viewModel.continueDraggingAnnotation(to: currentPoint)
                            }
                        }
                    }
                    .onEnded { _ in
                        defer { didBeginGesture = false }
                        if viewModel.isResizingAnnotation {
                            viewModel.endResizingAnnotation()
                        } else if viewModel.isDraggingAnnotation {
                            viewModel.endDraggingAnnotation()
                        }
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

        let (fontSizeInImageCoords, textColor): (CGFloat, Color) = {
            if let editIdx = viewModel.editingAnnotationIndex,
               editIdx >= 0,
               editIdx < viewModel.annotations.count {
                switch viewModel.annotations[editIdx] {
                case .text(let t):
                    return (t.style.fontSize, t.style.color.color)
                case .calloutBubble(let cb):
                    return (cb.style.fontSize, cb.style.color.color)
                default:
                    break
                }
            }
            return (AppSettings.shared.textSize, AppSettings.shared.strokeColor.color)
        }()

        let displayFontSize = max(10, fontSizeInImageCoords / displayScaleFactor)

        // Size the editor from the current content so multi-line input is fully visible
        let content = viewModel.textInputContent
        let lines = content.split(separator: "\n", omittingEmptySubsequences: false)
        let lineCount = max(1, lines.count)
        let measureFont = NSFont.systemFont(ofSize: displayFontSize)
        let longestLineWidth = lines.map { ($0 as NSString).size(withAttributes: [.font: measureFont]).width }.max() ?? 0
        let maxEditorWidth = max(80, imageSize.width - displayPosition.x)
        let editorWidth = min(longestLineWidth + 24, maxEditorWidth)
        let editorHeight = min(
            CGFloat(lineCount) * max(displayFontSize * 1.45, 20) + 10,
            max(30, imageSize.height - displayPosition.y)
        )

        return TextEditor(text: $viewModel.textInputContent)
            .font(.system(size: displayFontSize))
            .foregroundColor(textColor)
            .scrollContentBackground(.hidden)
            .padding(.horizontal, 4)
            .padding(.vertical, 2)
            .background(
                RoundedRectangle(cornerRadius: 4)
                    .fill(Color(nsColor: .textBackgroundColor).opacity(0.85))
                    .overlay(
                        RoundedRectangle(cornerRadius: 4)
                            .stroke(Color.accentColor, lineWidth: 1.5)
                    )
            )
            .frame(width: editorWidth, height: editorHeight, alignment: .topLeading)
            .offset(
                x: max(0, min(imageSize.width - 60, displayPosition.x - 4)),
                y: max(0, min(imageSize.height - displayFontSize - 6, displayPosition.y - 2))
            )
            .focused($isTextFieldFocused)
            .onAppear {
                DispatchQueue.main.async {
                    isTextFieldFocused = true
                }
            }
            .onChange(of: isTextFieldFocused) { _, focused in
                if !focused && viewModel.isWaitingForTextInput {
                    viewModel.commitTextInput()
                }
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
