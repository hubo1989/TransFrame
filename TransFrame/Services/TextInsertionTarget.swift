import AppKit
import ApplicationServices
import os

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
    /// 粘贴兜底：以剪贴板 + Cmd+V 替换当前选区。
    /// 返回恢复剪贴板的闭包，调用方必须在读回验证结束后调用。
    func replaceSelectedTextByPasting(_ text: String) async throws -> @MainActor () -> Void
}

/// AX 引用仅在主线程使用；快照在捕获时确定，禁止翻译结束后重新选择目标。
@MainActor
final class CapturedTextInsertion {
    let selectedText: String
    private let target: any TextInsertionTarget
    private let selection: TextInsertionSnapshot
    private let expectedClipboardText: String
    private static let logger = Logger.textInsertion

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
        guard let captured = try? CapturedTextInsertion(
            target: target, selection: selection,
            selectedText: selectedText, expectedClipboardText: clipboardText
        ) else {
            Self.logger.notice("绑定失败: 快照selectedText为空或不一致 clipLen=\(clipboardText.utf16.count, privacy: .public)")
            return nil
        }
        return captured
    }

    /// 替换捕获时的选区。优先 AX 选区替换；写入未生效时，在目标未变的前提下
    /// 回退为「剪贴板 + Cmd+V」粘贴。任何路径失败都不重复写入。
    func replace(with text: String) async throws {
        try Task.checkCancellation()
        guard !text.isEmpty else { throw TextInsertService.InsertError.insertionNotVerified }
        guard selectedText == expectedClipboardText,
              target.isFocused, try target.snapshot() == selection else {
            Self.logger.notice("插入失败: 目标已变化 focus=\(self.target.isFocused) clipMatch=\(self.selectedText == self.expectedClipboardText)")
            throw TextInsertService.InsertError.targetChanged
        }
        let expected = (selection.value as NSString).replacingCharacters(in: selection.range, with: text)

        // 第一路径：单次 AX 替换不单独发送 Delete；失败也可能已生效，必须读回后再判断。
        var axError: Error?
        do {
            try target.replaceSelectedText(text)
        } catch {
            axError = error
        }
        if let after = try? target.snapshot(), Self.valuesMatch(after.value, expected) {
            Self.logger.notice("插入成功: AX写入并验证通过")
            return
        }

        // AX 未生效才允许粘贴兜底；已有任何改写都 fail closed，绝不叠加重试。
        let afterAX = try? target.snapshot()
        guard let after = afterAX, after.value == selection.value, after.range == selection.range else {
            Self.logger.notice("插入失败: AX后目标已被部分改写 nowLen=\(afterAX?.value.utf16.count ?? -1, privacy: .public)")
            throw axError ?? TextInsertService.InsertError.insertionNotVerified
        }
        if let axError = axError as? TextInsertService.InsertError,
           case .accessibilityPermissionDenied = axError {
            throw axError
        }

        // 第二路径：粘贴兜底。粘贴前再次确认目标未变，防止译文打进错误位置。
        try Task.checkCancellation()
        guard target.isFocused, try target.snapshot() == selection else {
            Self.logger.notice("插入失败: 粘贴前复核目标已变化 focus=\(self.target.isFocused)")
            throw TextInsertService.InsertError.targetChanged
        }
        Self.logger.notice("AX未生效(空操作) → 粘贴兜底")
        let restorePasteboard = try await target.replaceSelectedTextByPasting(text)
        defer { restorePasteboard() }
        // 粘贴生效可能有延迟，轮询读回直至确认。
        for poll in 1...10 {
            try await Task.sleep(nanoseconds: 80_000_000)
            guard let current = try? target.snapshot() else { continue }
            if Self.valuesMatch(current.value, expected) {
                Self.logger.notice("插入成功: 粘贴兜底验证通过 poll=\(poll, privacy: .public)")
                return
            }
            if current.value != selection.value {
                Self.logger.notice("插入失败: 粘贴后目标被改写但结果不符 nowLen=\(current.value.utf16.count, privacy: .public)")
                break
            }
        }
        Self.logger.notice("插入失败: 粘贴后读回始终未变化（粘贴可能被应用忽略）")
        throw TextInsertService.InsertError.insertionNotVerified
    }

    /// 严格相等，或仅换行风格（CRLF/CR 与 LF）不同也算生效，避免应用归一化导致误报。
    private static func valuesMatch(_ value: String, _ expected: String) -> Bool {
        value == expected
            || normalizedNewlines(value) == normalizedNewlines(expected)
    }

    private static func normalizedNewlines(_ text: String) -> String {
        text.replacingOccurrences(of: "\r\n", with: "\n").replacingOccurrences(of: "\r", with: "\n")
    }
}

