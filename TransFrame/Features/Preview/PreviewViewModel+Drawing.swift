import Foundation
import SwiftUI

// MARK: - Drawing & Annotation Methods

extension PreviewViewModel {
    // MARK: - Drawing Methods

    /// Begins a drawing gesture at the given point
    /// - Parameter point: The point in image coordinates
    func beginDrawing(at point: CGPoint) {
        if isWaitingForTextInputInternal {
            commitTextInput()
        }

        guard let selectedTool else { return }

        // If user clicks on an existing text or callout bubble while using text/callout tool, edit it in-place
        if selectedTool == .text || selectedTool == .calloutBubble,
           let hitIdx = hitTest(at: point) {
            switch annotations[hitIdx] {
            case .text, .calloutBubble:
                beginEditingTextAnnotation(at: hitIdx)
                return
            default:
                break
            }
        }

        let scaleFactor = screenshot.sourceDisplay.scaleFactor

        // Apply current stroke/text styles from settings
        let strokeStyle = StrokeStyle(
            color: settings.strokeColor,
            lineWidth: settings.strokeWidth
        )
        let textStyle = TextStyle(
            color: settings.strokeColor,
            fontSize: settings.textSize,
            fontName: ".AppleSystemUIFont"
        )

        switch selectedTool {
        case .rectangle:
            rectangleTool.strokeStyle = strokeStyle
            rectangleTool.isFilled = settings.rectangleFilled
            rectangleTool.fillColor = settings.shapeFillColor
            rectangleTool.beginDrawing(at: point)
        case .ellipse:
            ellipseTool.strokeStyle = strokeStyle
            ellipseTool.isFilled = settings.ellipseFilled
            ellipseTool.fillColor = settings.shapeFillColor
            ellipseTool.beginDrawing(at: point)
        case .line:
            lineTool.strokeStyle = strokeStyle
            lineTool.beginDrawing(at: point)
        case .freehand:
            freehandTool.strokeStyle = strokeStyle
            freehandTool.beginDrawing(at: point)
        case .arrow:
            arrowTool.strokeStyle = strokeStyle
            arrowTool.beginDrawing(at: point)
        case .highlight:
            highlightTool.highlightColor = settings.strokeColor
            highlightTool.beginDrawing(at: point)
        case .mosaic:
            mosaicTool.blockSize = Int(settings.mosaicBlockSize)
            mosaicTool.beginDrawing(at: point)
        case .text:
            editingAnnotationIndex = nil
            textTool.textStyle = textStyle
            textTool.beginDrawing(at: point)
            isWaitingForTextInputInternal = true
            textInputPositionInternal = point
        case .numberLabel:
            numberLabelTool.labelColor = settings.strokeColor
            numberLabelTool.circleSize = 24 * scaleFactor
            numberLabelTool.beginDrawing(at: point)
        case .privacyRedaction:
            rectangleTool.strokeStyle = StrokeStyle(color: CodableColor(.black), lineWidth: strokeStyle.lineWidth)
            rectangleTool.isFilled = true
            rectangleTool.beginDrawing(at: point)
        case .calloutBubble:
            rectangleTool.strokeStyle = strokeStyle
            rectangleTool.isFilled = true
            rectangleTool.beginDrawing(at: point)
        }

        updateCurrentAnnotation()
    }

    /// Continues a drawing gesture to the given point
    /// - Parameter point: The point in image coordinates
    func continueDrawing(to point: CGPoint) {
        guard let selectedTool else { return }

        switch selectedTool {
        case .rectangle:
            rectangleTool.continueDrawing(to: point)
        case .ellipse:
            ellipseTool.continueDrawing(to: point)
        case .line:
            lineTool.continueDrawing(to: point)
        case .freehand:
            freehandTool.continueDrawing(to: point)
        case .arrow:
            arrowTool.continueDrawing(to: point)
        case .highlight:
            highlightTool.continueDrawing(to: point)
        case .mosaic:
            mosaicTool.continueDrawing(to: point)
        case .text:
            textTool.continueDrawing(to: point)
        case .numberLabel:
            numberLabelTool.continueDrawing(to: point)
        case .privacyRedaction, .calloutBubble:
            rectangleTool.continueDrawing(to: point)
        }

        updateCurrentAnnotation()
    }

