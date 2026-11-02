#pragma once
// Interfaces.h — pure virtual service contracts for the Windows app.
// Core defines the seams; NativeShell (task B) and the services layer
// (task C) provide implementations:
//
//   IOcrEngine            <- WindowsOcrEngine (WinRT), PaddleOCR client   [C]
//   ITranslationProvider  <- 9 engine adapters (no apple on Windows)      [C]
//   IHttpTransport        <- WinHttp transport                            [C]
//   ICredentialStore      <- Credential Manager store                    [C]
//   IHotkeyListener       <- RegisterHotKey listener                     [B]
//   IClipboard            <- Win32 clipboard service                     [B]
//   IHistoryStore         <- JSON-file backed store (settings container) [A core]
//
// All interfaces are platform-clean (no Win32/WinRT includes) so the Catch2
// unit tests can drive them with in-memory fakes on any host.

#include "CaptureDocument.h"
#include "LLMTranslationCore.h"
#include "TranslationResultBundle.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tf::core {

// ------------------------------------------------------------- image input --
/// Cross-platform image handle for OCR input: BGRA8 bytes + size (top-down).
struct OcrImage {
    std::int64_t width = 0;
    std::int64_t height = 0;
    std::string bgra;  // width*height*4 bytes
};

// ------------------------------------------------------------- OCR engine --
/// OCR engine contract (Swift AnyOCREngine). recognize() returns segments
/// with normalized bboxes (top-left origin) + confidence.
class IOcrEngine {
public:
    virtual ~IOcrEngine() = default;

    /// Stable engine identifier ("windows", "paddleocr").
    virtual std::string id() const = 0;
    /// Human-readable name for settings UI.
    virtual std::string name() const = 0;
    /// Configured + language pack/model present. Never performs I/O.
    virtual bool isAvailable() const = 0;
    /// Runs OCR on one image.
    virtual OCRResult recognize(const OcrImage& image) = 0;
};

// ----------------------------------------------------- translation provider --
/// Translation request (Swift translate(texts:from:to:) batch form).
struct TranslateRequest {
    std::vector<std::string> texts;
    /// Source language code; empty/"" = auto-detect.
    std::string sourceLanguage;
    std::string targetLanguage;
    /// Resolved prompt template (TranslationPromptConfig::resolvedPrompt).
    std::string promptTemplate;
    /// Engine-specific max output tokens (LLM engines).
    std::int64_t maxOutputTokens = 0;  // 0 = engine default
};

/// Translation provider contract (Swift TranslationProvider). Implementations
/// wrap one engine; batch translate preserves input order.
class ITranslationProvider {
public:
    virtual ~ITranslationProvider() = default;

    virtual std::string id() const = 0;
    virtual std::string name() const = 0;
    virtual TranslationEngineType engineType() const = 0;
    /// Configured + credentials present (never performs I/O).
    virtual bool isAvailable() const = 0;

    /// Batch translate. On failure returns an EngineError; per-text failures
    /// are reported as one error (same as Swift throws).
    /// On success results[i] corresponds to request.texts[i].
    virtual std::optional<EngineError> translate(const TranslateRequest& request,
                                                 std::vector<std::string>& outTranslations) = 0;

    /// Lightweight connectivity probe (models list / ping). MUST NOT call
    /// billed translation endpoints. Returns nullopt = no free probe/OK.
    virtual std::optional<EngineError> verifyConnection() = 0;
};

// ---------------------------------------------------------------- hotkeys --
/// Global hotkey listener (macOS Carbon RegisterEventHotKey analogue;
/// Windows: RegisterHotKey + message-only window, task B).
class IHotkeyListener {
public:
    virtual ~IHotkeyListener() = default;

    /// Registers one shortcut; returns false when the combination is already
    /// taken by another app.
    virtual bool registerShortcut(const std::string& actionId, const KeyboardShortcut& shortcut) = 0;
    virtual void unregisterShortcut(const std::string& actionId) = 0;
    /// Polls/next fired shortcut (event-driven in the real impl; the pure
    /// contract keeps a pull API so tests can drive it).
    /// Returns nullopt when no event is pending.
    virtual std::optional<std::string> nextFiredAction() = 0;
};

// -------------------------------------------------------------- clipboard --
/// Clipboard service (Swift ClipboardService; Win32 GetClipboardData under
/// the hood, task B).
class IClipboard {
public:
    virtual ~IClipboard() = default;

    /// Copies PNG bytes to the clipboard.
    virtual bool copyImage(const std::vector<std::uint8_t>& pngBytes) = 0;
    /// Copies UTF-8 text.
    virtual bool copyText(const std::string& text) = 0;
    /// Reads UTF-8 text; nullopt when the clipboard has no text.
    virtual std::optional<std::string> readText() = 0;
    virtual bool hasImage() const = 0;
};

// ------------------------------------------------------------ history store --
/// History persistence (Swift HistoryStore; UserDefaults-backed on macOS,
/// ApplicationData JSON on Windows — same "TransFrame.translationHistory"
/// payload). Max 50 entries (HistoryStore.maxHistoryEntries).
class IHistoryStore {
public:
    virtual ~IHistoryStore() = default;

    virtual std::vector<TranslationHistory> entries() const = 0;
    /// Inserts newest-first; removes same-content duplicates; enforces the cap.
    virtual void add(const TranslationHistory& entry) = 0;
    virtual void remove(const Uuid& id) = 0;
    virtual void toggleFavorite(const Uuid& id) = 0;
    virtual void save() = 0;
    virtual void reload() = 0;
};

// -------------------------------------------------------- credential store --
/// Credential storage abstraction (Swift KeychainService; Windows Credential
/// Manager, task C). Account names follow "TransFrame/<engine>/<instance>".
class ICredentialStore {
public:
    virtual ~ICredentialStore() = default;

    virtual void save(const std::string& account, const std::string& secret) = 0;
    virtual std::optional<std::string> load(const std::string& account) const = 0;
    virtual bool remove(const std::string& account) = 0;
    virtual bool has(const std::string& account) const = 0;
};

using OcrEnginePtr = std::unique_ptr<IOcrEngine>;
using TranslationProviderPtr = std::unique_ptr<ITranslationProvider>;
using HotkeyListenerPtr = std::unique_ptr<IHotkeyListener>;
using ClipboardPtr = std::unique_ptr<IClipboard>;
using HistoryStorePtr = std::unique_ptr<IHistoryStore>;
using CredentialStorePtr = std::shared_ptr<ICredentialStore>;

}  // namespace tf::core