@MainActor
final class AXTextInsertionTarget: TextInsertionTarget {
    private let element: AXUIElement
    private let processID: pid_t
    /// 元素是否经焦点窗口遍历发现（此时无 AX 焦点可核验，isFocused 退化为前台进程匹配，
    /// 替换安全性仍由快照一致性与剪贴板一致性闸门保证）。
    private let discoveredByWalking: Bool
    private static let logger = Logger.textInsertion

    private init(element: AXUIElement, processID: pid_t, discoveredByWalking: Bool = false) {
        self.element = element
        self.processID = processID
        self.discoveredByWalking = discoveredByWalking
    }

    static func capture() -> CapturedTextInsertion? {
        guard AXIsProcessTrusted() else {
            logger.notice("capture失败: AX未受信任")
            return nil
        }
        guard let application = NSWorkspace.shared.frontmostApplication else {
            logger.notice("capture失败: 无前台应用")
            return nil
        }
        let front = application.bundleIdentifier ?? "?"
        let axApp = AXUIElementCreateApplication(application.processIdentifier)

        // 焦点元素三层获取：per-app → system-wide → 焦点窗口树遍历（唯一带选区的文本控件）。
        // 锁屏后 system-wide 可能长期返回 -25204 且无应用内恢复手段，绝不能只依赖它。
        var element: AXUIElement?
        var walking = false
        for attempt in 0...3 {
            if let e = focusedElementViaApp(axApp) {
                element = e
                if attempt > 0 { logger.notice("focus: 重试\(attempt)后 per-app 成功 front=\(front, privacy: .public)") }
                break
            }
            if let e = focusedElementViaSystemWide() {
                element = e
                logger.notice("focus: per-app失败，system-wide成功 front=\(front, privacy: .public)")
                break
            }
            if let e = discoverSoleTextElementWithSelection(axApp) {
                element = e
                walking = true
                logger.notice("focus: 前两层失败，窗口遍历发现唯一选中文本控件 front=\(front, privacy: .public)")
                break
            }
            if attempt < 3 {
                logger.notice("focus: 三层均失败 attempt=\(attempt, privacy: .public) front=\(front, privacy: .public)")
                Thread.sleep(forTimeInterval: 0.15)
            }
        }
        guard let element else {
            logger.notice("capture失败: 无法获取焦点文本元素（详见focus日志）")
            return nil
        }
        var pid: pid_t = 0
        guard AXUIElementGetPid(element, &pid) == .success else {
            logger.notice("capture失败: 无法获取焦点元素pid")
            return nil
        }
        guard pid == application.processIdentifier else {
            logger.notice("capture失败: 焦点元素pid=\(Int(pid), privacy: .public)与前台应用pid=\(application.processIdentifier, privacy: .public)(\(front, privacy: .public))不一致")
            return nil
        }
        let target = AXTextInsertionTarget(element: element, processID: pid, discoveredByWalking: walking)
        guard let selection = try? target.snapshot() else {
            logger.notice("capture失败: snapshot不可读（原因见snapshot日志）")
            return nil
        }
        guard let text = selection.selectedText, !text.isEmpty else {
            logger.notice("capture失败: 无选中文本 valueLen=\(selection.value.utf16.count, privacy: .public) rangeLoc=\(selection.range.location) rangeLen=\(selection.range.length)")
            return nil
        }
        guard target.isFocused else {
            logger.notice("capture失败: 目标已非焦点")
            return nil
        }
        do {
            let captured = try CapturedTextInsertion(target: target, selection: selection, selectedText: text)
            logger.notice("capture成功: front=\(front, privacy: .public) walk=\(walking) valueLen=\(selection.value.utf16.count, privacy: .public) selectedLen=\(text.utf16.count, privacy: .public)")
            return captured
        } catch {
            logger.notice("capture失败: 快照校验不通过 selectedLen=\(text.utf16.count, privacy: .public)")
            return nil
        }
    }