    /// Ends a drawing gesture at the given point
    /// - Parameter point: The point in image coordinates
    func endDrawing(at point: CGPoint) {
        guard let selectedTool else { return }

        var annotation: Annotation?
        let scaleFactor = screenshot.sourceDisplay.scaleFactor

        switch selectedTool {
        case .rectangle:
            annotation = rectangleTool.endDrawing(at: point)
        case .ellipse:
            annotation = ellipseTool.endDrawing(at: point)
        case .line:
            annotation = lineTool.endDrawing(at: point)
        case .freehand:
            annotation = freehandTool.endDrawing(at: point)
        case .arrow:
            annotation = arrowTool.endDrawing(at: point)
        case .highlight:
            annotation = highlightTool.endDrawing(at: point)
        case .mosaic:
            annotation = mosaicTool.endDrawing(at: point)
        case .text:
            _ = textTool.endDrawing(at: point)
            updateCurrentAnnotation()
            return
        case .numberLabel:
            annotation = numberLabelTool.endDrawing(at: point)
        case .privacyRedaction:
            if let rectAnno = rectangleTool.endDrawing(at: point), case .rectangle(let r) = rectAnno {
                annotation = .privacyRedaction(PrivacyRedactionAnnotation(rect: r.rect, fillColor: CodableColor(.black)))
            }
        case .calloutBubble:
            let rawAnno = rectangleTool.endDrawing(at: point)
            let bubbleRect: CGRect
            if let rawAnno, case .rectangle(let r) = rawAnno, r.rect.width >= 30 * scaleFactor, r.rect.height >= 24 * scaleFactor {
                bubbleRect = r.rect
            } else {
                let defaultW = 120 * scaleFactor
                let defaultH = 48 * scaleFactor
                bubbleRect = CGRect(x: point.x, y: point.y, width: defaultW, height: defaultH)
            }
            let target = CGPoint(x: bubbleRect.midX, y: bubbleRect.maxY + 20 * scaleFactor)
            let textStyle = TextStyle(
                color: CodableColor(.black),
                fontSize: settings.textSize,
                fontName: ".AppleSystemUIFont"
            )
            let bubble = CalloutBubbleAnnotation(
                rect: bubbleRect,
                targetPoint: target,
                text: "Note",
                style: textStyle,
                bubbleColor: CodableColor(.white)
            )
            currentAnnotationInternal = nil
            drawingUpdateCounter += 1
            addAnnotation(.calloutBubble(bubble))
            let newIdx = annotations.count - 1
            beginEditingTextAnnotation(at: newIdx)
            return
        }

        currentAnnotationInternal = nil
        drawingUpdateCounter += 1

        if let annotation {
            addAnnotation(annotation)
        }
    }

    /// Cancels the current drawing or in-place text editing operation without losing existing annotations
    func cancelCurrentDrawing() {
        rectangleTool.cancelDrawing()
        ellipseTool.cancelDrawing()
        lineTool.cancelDrawing()
        freehandTool.cancelDrawing()
        arrowTool.cancelDrawing()
        highlightTool.cancelDrawing()
        mosaicTool.cancelDrawing()
        textTool.cancelDrawing()
        numberLabelTool.cancelDrawing()
        currentAnnotationInternal = nil
        isWaitingForTextInputInternal = false
        textInputPositionInternal = nil
        editingAnnotationIndex = nil
        drawingUpdateCounter += 1
    }

    /// Updates the cached current annotation to trigger view refresh
    func updateCurrentAnnotation() {
        currentAnnotationInternal = currentTool?.currentAnnotation
        drawingUpdateCounter += 1
    }

    /// Commits the current text input and adds or updates the annotation
    func commitTextInput() {
        guard isWaitingForTextInputInternal else { return }

        let trimmedText = textInputContent.trimmingCharacters(in: .whitespacesAndNewlines)

        // Reset observable text input state first to prevent re-entrancy
        let editIdx = editingAnnotationIndex
        let position = textInputPositionInternal
        textInputContent = ""
        isWaitingForTextInputInternal = false
        textInputPositionInternal = nil
        editingAnnotationIndex = nil
        textTool.cancelDrawing()

        // Case 1: Updating an existing text or calloutBubble annotation in-place (preserving Z-Order and Undo history)
        if let editIdx, editIdx >= 0, editIdx < annotations.count {
            switch annotations[editIdx] {
            case .text(var textAnno):
                if trimmedText.isEmpty {
                    removeAnnotation(at: editIdx)
                    if selectedAnnotationIndex == editIdx {
                        selectedAnnotationIndex = nil
                    }
                } else if textAnno.content != trimmedText {
                    pushUndoState()
                    textAnno.content = trimmedText
                    screenshot = screenshot.replacingAnnotation(at: editIdx, with: .text(textAnno))
                    redoStack.removeAll()
                }
            case .calloutBubble(var bubbleAnno):
                let finalText = trimmedText.isEmpty ? bubbleAnno.text : trimmedText
                if bubbleAnno.text != finalText {
                    pushUndoState()
                    bubbleAnno.text = finalText
                    screenshot = screenshot.replacingAnnotation(at: editIdx, with: .calloutBubble(bubbleAnno))
                    redoStack.removeAll()
                }
            default:
                break
            }
            drawingUpdateCounter += 1
            return
        }

        // Case 2: Creating a new text annotation
        guard let position, !trimmedText.isEmpty else {
            drawingUpdateCounter += 1
            return
        }

        let textStyle = TextStyle(
            color: settings.strokeColor,
            fontSize: settings.textSize,
            fontName: ".AppleSystemUIFont"
        )
        let annotation = Annotation.text(
            TextAnnotation(position: position, content: trimmedText, style: textStyle)
        )
        addAnnotation(annotation)
        drawingUpdateCounter += 1
    }

