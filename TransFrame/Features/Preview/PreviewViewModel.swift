import Foundation
import SwiftUI
import AppKit
import Observation

/// ViewModel for the screenshot preview window.
/// Manages screenshot state, annotations, and user actions.
/// Must run on MainActor for UI binding.
@MainActor
@Observable
final class PreviewViewModel {
    // MARK: - Properties

    var screenshot: Screenshot

    @ObservationIgnored
    private(set) var isVisible: Bool = false

    var selectedTool: AnnotationToolType? {
        didSet {
            if oldValue != selectedTool {
                if isWaitingForTextInputInternal {
                    commitTextInput()
                } else {
                    cancelCurrentDrawing()
                }
            }
            if selectedTool != nil && isCropMode {
                isCropMode = false
                cropRect = nil
            }
        }
    }

    var isCropMode: Bool = false {
        didSet {
            if isCropMode && isWaitingForTextInputInternal {
                commitTextInput()
            }
            if isCropMode && selectedTool != nil {
                selectedTool = nil
            }
            if !isCropMode {
                cropRect = nil
            }
        }
    }

    var cropRect: CGRect?
    var isCropSelecting: Bool = false
    var cropStartPoint: CGPoint?
    var errorMessage: String?

    var isSaving: Bool = false
    var isCopying: Bool = false
    var copySuccessMessage: String?

    var isSavingWithTranslations: Bool = false
    var saveSuccessMessage: String?

    @ObservationIgnored
    var onDismiss: (() -> Void)?

    @ObservationIgnored
    var onSave: ((URL) -> Void)?

    @ObservationIgnored
    let settings = AppSettings.shared

    @ObservationIgnored
    let imageExporter = ImageExporter.shared

    @ObservationIgnored
    let clipboardService = ClipboardService.shared

    @ObservationIgnored
    let ocrService = OCRService.shared

    @ObservationIgnored
    let translationEngine = TranslationEngine.shared

    var ocrResult: OCRResult?
    var isPerformingOCR: Bool = false
    /// 面板显示哪个结果，由工具栏最后一次触发的动作决定（面板内无切换）。
    enum ResultsPanelTab {
        case ocr
        case translate
    }
    var resultsPanelTab: ResultsPanelTab = .ocr
    var ocrTranslationError: String?

    var isDetectingPrivacy: Bool = false
    var smartRedactionMessage: String?

    var bilingualSegments: [BilingualSegment] = []
    var isTranslating: Bool = false

    @ObservationIgnored
    var ocrTask: Task<Void, Never>?
    @ObservationIgnored
    var translationTask: Task<Void, Never>?
    var analysisGeneration: UInt64 = 0

    // MARK: - Annotation Tools

    @ObservationIgnored
    var rectangleTool = RectangleTool()

    @ObservationIgnored
    var ellipseTool = EllipseTool()

    @ObservationIgnored
    var lineTool = LineTool()

    @ObservationIgnored
    var freehandTool = FreehandTool()

    @ObservationIgnored
    var arrowTool = ArrowTool()

    @ObservationIgnored
    var highlightTool = HighlightTool()

    @ObservationIgnored
    var mosaicTool = MosaicTool()

    @ObservationIgnored
    var textTool = TextTool()

    @ObservationIgnored
    var numberLabelTool = NumberLabelTool()

    var drawingUpdateCounter: Int = 0
    var currentAnnotationInternal: Annotation?
    var isWaitingForTextInputInternal: Bool = false
    var textInputPositionInternal: CGPoint?

    // MARK: - Annotation Selection & Editing

    var selectedAnnotationIndex: Int?
    var editingAnnotationIndex: Int?
    var isDraggingAnnotation: Bool = false
    var activeResizeHandle: AnnotationHandle?

    var isResizingAnnotation: Bool {
        activeResizeHandle != nil
    }

    @ObservationIgnored
    var dragStartPoint: CGPoint?

    @ObservationIgnored
    var dragOriginalPosition: CGPoint?

    @ObservationIgnored
    var dragOriginalAnnotation: Annotation?

    @ObservationIgnored
    var dragInitialScreenshot: Screenshot?

    var isDrawing: Bool {
        currentTool?.isActive ?? false
    }

    var currentAnnotation: Annotation? {
        currentAnnotationInternal
    }

    var currentTool: (any AnnotationTool)? {
        guard let selectedTool else { return nil }
        switch selectedTool {
        case .rectangle: return rectangleTool
        case .ellipse: return ellipseTool
        case .line: return lineTool
        case .arrow: return arrowTool
        case .freehand: return freehandTool
        case .highlight: return highlightTool
        case .mosaic: return mosaicTool
        case .text: return textTool
        case .numberLabel: return numberLabelTool
        case .privacyRedaction, .calloutBubble: return rectangleTool
        }
    }

    var isWaitingForTextInput: Bool {
        isWaitingForTextInputInternal
    }

    /// Synchronized text input content that stays in sync with TextTool
    var textInputContent: String {
        get {
            textTool.currentText
        }
        set {
            textTool.updateText(newValue)
        }
    }

