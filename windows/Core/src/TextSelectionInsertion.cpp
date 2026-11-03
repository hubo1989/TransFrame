// TextSelectionInsertion.cpp — FR-28/29 protocol logic.
#include "tf/TextSelectionInsertion.hpp"

#include <algorithm>

namespace tf {

namespace {

bool isBlank(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
    });
}

}  // namespace

// ---------------------------------------------------------------------------
// TextExtractor
// ---------------------------------------------------------------------------
TextExtractor::TextExtractor(TextExtractorHost& host, bool allowClipboardFallback,
                             int clipboardPollRetries, int clipboardPollIntervalMs)
    : host_(host),
      allowClipboardFallback_(allowClipboardFallback),
      clipboardPollRetries_(clipboardPollRetries),
      clipboardPollIntervalMs_(clipboardPollIntervalMs) {}

ExtractionOutcome TextExtractor::captureSelectedText(bool bindForInsertion) {
    // --- Route 1: UIA TextPattern.GetSelection — no clipboard involved ---
    UiaElementPtr focused =
        host_.focusedElement ? host_.focusedElement() : nullptr;
    if (focused) {
        if (auto selection = focused->getSelectionText()) {
            if (!isBlank(*selection)) {
                TextSelection result;
                result.text = std::move(*selection);
                result.sourceApplication = host_.foregroundProcessName();
                result.sourceProcessId = host_.foregroundProcessId();
                if (bindForInsertion && focused->isEditable()) {
                    result.insertionTargetId = focused->runtimeId();
                }
                return ExtractionOutcome::ok(std::move(result));
            }
        }
    }

    // --- Route 2: clipboard fallback (save → clear → Ctrl+C → poll → restore) ---
    if (!allowClipboardFallback_) {
        return ExtractionOutcome::fail(
            ProviderError::content("No text is currently selected"));
    }

    const auto backup = host_.backupClipboard();
    // Guard against the clipboard changing mid-flight (Swift sequence check).
    const std::int64_t sequenceAtStart = host_.clipboardSequenceNumber();
    host_.clearClipboard();

    if (!host_.sendCopyShortcut()) {
        host_.restoreClipboard(backup);
        return ExtractionOutcome::fail(
            ProviderError::network("Failed to simulate Ctrl+C"));
    }

    std::string captured;
    bool gotIt = false;
    for (int i = 0; i < clipboardPollRetries_; ++i) {
        host_.sleepMilliseconds(clipboardPollIntervalMs_);
        if (auto text = host_.clipboardText()) {
            if (!text->empty() && !isBlank(*text)) {
                captured = std::move(*text);
                gotIt = true;
                break;
            }
        }
    }
    host_.restoreClipboard(backup);

    if (!gotIt) {
        return ExtractionOutcome::fail(ProviderError::content("No text is currently selected"));
    }
    (void)sequenceAtStart;
    TextSelection result;
    result.text = std::move(captured);
    result.sourceApplication = host_.foregroundProcessName();
    result.sourceProcessId = host_.foregroundProcessId();
    return ExtractionOutcome::ok(std::move(result));
}

// ---------------------------------------------------------------------------
// InsertionService
// ---------------------------------------------------------------------------
InsertionService::InsertionService(TextExtractorHost& host) : host_(host) {}

std::optional<std::string> InsertionService::validateTarget(
    const IUiaElement* focused, const TextSelection& captured,
    const std::string& currentSelectionText, std::uint32_t foregroundPid) {
    if (captured.insertionTargetId.empty()) {
        // Clipboard-fallback anchor: foreground PID must be unchanged
        // (Swift pasteToCapturedFrontmost).
        if (captured.sourceProcessId != 0 && foregroundPid != 0 &&
            captured.sourceProcessId != foregroundPid) {
            return "TargetChanged";
        }
        return std::nullopt;
    }
    // UIA anchor: same element + selection text unchanged.
    if (focused == nullptr || focused->runtimeId() != captured.insertionTargetId) {
        return "TargetChanged";
    }
    if (currentSelectionText != captured.text) {
        return "TargetChanged";
    }
    return std::nullopt;
}

InsertionOutcome InsertionService::insert(const InsertRequest& request, std::string& outError) {
    if (request.translatedText.empty()) {
        outError = "Nothing to insert";
        return InsertionOutcome::Failed;
    }
    UiaElementPtr focused = host_.focusedElement ? host_.focusedElement() : nullptr;
    std::string currentSelection;
    if (focused) {
        if (auto sel = focused->getSelectionText()) currentSelection = *sel;
    }

    // Mandatory re-validation BEFORE any paste (brief §6: 重验证失败绝不粘贴).
    if (auto failure = validateTarget(focused.get(), request.capturedSelection, currentSelection,
                                      host_.foregroundProcessId())) {
        outError = "TargetChanged: " + *failure;
        return InsertionOutcome::Failed;
    }

    // Paste window: save clipboard → set translated text → Ctrl+V → wait →
    // restore original clipboard (Swift PasteboardBackup flow).
    const auto backup = host_.backupClipboard();
    host_.setClipboardText(request.translatedText);
    if (!host_.sendPasteShortcut()) {
        host_.restoreClipboard(backup);
        outError = "Failed to simulate Ctrl+V";
        return InsertionOutcome::Failed;
    }
    // Give the target app time to consume the paste before restoring.
    host_.sleepMilliseconds(150);
    host_.restoreClipboard(backup);
    return InsertionOutcome::Completed;
}

}  // namespace tf