    // MARK: - Annotation Selection, Resizing & Editing

    /// Tests if a point hits a control handle of the currently selected annotation
    func hitTestSelectedAnnotationHandle(at point: CGPoint) -> AnnotationHandle? {
        guard let index = selectedAnnotationIndex,
              index >= 0,
              index < annotations.count else { return nil }
        let tolerance = 10.0 * screenshot.sourceDisplay.scaleFactor
        return annotations[index].hitTestHandle(at: point, padding: 4, tolerance: tolerance)
    }

    /// Tests if a point hits an annotation using exact geometric distance and returns its index
    /// - Parameter point: The point to test in image coordinates
    /// - Returns: The index of the hit annotation, or nil if none hit
    func hitTest(at point: CGPoint) -> Int? {
        let tolerance = 8.0 * screenshot.sourceDisplay.scaleFactor
        for (index, annotation) in annotations.enumerated().reversed() {
            if annotation.hitTest(point: point, tolerance: tolerance) {
                return index
            }
        }
        return nil
    }

    /// Selects the annotation at the given index
    func selectAnnotation(at index: Int?) {
        if isWaitingForTextInputInternal && editingAnnotationIndex != index {
            commitTextInput()
        }
        // Deselect any tool when selecting an annotation
        if index != nil && selectedTool != nil {
            selectedTool = nil
        }
        selectedAnnotationIndex = index
    }

    /// Deselects any selected annotation
    func deselectAnnotation() {
        if isWaitingForTextInputInternal {
            commitTextInput()
        }
        selectedAnnotationIndex = nil
        isDraggingAnnotation = false
        activeResizeHandle = nil
        dragStartPoint = nil
        dragOriginalPosition = nil
        dragOriginalAnnotation = nil
        dragInitialScreenshot = nil
    }

    /// Deletes the currently selected annotation
    func deleteSelectedAnnotation() {
        guard let index = selectedAnnotationIndex else { return }
        if editingAnnotationIndex == index {
            cancelCurrentDrawing()
        }
        pushUndoState()
        screenshot = screenshot.removingAnnotation(at: index)
        redoStack.removeAll()
        selectedAnnotationIndex = nil
    }

    /// Begins resizing or adjusting a control handle on the selected annotation
    func beginResizingAnnotation(handle: AnnotationHandle, at point: CGPoint) {
        guard let index = selectedAnnotationIndex,
              index >= 0,
              index < annotations.count else { return }

        if isWaitingForTextInputInternal {
            commitTextInput()
        }

        activeResizeHandle = handle
        dragStartPoint = point
        dragOriginalAnnotation = annotations[index]
        dragInitialScreenshot = screenshot
    }

    /// Continues resizing or adjusting a control handle on the selected annotation
    func continueResizingAnnotation(to point: CGPoint) {
        guard let handle = activeResizeHandle,
              let index = selectedAnnotationIndex,
              index >= 0,
              index < annotations.count else { return }

        // ⌘ held: preserve the annotation's current aspect ratio
        let options = HandleDragOptions(
            preserveAspectRatio: NSEvent.modifierFlags.contains(.command)
        )
        let updated = annotations[index].applyingHandleDrag(handle, to: point, options: options)
        screenshot.annotations[index] = updated
        drawingUpdateCounter += 1
    }

    /// Ends resizing or adjusting a control handle and records Undo if modified
    func endResizingAnnotation() {
        guard activeResizeHandle != nil else { return }
        if let initial = dragInitialScreenshot, initial.annotations != screenshot.annotations {
            undoStack.append(initial)
            if undoStack.count > maxUndoLevels {
                undoStack.removeFirst()
            }
            redoStack.removeAll()
        }
        activeResizeHandle = nil
        dragStartPoint = nil
        dragOriginalAnnotation = nil
        dragInitialScreenshot = nil
    }

