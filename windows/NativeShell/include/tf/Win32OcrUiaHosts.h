// tf/Win32OcrUiaHosts.h — NativeShell seams for WindowsOcrEngine (WinRT),
// PaddleOcrEngine (CreateProcess), and TextExtractorHost (UIA + clipboard +
// SendInput). Windows-only compilation; NOT RUN on this macOS host.
#ifndef TF_WIN32_OCR_UIA_HOSTS_H
#define TF_WIN32_OCR_UIA_HOSTS_H

#ifdef _WIN32

#include "tf/OcrEngines.hpp"
#include "tf/TextSelectionInsertion.hpp"

#include <functional>

namespace tf {

/// WinRT glue: SoftwareBitmap from BGRA bytes → OcrEngine::RecognizeAsync →
/// RawLine[] (pixel rects, top-left origin; Windows OCR exposes no
/// confidence → 0 → mapped to 1.0). Language inventory from
/// OcrEngine::AvailableRecognizerLanguages.
WindowsOcrEngine::LanguageProvider MakeWinRtLanguageProvider();
WindowsOcrEngine::Recognizer MakeWinRtRecognizer();

/// CreateProcess runner for PaddleOCR (argv-style, no shell; pipes for
/// stdout/stderr; hard timeout kill).
PaddleOcrEngine::ProcessRunner MakeWin32ProcessRunner();

/// Filesystem availability probe: executable exists + `--version` exit 0
/// (Swift PaddleOCRChecker.performFullCheck shape).
PaddleOcrEngine::AvailabilityChecker MakeWin32AvailabilityChecker();

/// UIA + clipboard + SendInput host for TextExtractor / InsertionService.
/// - focusedElement: IUIAutomation::GetFocusedElement → TextPattern wrapper
/// - clipboard: OpenClipboard/EmptyClipboard/SetClipboardData(CF_UNICODETEXT)
///   + GetClipboardSequenceNumber for change detection
/// - copy/paste: SendInput VK_CONTROL+VK_'C'/'V'
class Win32TextExtractorHost final : public TextExtractorHost {
public:
    Win32TextExtractorHost();
    ~Win32TextExtractorHost() override;

    ClipboardBackup backupClipboard() override;
    void restoreClipboard(const ClipboardBackup& backup) override;
    void clearClipboard() override;
    std::int64_t setClipboardText(const std::string& text) override;
    std::int64_t clipboardSequenceNumber() override;
    std::optional<std::string> clipboardText() override;
    bool sendCopyShortcut() override;
    bool sendPasteShortcut() override;
    std::string foregroundProcessName() override;
    std::uint32_t foregroundProcessId() override;
    void sleepMilliseconds(int ms) override;

    // Focused-element seam (settable so tests can inject).
    std::function<UiaElementPtr()> focusedElement;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace tf

#endif  // _WIN32
#endif  // TF_WIN32_OCR_UIA_HOSTS_H