    var isFocused: Bool {
        guard NSWorkspace.shared.frontmostApplication?.processIdentifier == processID else { return false }
        // 遍历发现的元素没有 AX 焦点可核验，退化为前台进程匹配。
        if discoveredByWalking { return true }
        guard let focused = Self.focusedElement() else { return false }
        return CFEqual(focused, element)
    }

    func snapshot() throws -> TextInsertionSnapshot {
        guard let value = attribute(kAXValueAttribute) as? String,
              let rangeObject = attribute(kAXSelectedTextRangeAttribute),
              CFGetTypeID(rangeObject) == AXValueGetTypeID() else {
            Self.logger.notice("snapshot失败: kAXValue/kAXSelectedTextRange不可读")
            throw TextInsertService.InsertError.targetUnavailable
        }
        let rangeValue = unsafeDowncast(rangeObject, to: AXValue.self)
        var range = CFRange()
        guard AXValueGetType(rangeValue) == .cfRange,
              AXValueGetValue(rangeValue, .cfRange, &range) else {
            Self.logger.notice("snapshot失败: range值类型不符")
            throw TextInsertService.InsertError.targetUnavailable
        }
        // 不支持多个不连续选区，以免替换范围与快照不同。
        if let ranges = attribute(kAXSelectedTextRangesAttribute) as? [AnyObject], ranges.count > 1 {
            Self.logger.notice("snapshot失败: 多个不连续选区 count=\(ranges.count, privacy: .public)")
            throw TextInsertService.InsertError.targetUnavailable
        }
        let snapshot = TextInsertionSnapshot(value: value, range: NSRange(location: range.location, length: range.length))
        if range.length > 0, let selected = attribute(kAXSelectedTextAttribute) as? String,
           snapshot.selectedText != selected {
            Self.logger.notice("snapshot失败: AXSelectedText与value子串不一致 axLen=\(selected.utf16.count, privacy: .public) subLen=\(snapshot.selectedText?.utf16.count ?? -1, privacy: .public)")
            throw TextInsertService.InsertError.targetUnavailable
        }
        return snapshot
    }

    func replaceSelectedText(_ text: String) throws {
        guard AXIsProcessTrusted() else { throw TextInsertService.InsertError.accessibilityPermissionDenied }
        var settable = DarwinBoolean(false)
        let probe = AXUIElementIsAttributeSettable(element, kAXSelectedTextAttribute as CFString, &settable)
        guard probe == .success, settable.boolValue else {
            Self.logger.notice("AX写入: kAXSelectedText settable=false probeRc=\(probe.rawValue, privacy: .public)")
            throw TextInsertService.InsertError.targetUnavailable
        }
        let rc = AXUIElementSetAttributeValue(element, kAXSelectedTextAttribute as CFString, text as CFString)
        guard rc == .success else {
            Self.logger.notice("AX写入: set返回错误 rc=\(rc.rawValue, privacy: .public)")
            throw TextInsertService.InsertError.insertionNotVerified
        }
        Self.logger.notice("AX写入: set返回success textLen=\(text.utf16.count, privacy: .public)")
    }

    /// 通过「剪贴板 + Cmd+V」替换当前选区。返回恢复剪贴板的闭包，
    /// 调用方必须在读回验证结束后再调用，避免目标应用延迟消费剪贴板时拿到旧内容。
    func replaceSelectedTextByPasting(_ text: String) async throws -> @MainActor () -> Void {
        guard AXIsProcessTrusted() else { throw TextInsertService.InsertError.accessibilityPermissionDenied }
        let backup = PasteboardBackup.save()
        let pasteboard = NSPasteboard.general
        pasteboard.clearContents()
        guard pasteboard.setString(text, forType: .string) else {
            PasteboardBackup.restore(backup)
            throw TextInsertService.InsertError.eventCreationFailed
        }
        guard let source = CGEventSource(stateID: .hidSystemState),
              let keyDown = CGEvent(keyboardEventSource: source, virtualKey: 9, keyDown: true), // kVK_ANSI_V
              let keyUp = CGEvent(keyboardEventSource: source, virtualKey: 9, keyDown: false) else {
            PasteboardBackup.restore(backup)
            throw TextInsertService.InsertError.eventCreationFailed
        }
        keyDown.flags = .maskCommand
        keyUp.flags = .maskCommand
        keyDown.post(tap: .cghidEventTap)
        keyUp.post(tap: .cghidEventTap)
        return { PasteboardBackup.restore(backup) }
    }

