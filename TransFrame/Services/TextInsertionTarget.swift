import AppKit
import ApplicationServices

struct TextInsertionSnapshot: Equatable, Sendable {
    var value: String
    var range: NSRange

    var selectedText: String? {
        let length = (value as NSString).length
        guard range.location >= 0, range.length > 0,
              range.location <= length, range.length <= length - range.location else { return nil }
        return (value as NSString).substring(with: range)
    }
}

@MainActor
protocol TextInsertionTarget: AnyObject {
    var isFocused: Bool { get }
    func snapshot() throws -> TextInsertionSnapshot
    func replaceSelectedText(_ text: String) throws
}

/// AX 引用仅在主线程使用；快照在捕获时确定，禁止翻译结束后重新选择目标。
@MainActor
final class CapturedTextInsertion {
    let selectedText: String
    private let target: any TextInsertionTarget
    private let selection: TextInsertionSnapshot
    private let expectedClipboardText: String

    init(
        target: any TextInsertionTarget,
        selection: TextInsertionSnapshot,
        selectedText: String,
        expectedClipboardText: String? = nil
    ) throws {
        guard selection.selectedText == selectedText, !selectedText.isEmpty else {
            throw TextInsertService.InsertError.targetUnavailable
        }
        self.target = target
        self.selection = selection
        self.selectedText = selectedText
        self.expectedClipboardText = expectedClipboardText ?? selectedText
    }

    func matching(clipboardText: String) -> CapturedTextInsertion? {
        try? CapturedTextInsertion(
            target: target, selection: selection,
            selectedText: selectedText, expectedClipboardText: clipboardText
        )
    }

    func replace(with text: String) throws {
        try Task.checkCancellation()
        guard !text.isEmpty else { throw TextInsertService.InsertError.insertionNotVerified }
        guard selectedText == expectedClipboardText,
              target.isFocused, try target.snapshot() == selection else {
            throw TextInsertService.InsertError.targetChanged
        }
        let expected = (selection.value as NSString).replacingCharacters(in: selection.range, with: text)
        // 单次 AX 替换不单独发送 Delete；失败也可能已生效，必须读回后再判断。
        do {
            try target.replaceSelectedText(text)
        } catch {
            guard let after = try? target.snapshot(), after.value == expected else { throw error }
            return
        }
        guard try target.snapshot().value == expected else {
            throw TextInsertService.InsertError.insertionNotVerified
        }
    }
}

@MainActor
final class AXTextInsertionTarget: TextInsertionTarget {
    private let element: AXUIElement
    private let processID: pid_t

    private init(element: AXUIElement, processID: pid_t) {
        self.element = element
        self.processID = processID
    }

    static func capture() -> CapturedTextInsertion? {
        guard AXIsProcessTrusted(), let element = focusedElement(),
              let application = NSWorkspace.shared.frontmostApplication else { return nil }
        var pid: pid_t = 0
        guard AXUIElementGetPid(element, &pid) == .success,
              pid == application.processIdentifier else { return nil }
        let target = AXTextInsertionTarget(element: element, processID: pid)
        guard let selection = try? target.snapshot(), let text = selection.selectedText,
              target.isFocused else { return nil }
        return try? CapturedTextInsertion(target: target, selection: selection, selectedText: text)
    }

    var isFocused: Bool {
        guard NSWorkspace.shared.frontmostApplication?.processIdentifier == processID,
              let focused = Self.focusedElement() else { return false }
        return CFEqual(focused, element)
    }

    func snapshot() throws -> TextInsertionSnapshot {
        guard let value = attribute(kAXValueAttribute) as? String,
              let rangeObject = attribute(kAXSelectedTextRangeAttribute),
              CFGetTypeID(rangeObject) == AXValueGetTypeID() else {
            throw TextInsertService.InsertError.targetUnavailable
        }
        let rangeValue = unsafeDowncast(rangeObject, to: AXValue.self)
        var range = CFRange()
        guard AXValueGetType(rangeValue) == .cfRange,
              AXValueGetValue(rangeValue, .cfRange, &range) else {
            throw TextInsertService.InsertError.targetUnavailable
        }
        // 不支持多个不连续选区，以免替换范围与快照不同。
        if let ranges = attribute(kAXSelectedTextRangesAttribute) as? [AnyObject], ranges.count > 1 {
            throw TextInsertService.InsertError.targetUnavailable
        }
        let snapshot = TextInsertionSnapshot(value: value, range: NSRange(location: range.location, length: range.length))
        if range.length > 0, let selected = attribute(kAXSelectedTextAttribute) as? String,
           snapshot.selectedText != selected {
            throw TextInsertService.InsertError.targetUnavailable
        }
        return snapshot
    }

    func replaceSelectedText(_ text: String) throws {
        guard AXIsProcessTrusted() else { throw TextInsertService.InsertError.accessibilityPermissionDenied }
        var settable = DarwinBoolean(false)
        guard AXUIElementIsAttributeSettable(element, kAXSelectedTextAttribute as CFString, &settable) == .success,
              settable.boolValue else { throw TextInsertService.InsertError.targetUnavailable }
        guard AXUIElementSetAttributeValue(element, kAXSelectedTextAttribute as CFString, text as CFString) == .success else {
            throw TextInsertService.InsertError.insertionNotVerified
        }
    }

    private func attribute(_ name: String) -> AnyObject? {
        var value: CFTypeRef?
        guard AXUIElementCopyAttributeValue(element, name as CFString, &value) == .success else { return nil }
        return value
    }

    private static func focusedElement() -> AXUIElement? {
        var value: CFTypeRef?
        guard AXUIElementCopyAttributeValue(
            AXUIElementCreateSystemWide(), kAXFocusedUIElementAttribute as CFString, &value
        ) == .success, let value, CFGetTypeID(value) == AXUIElementGetTypeID() else { return nil }
        return unsafeDowncast(value, to: AXUIElement.self)
    }
}