    var textInputPosition: CGPoint? {
        textInputPositionInternal
    }

    // MARK: - Computed Properties

    var image: CGImage {
        screenshot.image
    }

    var annotations: [Annotation] {
        screenshot.annotations
    }

    var dimensionsText: String {
        screenshot.formattedDimensions
    }

    var fileSizeText: String {
        let format = settings.defaultFormat
        let pixelCount = Double(screenshot.image.width * screenshot.image.height)
        let bytes = Int(pixelCount * format.estimatedBytesPerPixel)

        if bytes < 1024 {
            return "\(bytes) B"
        } else if bytes < 1024 * 1024 {
            return String(format: "%.1f KB", Double(bytes) / 1024.0)
        } else {
            return String(format: "%.1f MB", Double(bytes) / (1024.0 * 1024.0))
        }
    }

    var displayName: String {
        screenshot.sourceDisplay.name
    }

    var format: ExportFormat {
        get { screenshot.format }
        set { screenshot = screenshot.with(format: newValue) }
    }

    var canUndo: Bool {
        !undoStack.isEmpty
    }

    var canRedo: Bool {
        !redoStack.isEmpty
    }

    // MARK: - Undo/Redo

    var undoStack: [Screenshot] = []
    var redoStack: [Screenshot] = []

    @ObservationIgnored
    let maxUndoLevels = 15

    var imageSizeChangeCounter: Int = 0

    // MARK: - Initialization

    init(screenshot: Screenshot) {
        self.screenshot = screenshot
    }

    // MARK: - Public API

    func show() {
        isVisible = true
    }

    func hide() {
        guard isVisible else { return }
        isVisible = false
        ocrTask?.cancel()
        translationTask?.cancel()
        undoStack.removeAll()
        redoStack.removeAll()
        onDismiss?()
    }

    func addAnnotation(_ annotation: Annotation) {
        pushUndoState()
        screenshot = screenshot.adding(annotation)
        redoStack.removeAll()
    }

    func removeAnnotation(at index: Int) {
        guard index >= 0 && index < annotations.count else { return }
        pushUndoState()
        screenshot = screenshot.removingAnnotation(at: index)
        redoStack.removeAll()
    }

    func undo() {
        guard let previousState = undoStack.popLast() else { return }

        let currentSize = CGSize(width: screenshot.image.width, height: screenshot.image.height)
        let previousSize = CGSize(width: previousState.image.width, height: previousState.image.height)
        let imageSizeChanged = currentSize != previousSize

        redoStack.append(screenshot)
        screenshot = previousState

        if imageSizeChanged {
            imageSizeChangeCounter += 1
        }
    }

    func redo() {
        guard let nextState = redoStack.popLast() else { return }

        let currentSize = CGSize(width: screenshot.image.width, height: screenshot.image.height)
        let nextSize = CGSize(width: nextState.image.width, height: nextState.image.height)
        let imageSizeChanged = currentSize != nextSize

        undoStack.append(screenshot)
        screenshot = nextState

        if imageSizeChanged {
            imageSizeChangeCounter += 1
        }
    }

    func selectTool(_ tool: AnnotationToolType?) {
        // Reset number label counter when deselecting the tool
        if selectedTool == .numberLabel && tool != .numberLabel {
            numberLabelTool.resetNumber()
        }
        selectedTool = tool
    }

    func dismiss() {
        hide()
    }

    func pushUndoState() {
        undoStack.append(screenshot)

        if undoStack.count > maxUndoLevels {
            undoStack.removeFirst()
        }
    }

    func clearError() {
        Task {
            try? await Task.sleep(for: .seconds(3))
            errorMessage = nil
        }
    }

    // MARK: - Selected Annotation Properties

    var selectedAnnotationType: AnnotationToolType? {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return nil }

