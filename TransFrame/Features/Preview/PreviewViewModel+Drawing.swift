import Foundation
import SwiftUI

// MARK: - Drawing & Annotation Methods

extension PreviewViewModel {
    // MARK: - Drawing Methods

    /// Begins a drawing gesture at the given point
    /// - Parameter point: The point in image coordinates
    func beginDrawing(at point: CGPoint) {
        guard let selectedTool else { return }

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
            rectangleTool.beginDrawing(at: point)
        case .ellipse:
            ellipseTool.strokeStyle = strokeStyle
            ellipseTool.isFilled = settings.ellipseFilled
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
            highlightTool.beginDrawing(at: point)
        case .mosaic:
            mosaicTool.blockSize = Int(settings.mosaicBlockSize)
            mosaicTool.beginDrawing(at: point)
        case .text:
            textTool.textStyle = textStyle
            textTool.beginDrawing(at: point)
            isWaitingForTextInputInternal = true
            textInputPositionInternal = point
        case .numberLabel:
            numberLabelTool.beginDrawing(at: point)
        case .privacyRedaction:
            rectangleTool.strokeStyle = strokeStyle
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
            if let rectAnno = rectangleTool.endDrawing(at: point), case .rectangle(let r) = rectAnno {
                let target = CGPoint(x: r.rect.midX, y: r.rect.maxY + 20)
                annotation = .calloutBubble(CalloutBubbleAnnotation(rect: r.rect, targetPoint: target, text: "Note"))
            }
        }

        currentAnnotationInternal = nil
        drawingUpdateCounter += 1