    /// Begins dragging the selected annotation
    func beginDraggingAnnotation(at point: CGPoint) {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return }

        if isWaitingForTextInputInternal && editingAnnotationIndex != index {
            commitTextInput()
        }

        isDraggingAnnotation = true
        dragStartPoint = point
        dragOriginalAnnotation = annotations[index]
        dragInitialScreenshot = screenshot
        dragOriginalPosition = annotations[index].bounds.origin
    }

    /// Continues dragging the selected annotation
    func continueDraggingAnnotation(to point: CGPoint) {
        guard isDraggingAnnotation,
              let index = selectedAnnotationIndex,
              let startPoint = dragStartPoint,
              let originalAnnotation = dragOriginalAnnotation,
              index < annotations.count else { return }

        let delta = CGPoint(
            x: point.x - startPoint.x,
            y: point.y - startPoint.y
        )

        let updated = originalAnnotation.offsetBy(dx: delta.x, dy: delta.y)
        screenshot.annotations[index] = updated
        drawingUpdateCounter += 1
    }

    /// Ends dragging the selected annotation and records Undo state if moved
    func endDraggingAnnotation() {
        if isDraggingAnnotation,
           let initial = dragInitialScreenshot,
           initial.annotations != screenshot.annotations {
            undoStack.append(initial)
            if undoStack.count > maxUndoLevels {
                undoStack.removeFirst()
            }
            redoStack.removeAll()
        }
        isDraggingAnnotation = false
        dragStartPoint = nil
        dragOriginalPosition = nil
        dragOriginalAnnotation = nil
        dragInitialScreenshot = nil
    }

    /// Updates the color of the selected annotation
    func updateSelectedAnnotationColor(_ color: CodableColor) {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return }

        pushUndoState()
        let annotation = annotations[index]
        var updatedAnnotation: Annotation?

        switch annotation {
        case .rectangle(var rect):
            rect.style.color = color
            updatedAnnotation = .rectangle(rect)

        case .ellipse(var ellipse):
            ellipse.style.color = color
            updatedAnnotation = .ellipse(ellipse)

        case .line(var line):
            line.style.color = color
            updatedAnnotation = .line(line)

        case .freehand(var freehand):
            freehand.style.color = color
            updatedAnnotation = .freehand(freehand)

        case .arrow(var arrow):
            arrow.style.color = color
            updatedAnnotation = .arrow(arrow)

        case .highlight(var highlight):
            highlight.color = color
            updatedAnnotation = .highlight(highlight)

        case .mosaic:
            return

        case .text(var text):
            text.style.color = color
            updatedAnnotation = .text(text)

        case .numberLabel(var label):
            label.color = color
            updatedAnnotation = .numberLabel(label)

        case .privacyRedaction(var redaction):
            redaction.fillColor = color
            updatedAnnotation = .privacyRedaction(redaction)

        case .calloutBubble(var bubble):
            bubble.bubbleColor = color
            updatedAnnotation = .calloutBubble(bubble)
        }

        if let updated = updatedAnnotation {
            screenshot = screenshot.replacingAnnotation(at: index, with: updated)
            redoStack.removeAll()
        }
    }

    /// Updates the stroke width of the selected annotation
    func updateSelectedAnnotationStrokeWidth(_ width: CGFloat) {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return }

        pushUndoState()
        let annotation = annotations[index]
        var updatedAnnotation: Annotation?

        switch annotation {
        case .rectangle(var rect):
            rect.style.lineWidth = width
            updatedAnnotation = .rectangle(rect)

        case .ellipse(var ellipse):
            ellipse.style.lineWidth = width
            updatedAnnotation = .ellipse(ellipse)

        case .line(var line):
            line.style.lineWidth = width
            updatedAnnotation = .line(line)

        case .freehand(var freehand):
            freehand.style.lineWidth = width
            updatedAnnotation = .freehand(freehand)

        case .arrow(var arrow):
            arrow.style.lineWidth = width
            updatedAnnotation = .arrow(arrow)

        case .highlight, .mosaic, .text, .numberLabel, .privacyRedaction, .calloutBubble:
            return
        }

        if let updated = updatedAnnotation {
            screenshot = screenshot.replacingAnnotation(at: index, with: updated)
            redoStack.removeAll()
        }
    }

    /// Updates the font size of the selected text or callout bubble annotation
    func updateSelectedAnnotationFontSize(_ size: CGFloat) {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return }

        let annotation = annotations[index]
        switch annotation {
        case .text(var text):
            guard text.style.fontSize != size else { return }
            pushUndoState()
            text.style.fontSize = size
            screenshot = screenshot.replacingAnnotation(at: index, with: .text(text))
            redoStack.removeAll()
        case .calloutBubble(var bubble):
            guard bubble.style.fontSize != size else { return }
            pushUndoState()
            bubble.style.fontSize = size
            screenshot = screenshot.replacingAnnotation(at: index, with: .calloutBubble(bubble))
            redoStack.removeAll()
        default:
            return
        }
    }

    /// Updates the isFilled state of the selected rectangle or ellipse annotation
    func updateSelectedAnnotationFilled(_ isFilled: Bool) {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return }

        let annotation = annotations[index]

        let shouldUpdate: Bool
        if case .rectangle(let rect) = annotation {
            shouldUpdate = rect.isFilled != isFilled
        } else if case .ellipse(let ellipse) = annotation {
            shouldUpdate = ellipse.isFilled != isFilled
        } else {
            return
        }

        guard shouldUpdate else { return }

        pushUndoState()

        if case .rectangle(var rect) = annotation {
            rect.isFilled = isFilled
            screenshot = screenshot.replacingAnnotation(at: index, with: .rectangle(rect))
        } else if case .ellipse(var ellipse) = annotation {
            ellipse.isFilled = isFilled
            screenshot = screenshot.replacingAnnotation(at: index, with: .ellipse(ellipse))
        }

        redoStack.removeAll()
    }

    /// Updates the background fill color of the selected rectangle or ellipse annotation
    func updateSelectedAnnotationFillColor(_ color: CodableColor) {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return }

        switch annotations[index] {
        case .rectangle(var rect):
            guard rect.fillColor != color else { return }
            pushUndoState()
            rect.fillColor = color
            screenshot = screenshot.replacingAnnotation(at: index, with: .rectangle(rect))
            redoStack.removeAll()
        case .ellipse(var ellipse):
            guard ellipse.fillColor != color else { return }
            pushUndoState()
            ellipse.fillColor = color
            screenshot = screenshot.replacingAnnotation(at: index, with: .ellipse(ellipse))
            redoStack.removeAll()
        default:
            return
        }
    }

    /// Updates the block size of the selected mosaic annotation
    func updateSelectedAnnotationBlockSize(_ blockSize: Int) {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return }

        let annotation = annotations[index]
        guard case .mosaic(var mosaic) = annotation else { return }

        let clampedBlockSize = max(1, blockSize)
        guard mosaic.blockSize != clampedBlockSize else { return }

        pushUndoState()
        mosaic.blockSize = clampedBlockSize
        screenshot = screenshot.replacingAnnotation(at: index, with: .mosaic(mosaic))
        redoStack.removeAll()
    }

    /// Re-edits the selected text or calloutBubble annotation in-place without deleting or altering Z-Order
    func editSelectedText() {
        guard let index = selectedAnnotationIndex, index < annotations.count else { return }
        beginEditingTextAnnotation(at: index)
    }

    /// Begins in-place editing of a text or calloutBubble annotation at the given index
    func beginEditingTextAnnotation(at index: Int) {
        guard index >= 0 && index < annotations.count else { return }
        if isWaitingForTextInputInternal && editingAnnotationIndex != index {
            commitTextInput()
        }

        switch annotations[index] {
        case .text(let text):
            selectedAnnotationIndex = index
            editingAnnotationIndex = index
            textInputContent = text.content
            textInputPositionInternal = text.position
            isWaitingForTextInputInternal = true
        case .calloutBubble(let bubble):
            selectedAnnotationIndex = index
            editingAnnotationIndex = index
            textInputContent = bubble.text
            let scaleFactor = screenshot.sourceDisplay.scaleFactor
            textInputPositionInternal = CGPoint(
                x: bubble.rect.minX + 6 * scaleFactor,
                y: bubble.rect.minY + 6 * scaleFactor
            )
            isWaitingForTextInputInternal = true
        default:
            break
        }
    }

    /// Sets or updates the quadratic bezier control point for a curved arrow
    func updateSelectedArrowCurve(controlPoint: CGPoint?) {
        guard let index = selectedAnnotationIndex, index < annotations.count else { return }
        guard case .arrow(var arrow) = annotations[index] else { return }
        pushUndoState()
        arrow.controlPoint = controlPoint
        screenshot = screenshot.replacingAnnotation(at: index, with: .arrow(arrow))
        redoStack.removeAll()
    }
}
