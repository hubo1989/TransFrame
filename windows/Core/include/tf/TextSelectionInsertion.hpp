// tf/TextSelectionInsertion.hpp
// 划词取文本 + 翻译并插入 (FR-28/29): protocol logic in Core, system calls in
// NativeShell. Mirrors the macOS safety semantics from TextSelectionService /
// TextInsertService / AXTextInsertionTarget:
//   1. UIA TextPattern GetSelection first (no clipboard touched).
//   2. Clipboard fallback: save → clear → Ctrl+C → poll → restore.
//   3. Insert: re-validate the target selection still equals the captured
//      text; ONLY then SendInput Ctrl+V (paste), never blind typing.
//      Re-validation failure aborts — 绝不粘贴 (brief §6).
#ifndef TF_TEXT_SELECTION_INSERTION_HPP
#define TF_TEXT_SELECTION_INSERTION_HPP

#include "tf/CoreModels.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tf {

/// Captured selection + the identity of the editable target (macOS
/// CapturedTextInsertion / TextSelectionResult).
struct TextSelection {
    std::string text;
    std::string sourceApplication;  // exe name when known
    std::uint32_t sourceProcessId = 0;
    /// Non-empty when a UIA editable target was bound (supports re-validation
    /// before paste). Empty in clipboard-fallback mode.
    std::string insertionTargetId;
};

/// UIA abstraction over the focused element (mock in tests; NativeShell
/// implements via IUIAutomation TextPattern/ValuePattern).
class IUiaElement {
public:
    virtual ~IUiaElement() = default;
    /// Selected text via TextPattern::GetSelection; nullopt = pattern absent.
    virtual std::optional<std::string> getSelectionText() const = 0;
    /// Whether the element exposes an editable ValuePattern.
    virtual bool isEditable() const = 0;
    /// Stable identity for re-validation (RuntimeId / HWND+pattern combo).
    virtual std::string runtimeId() const = 0;
};

using UiaElementPtr = std::unique_ptr<IUiaElement>;

/// System seams implemented by NativeShell (Win32/UIA/SendInput).
struct TextExtractorHost {
    /// Focused element (UIA) or nullptr when UIA is unavailable.
    std::function<UiaElementPtr()> focusedElement;
    /// Full clipboard snapshot: formats + data, restorable (Swift
    /// PasteboardBackup.save/restore).
    struct ClipboardBackup {
        std::string text;              // CF_UNICODETEXT payload (may be empty)
        std::string raw;               // serialized bytes of other formats
        std::int64_t sequenceNumber = 0;
    };
    virtual ~TextExtractorHost() = default;
    virtual ClipboardBackup backupClipboard() = 0;
    virtual void restoreClipboard(const ClipboardBackup& backup) = 0;
    virtual void clearClipboard() = 0;
    /// Sets text and returns the new sequence number.
    virtual std::int64_t setClipboardText(const std::string& text) = 0;
    virtual std::int64_t clipboardSequenceNumber() = 0;
    virtual std::optional<std::string> clipboardText() = 0;
    /// Sends Ctrl+C / Ctrl+V via SendInput.
    virtual bool sendCopyShortcut() = 0;
    virtual bool sendPasteShortcut() = 0;
    /// Foreground process info for anchoring.
    virtual std::string foregroundProcessName() = 0;
    virtual std::uint32_t foregroundProcessId() = 0;
    /// Sleep helper (ms) so tests can run without real delays.
    virtual void sleepMilliseconds(int ms) = 0;
};

/// Extraction outcome (Swift CaptureError mapped onto error categories).
struct ExtractionOutcome {
    std::optional<TextSelection> selection;
    std::optional<ProviderError> error;

    static ExtractionOutcome ok(TextSelection s) {
        return ExtractionOutcome{std::move(s), std::nullopt};
    }
    static ExtractionOutcome fail(ProviderError e) {
        return ExtractionOutcome{std::nullopt, std::move(e)};
    }
};

/// FR-28: UIA-first text extraction with clipboard fallback.
class TextExtractor {
public:
    /// allowClipboardFallback: 设置可关 (Swift TextSelectionService seam).
    TextExtractor(TextExtractorHost& host, bool allowClipboardFallback = true,
                  int clipboardPollRetries = 20, int clipboardPollIntervalMs = 100);

    /// Captures the current selection. bindForInsertion additionally binds
    /// the UIA editable target for later re-validation.
    ExtractionOutcome captureSelectedText(bool bindForInsertion = false);

    bool canCapture() const { return host_.focusedElement != nullptr; }

private:
    TextExtractorHost& host_;
    bool allowClipboardFallback_;
    int clipboardPollRetries_;
    int clipboardPollIntervalMs_;
};

/// FR-29 outcome states (macOS TextInsertionProgressController tri-state).
enum class InsertionOutcome { InProgress, Completed, Failed };

struct InsertRequest {
    std::string translatedText;
    TextSelection capturedSelection;  // anchor captured at trigger time
};

/// FR-29: translate-and-insert with mandatory re-validation.
class InsertionService {
public:
    explicit InsertionService(TextExtractorHost& host);

    /// Re-validates the target then pastes. Steps:
    ///   1. focused element must exist and match capturedSelection.insertionTargetId
    ///      (or foreground PID anchor in fallback mode);
    ///   2. current selection must still equal capturedSelection.text
    ///      (UIA read-back; Swift "目标重验证");
    ///   3. setClipboardText(translated) → sendPasteShortcut();
    ///   4. restore the original clipboard after the paste window.
    /// Any mismatch → abort with TargetChanged; 绝不粘贴 (brief §6).
    InsertionOutcome insert(const InsertRequest& request, std::string& outError);

    /// Pure re-validation logic, exposed for tests with a mock element.
    /// Returns nullopt when validation passes; error text otherwise.
    static std::optional<std::string> validateTarget(const IUiaElement* focused,
                                                     const TextSelection& captured,
                                                     const std::string& currentSelectionText,
                                                     std::uint32_t foregroundPid);

private:
    TextExtractorHost& host_;
};

}  // namespace tf

#endif  // TF_TEXT_SELECTION_INSERTION_HPP