    private func attribute(_ name: String) -> AnyObject? {
        var value: CFTypeRef?
        guard AXUIElementCopyAttributeValue(element, name as CFString, &value) == .success else { return nil }
        return value
    }

    private static func focusedElement() -> AXUIElement? {
        if let front = NSWorkspace.shared.frontmostApplication,
           let e = focusedElementViaApp(AXUIElementCreateApplication(front.processIdentifier)) {
            return e
        }
        return focusedElementViaSystemWide()
    }

    private static func focusedElementViaApp(_ axApp: AXUIElement) -> AXUIElement? {
        var v: CFTypeRef?
        guard AXUIElementCopyAttributeValue(axApp, kAXFocusedUIElementAttribute as CFString, &v) == .success,
              let v, CFGetTypeID(v) == AXUIElementGetTypeID() else { return nil }
        return unsafeDowncast(v, to: AXUIElement.self)
    }

    private static func focusedElementViaSystemWide() -> AXUIElement? {
        var v: CFTypeRef?
        guard AXUIElementCopyAttributeValue(AXUIElementCreateSystemWide(), kAXFocusedUIElementAttribute as CFString, &v) == .success,
              let v, CFGetTypeID(v) == AXUIElementGetTypeID() else { return nil }
        return unsafeDowncast(v, to: AXUIElement.self)
    }

    /// 前两层拿不到焦点元素时的兜底：在焦点窗口树中寻找「恰好一个带选区的文本控件」。
    /// 仅接受唯一候选，避免绑到错误控件；后续仍受快照与剪贴板一致性闸门约束。
    private static func discoverSoleTextElementWithSelection(_ axApp: AXUIElement) -> AXUIElement? {
        var wv: CFTypeRef?
        guard AXUIElementCopyAttributeValue(axApp, kAXFocusedWindowAttribute as CFString, &wv) == .success,
              let window = wv, CFGetTypeID(window) == AXUIElementGetTypeID() else { return nil }
        var candidates: [AXUIElement] = []
        func walk(_ el: AXUIElement, _ depth: Int) {
            guard depth <= 6, candidates.count <= 1 else { return }
            if let role = copyAttribute(el, kAXRoleAttribute) as? String,
               role == "AXTextArea" || role == "AXTextField",
               hasNonEmptySelection(el) {
                candidates.append(el)
            }
            if let kids = copyAttribute(el, kAXChildrenAttribute) as? [AnyObject] {
                for k in kids where CFGetTypeID(k) == AXUIElementGetTypeID() {
                    walk(unsafeDowncast(k, to: AXUIElement.self), depth + 1)
                }
            }
        }
        walk(unsafeDowncast(window, to: AXUIElement.self), 0)
        return candidates.count == 1 ? candidates[0] : nil
    }

    private static func hasNonEmptySelection(_ el: AXUIElement) -> Bool {
        guard copyAttribute(el, kAXValueAttribute) is String,
              let rv = copyAttribute(el, kAXSelectedTextRangeAttribute),
              CFGetTypeID(rv) == AXValueGetTypeID() else { return false }
        let axv = unsafeDowncast(rv, to: AXValue.self)
        var range = CFRange()
        return AXValueGetType(axv) == .cfRange && AXValueGetValue(axv, .cfRange, &range) && range.length > 0
    }

    private static func copyAttribute(_ element: AXUIElement, _ name: String) -> AnyObject? {
        var v: CFTypeRef?
        guard AXUIElementCopyAttributeValue(element, name as CFString, &v) == .success else { return nil }
        return v
    }
}