        if let annotation {
            addAnnotation(annotation)
        }
    }

    /// Cancels the current drawing operation
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
        drawingUpdateCounter += 1
    }

    /// Updates the cached current annotation to trigger view refresh
    func updateCurrentAnnotation() {
        currentAnnotationInternal = currentTool?.currentAnnotation
        drawingUpdateCounter += 1
    }

    /// Commits the current text input and adds the annotation
    func commitTextInput() {
        guard let position = textInputPositionInternal else { return }

        let trimmedText = textInputContent.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedText.isEmpty else {
            cancelCurrentDrawing()
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

        // Reset observable text input state
        textInputContent = ""
        isWaitingForTextInputInternal = false
        textInputPositionInternal = nil
    }

    // MARK: - Annotation Selection & Editing

    /// Tests if a point hits an annotation and returns its index
    /// - Parameter point: The point to test in image coordinates
    /// - Returns: The index of the hit annotation, or nil if none hit
    func hitTest(at point: CGPoint) -> Int? {
        // Check in reverse order (top-most first)
        for (index, annotation) in annotations.enumerated().reversed() {
            let bounds = annotation.bounds
            // Add some padding for easier selection
            let expandedBounds = bounds.insetBy(dx: -10, dy: -10)
            if expandedBounds.contains(point) {
                return index
            }
        }
        return nil
    }

    /// Selects the annotation at the given index
    func selectAnnotation(at index: Int?) {
        // Deselect any tool when selecting an annotation
        if index != nil && selectedTool != nil {
            selectedTool = nil
        }
        selectedAnnotationIndex = index
    }

    /// Deselects any selected annotation
    func deselectAnnotation() {
        selectedAnnotationIndex = nil
        isDraggingAnnotation = false
        dragStartPoint = nil
        dragOriginalPosition = nil
    }

    /// Deletes the currently selected annotation
    func deleteSelectedAnnotation() {
        guard let index = selectedAnnotationIndex else { return }
        pushUndoState()
        screenshot = screenshot.removingAnnotation(at: index)
        redoStack.removeAll()
        selectedAnnotationIndex = nil
    }

    /// Begins dragging the selected annotation
    func beginDraggingAnnotation(at point: CGPoint) {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return }

        isDraggingAnnotation = true
        dragStartPoint = point

        // Store the original position based on annotation type
        let annotation = annotations[index]
        switch annotation {
        case .rectangle(let rect):
            dragOriginalPosition = rect.rect.origin
        case .ellipse(let ellipse):
            dragOriginalPosition = ellipse.rect.origin
        case .line(let line):
            dragOriginalPosition = line.bounds.origin
        case .freehand(let freehand):
            dragOriginalPosition = freehand.bounds.origin
        case .arrow(let arrow):
            dragOriginalPosition = arrow.bounds.origin
        case .highlight(let highlight):
            dragOriginalPosition = highlight.rect.origin
        case .mosaic(let mosaic):
            dragOriginalPosition = mosaic.rect.origin
        case .text(let text):
            dragOriginalPosition = text.position
        case .numberLabel(let label):
            dragOriginalPosition = label.position
        case .privacyRedaction(let redaction):
            dragOriginalPosition = redaction.rect.origin
        case .calloutBubble(let bubble):
            dragOriginalPosition = bubble.rect.origin
        }
    }

    /// Continues dragging the selected annotation
    func continueDraggingAnnotation(to point: CGPoint) {
        guard isDraggingAnnotation,
              let index = selectedAnnotationIndex,
              let startPoint = dragStartPoint,
              let originalPosition = dragOriginalPosition,
              index < annotations.count else { return }

        let delta = CGPoint(
            x: point.x - startPoint.x,
            y: point.y - startPoint.y
        )

        let annotation = annotations[index]
        var updatedAnnotation: Annotation?

        switch annotation {
        case .rectangle(var rect):
            rect.rect.origin = CGPoint(
                x: originalPosition.x + delta.x,
                y: originalPosition.y + delta.y
            )
            updatedAnnotation = .rectangle(rect)

        case .ellipse(var ellipse):
            ellipse.rect.origin = CGPoint(
                x: originalPosition.x + delta.x,
                y: originalPosition.y + delta.y
            )
            updatedAnnotation = .ellipse(ellipse)

        case .line(var line):
            let bounds = line.bounds
            let offsetX = originalPosition.x + delta.x - bounds.origin.x
            let offsetY = originalPosition.y + delta.y - bounds.origin.y
            line.startPoint = CGPoint(x: line.startPoint.x + offsetX, y: line.startPoint.y + offsetY)
            line.endPoint = CGPoint(x: line.endPoint.x + offsetX, y: line.endPoint.y + offsetY)
            updatedAnnotation = .line(line)

        case .freehand(var freehand):
            let bounds = freehand.bounds
            let offsetX = originalPosition.x + delta.x - bounds.origin.x
            let offsetY = originalPosition.y + delta.y - bounds.origin.y
            freehand.points = freehand.points.map { point in
                CGPoint(x: point.x + offsetX, y: point.y + offsetY)
            }
            updatedAnnotation = .freehand(freehand)

        case .arrow(var arrow):
            let bounds = arrow.bounds
            let offsetX = originalPosition.x + delta.x - bounds.origin.x
            let offsetY = originalPosition.y + delta.y - bounds.origin.y
            arrow.startPoint = CGPoint(
                x: arrow.startPoint.x + offsetX,
                y: arrow.startPoint.y + offsetY
            )
            arrow.endPoint = CGPoint(
                x: arrow.endPoint.x + offsetX,
                y: arrow.endPoint.y + offsetY
            )
            updatedAnnotation = .arrow(arrow)

        case .highlight(var highlight):
            highlight.rect.origin = CGPoint(
                x: originalPosition.x + delta.x,
                y: originalPosition.y + delta.y
            )
            updatedAnnotation = .highlight(highlight)

        case .mosaic(var mosaic):
            mosaic.rect.origin = CGPoint(
                x: originalPosition.x + delta.x,
                y: originalPosition.y + delta.y
            )
            updatedAnnotation = .mosaic(mosaic)

        case .text(var text):
            text.position = CGPoint(
                x: originalPosition.x + delta.x,
                y: originalPosition.y + delta.y
            )
            updatedAnnotation = .text(text)

        case .numberLabel(var label):
            label.position = CGPoint(
                x: originalPosition.x + delta.x,
                y: originalPosition.y + delta.y
            )
            updatedAnnotation = .numberLabel(label)

        case .privacyRedaction(var redaction):
            redaction.rect.origin = CGPoint(
                x: originalPosition.x + delta.x,
                y: originalPosition.y + delta.y
            )
            updatedAnnotation = .privacyRedaction(redaction)

        case .calloutBubble(var bubble):
            let shiftX = originalPosition.x + delta.x - bubble.rect.origin.x
            let shiftY = originalPosition.y + delta.y - bubble.rect.origin.y
            bubble.rect.origin = CGPoint(
                x: originalPosition.x + delta.x,
                y: originalPosition.y + delta.y
            )
            bubble.targetPoint = CGPoint(
                x: bubble.targetPoint.x + shiftX,
                y: bubble.targetPoint.y + shiftY
            )
            updatedAnnotation = .calloutBubble(bubble)
        }

        if let updated = updatedAnnotation {
            // Update without pushing undo (will push on end)
            screenshot.annotations[index] = updated
            drawingUpdateCounter += 1
        }
    }

    /// Ends dragging the selected annotation
    func endDraggingAnnotation() {
        isDraggingAnnotation = false
        dragStartPoint = nil
        dragOriginalPosition = nil
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

    /// Updates the font size of the selected text annotation
    func updateSelectedAnnotationFontSize(_ size: CGFloat) {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return }

        let annotation = annotations[index]
        guard case .text(var text) = annotation else { return }

        pushUndoState()
        text.style.fontSize = size
        screenshot = screenshot.replacingAnnotation(at: index, with: .text(text))
        redoStack.removeAll()
    }

    /// Updates the isFilled state of the selected rectangle or ellipse annotation
    func updateSelectedAnnotationFilled(_ isFilled: Bool) {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return }

        let annotation = annotations[index]
        
        // Check if the fill state is actually changing
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

    /// Updates the block size of the selected mosaic annotation
    func updateSelectedAnnotationBlockSize(_ blockSize: Int) {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return }

        let annotation = annotations[index]
        guard case .mosaic(var mosaic) = annotation else { return }

        // Enforce minimum block size of 1 to prevent rendering errors
        let clampedBlockSize = max(1, blockSize)
        guard mosaic.blockSize != clampedBlockSize else { return }

        pushUndoState()
        mosaic.blockSize = clampedBlockSize
        screenshot = screenshot.replacingAnnotation(at: index, with: .mosaic(mosaic))
        redoStack.removeAll()
    }
}