        switch annotations[index] {
        case .rectangle: return .rectangle
        case .ellipse: return .ellipse
        case .line: return .line
        case .freehand: return .freehand
        case .arrow: return .arrow
        case .highlight: return .highlight
        case .mosaic: return .mosaic
        case .text: return .text
        case .numberLabel: return .numberLabel
        case .privacyRedaction: return .privacyRedaction
        case .calloutBubble: return .calloutBubble
        }
    }

    var selectedAnnotationColor: CodableColor? {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return nil }

        switch annotations[index] {
        case .rectangle(let rect): return rect.style.color
        case .ellipse(let ellipse): return ellipse.style.color
        case .line(let line): return line.style.color
        case .freehand(let freehand): return freehand.style.color
        case .arrow(let arrow): return arrow.style.color
        case .text(let text): return text.style.color
        case .highlight(let highlight): return highlight.color
        case .mosaic: return nil
        case .numberLabel(let label): return label.color
        case .privacyRedaction(let redaction): return redaction.fillColor
        case .calloutBubble(let bubble): return bubble.bubbleColor
        }
    }

    var selectedAnnotationStrokeWidth: CGFloat? {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return nil }

        switch annotations[index] {
        case .rectangle(let rect): return rect.style.lineWidth
        case .ellipse(let ellipse): return ellipse.style.lineWidth
        case .line(let line): return line.style.lineWidth
        case .freehand(let freehand): return freehand.style.lineWidth
        case .arrow(let arrow): return arrow.style.lineWidth
        case .text: return nil
        case .mosaic: return nil
        case .highlight: return nil
        case .numberLabel: return nil
        case .privacyRedaction: return nil
        case .calloutBubble: return nil
        }
    }

    var selectedAnnotationFontSize: CGFloat? {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return nil }

        if case .text(let text) = annotations[index] {
            return text.style.fontSize
        }
        if case .calloutBubble(let bubble) = annotations[index] {
            return bubble.style.fontSize
        }
        return nil
    }

    var selectedAnnotationIsFilled: Bool? {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return nil }

        if case .rectangle(let rect) = annotations[index] {
            return rect.isFilled
        }
        if case .ellipse(let ellipse) = annotations[index] {
            return ellipse.isFilled
        }
        if case .privacyRedaction = annotations[index] {
            return true
        }
        return nil
    }

    var selectedAnnotationFillColor: CodableColor? {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return nil }

        if case .rectangle(let rect) = annotations[index] {
            return rect.effectiveFillColor
        }
        if case .ellipse(let ellipse) = annotations[index] {
            return ellipse.effectiveFillColor
        }
        return nil
    }

    var selectedAnnotationBlockSize: Int? {
        guard let index = selectedAnnotationIndex,
              index < annotations.count else { return nil }

        if case .mosaic(let mosaic) = annotations[index] {
            return mosaic.blockSize
        }
        return nil
    }

    var hasOCRResults: Bool {
        ocrResult?.hasResults ?? false
    }

    var combinedOCRText: String {
        ocrResult?.fullText ?? ""
    }

    // MARK: - Pin Functionality

    /// Pins the current screenshot with all annotations
    func pinScreenshot() {
        if isWaitingForTextInputInternal {
            commitTextInput()
        }
        PinnedWindowsManager.shared.pinScreenshot(screenshot, annotations: annotations)
    }

    /// Checks if the current screenshot is pinned
    var isPinned: Bool {
        PinnedWindowsManager.shared.isPinned(screenshot.id)
    }
}

// MARK: - Annotation Tool Type

enum AnnotationToolType: String, CaseIterable, Identifiable, Sendable {
    case rectangle
    case ellipse
    case line
    case arrow
    case freehand
    case highlight
    case mosaic
    case text
    case numberLabel
    case privacyRedaction
    case calloutBubble

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .rectangle: return String(localized: "tool.rectangle")
        case .ellipse: return String(localized: "tool.ellipse")
        case .line: return String(localized: "tool.line")
        case .arrow: return String(localized: "tool.arrow")
        case .freehand: return String(localized: "tool.freehand")
        case .highlight: return String(localized: "tool.highlight")
        case .mosaic: return String(localized: "tool.mosaic")
        case .text: return String(localized: "tool.text")
        case .numberLabel: return String(localized: "tool.numberLabel")
        case .privacyRedaction: return String(localized: "tool.privacyRedaction")
        case .calloutBubble: return String(localized: "tool.calloutBubble")
        }
    }

    var keyboardShortcut: Character {
        switch self {
        case .rectangle: return "r"
        case .ellipse: return "o"
        case .line: return "l"
        case .arrow: return "a"
        case .freehand: return "d"
        case .highlight: return "h"
        case .mosaic: return "m"
        case .text: return "t"
        case .numberLabel: return "n"
        case .privacyRedaction: return "x"
        case .calloutBubble: return "b"
        }
    }

    var systemImage: String {
        switch self {
        case .rectangle: return "rectangle"
        case .ellipse: return "circle"
        case .line: return "line.diagonal"
        case .arrow: return "arrow.up.right"
        case .freehand: return "pencil.line"
        case .highlight: return "highlighter"
        case .mosaic: return "checkerboard.rectangle"
        case .text: return "textbox"
        case .numberLabel: return "number.circle"
        case .privacyRedaction: return "eye.slash.fill"
        case .calloutBubble: return "bubble.left.and.bubble.right"
        }
    }

    var cursor: NSCursor {
        switch self {
        case .text:
            return .iBeam
        case .freehand:
            return NSCursor.symbolCursor(systemName: "pencil", hotSpot: NSPoint(x: 3, y: 17)) ?? .crosshair
        case .highlight:
            return NSCursor.symbolCursor(systemName: "highlighter", hotSpot: NSPoint(x: 3, y: 17)) ?? .crosshair
        case .mosaic:
            return NSCursor.symbolCursor(systemName: "checkerboard.rectangle", hotSpot: NSPoint(x: 10, y: 10)) ?? .crosshair
        case .privacyRedaction:
            return NSCursor.symbolCursor(systemName: "eye.slash.fill", hotSpot: NSPoint(x: 10, y: 10)) ?? .crosshair
        case .numberLabel:
            return NSCursor.symbolCursor(systemName: "number.circle.fill", hotSpot: NSPoint(x: 10, y: 10)) ?? .crosshair
        case .rectangle, .ellipse, .line, .arrow, .calloutBubble:
            return .crosshair
        }
    }
}
