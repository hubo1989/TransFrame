// ResultWindow.hpp — translation result popup (screenshot flow v1).
//
// A borderless topmost window shown near the captured region: source text
// (OCR) on top, translated text below, copy button in the corner. Closes on
// Esc / focus loss / "Close". This is the v1 in-app result surface; the
// pinned overlay manager (task D) replaces it later.
#ifndef TF_RESULT_WINDOW_HPP
#define TF_RESULT_WINDOW_HPP

#include "tf/CoreTypes.hpp"

#include <memory>
#include <string>

namespace tf {

struct ResultWindowContent {
    std::wstring sourceText;      // OCR full text (may be empty)
    std::wstring translatedText;  // final translation (may show an error)
    std::wstring engineLabel;     // e.g. "MTransServer"
    std::wstring statusLine;      // e.g. "OCR: Windows OCR · 68ms | Engine: 214ms"
    Rect anchorPhysical;          // captured region (physical px) — popup placed below
    /// Captured pixels for the FR-07 preview window. Null hides the
    /// "预览" button (non-screenshot callers stay unchanged).
    std::shared_ptr<PixelBuffer> previewImage;
};

class ResultWindowController {
public:
    ResultWindowController();
    ~ResultWindowController();

    ResultWindowController(const ResultWindowController&) = delete;
    ResultWindowController& operator=(const ResultWindowController&) = delete;

    /// Register the window class (once per process).
    static bool registerClass(HINSTANCE instance);

    /// Create (or reuse) the popup and fill it. Shows async on the caller's
    /// (main) thread; content is copied.
    bool show(const ResultWindowContent& content);

    void close();

    bool isVisible() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tf

#endif // TF_RESULT_WINDOW_HPP
