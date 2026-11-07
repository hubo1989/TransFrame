// tf/CoreModels.hpp
// Cross-platform Core models for the translation/OCR service layer (Task C).
// Schema mirrors macOS TransFrame/Models/*.swift one-to-one:
//   TranslationEngineType.swift, EngineSelectionV2.swift, TranslationScene.swift,
//   TranslationResult.swift, TranslationResultBundle.swift, BilingualSegment.swift,
//   ScreenAnalysisResult.swift (TextSegment), OCREngineType.swift (no vision →
//   windowsOcr), VLMProviderType.swift, AppSettings.swift (chain persistence).
// Platform-clean: no Win32/WinRT includes; Catch2 tests compile it directly.
#ifndef TF_CORE_MODELS_HPP
#define TF_CORE_MODELS_HPP

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tf {

// ---------------------------------------------------------------------------
// UUID (RFC 4122 v4, std::mt19937_64 seeded from random_device; string form is
// the interop format — settings JSON and chain items key on it, same as Swift
// CompatibleConfig.id / EngineChainItem.id).
// ---------------------------------------------------------------------------
struct Uuid {
    std::array<std::uint8_t, 16> bytes{};

    static Uuid random();
    /// Parses "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"; braces/urn prefix tolerated.
    static std::optional<Uuid> parse(const std::string& text);
    /// Nil UUID 00000000-0000-0000-0000-000000000000 (Swift CompatibleConfig.default id).
    static Uuid nil();

    bool isNil() const;
    std::string toString() const;
    bool operator==(const Uuid& o) const = default;
};

// ---------------------------------------------------------------------------
// Geo (normalized OCR space): doubles 0..1, top-left origin, Y grows down —
// same schema as macOS TextSegment.boundingBox / OCRText.boundingBox.
// ---------------------------------------------------------------------------
struct NormRect {
    double x = 0, y = 0, width = 0, height = 0;

    double minX() const { return x; }
    double minY() const { return y; }
    double maxX() const { return x + width; }
    double maxY() const { return y + height; }
    double midX() const { return x + width / 2; }
    double midY() const { return y + height / 2; }

    NormRect unionWith(const NormRect& o) const;
    bool operator==(const NormRect& o) const = default;
};

/// One text fragment with a normalized bbox + confidence (Swift TextSegment).
struct TextSegment {
    std::string id;          // UUID string
    std::string text;
    NormRect boundingBox;
    float confidence = 1.0f;
    bool isUserEdited = false;

    TextSegment() = default;
    TextSegment(std::string text_, NormRect box, float conf = 1.0f, bool edited = false);
    static TextSegment make(std::string text, NormRect box, float conf = 1.0f);
    bool isHighConfidence() const { return confidence > 0.7f; }
};

/// OCR output (Swift OCRResult): image size + observations sorted for fullText.
struct OcrResult {
    std::vector<TextSegment> observations;
    std::int64_t imageWidth = 0;   // physical px
    std::int64_t imageHeight = 0;  // physical px
    std::int64_t timestampMs = 0;  // epoch ms

    bool hasResults() const { return !observations.empty(); }
    std::string fullText() const;
    static OcrResult empty(std::int64_t w, std::int64_t h);
};

// ---------------------------------------------------------------------------
// Engines — NO apple (PRD §6.12: Apple Translation removed on Windows).
// Raw values must stay identical to the macOS enum strings ("mtran" etc.) so
// settings JSON is portable across platforms.
// ---------------------------------------------------------------------------
enum class TranslationEngineType : std::uint8_t {
    MtranServer,   // "mtran"
    OpenAI,        // "openai"
    Claude,        // "claude"
    Gemini,        // "gemini"
    Ollama,        // "ollama"
    Google,        // "google"
    DeepL,         // "deepl"
    Baidu,         // "baidu"
    Custom,        // "custom" (OpenAI-compatible instance)
};

/// All engines, settings-page order (Swift TranslationEngineType.uiCards minus apple).
std::vector<TranslationEngineType> allTranslationEngines();

const char* translationEngineRaw(TranslationEngineType t);      // "mtran" ...
std::optional<TranslationEngineType> translationEngineFromRaw(const std::string& raw);
const char* translationEngineDisplayName(TranslationEngineType t); // "MTranServer", "OpenAI", ...

/// Requires a stored API key (Swift TranslationEngineType.requiresAPIKey minus apple).
bool translationEngineRequiresApiKey(TranslationEngineType t);
/// Requires an AppID alongside the key (Baidu).
bool translationEngineRequiresAppId(TranslationEngineType t);
/// Default base URL (Swift defaultBaseURL minus apple).
std::string translationEngineDefaultBaseUrl(TranslationEngineType t);
/// Default model name (Swift defaultModelName minus apple).
std::string translationEngineDefaultModelName(TranslationEngineType t);

// ---------------------------------------------------------------------------
// Selection model V2 (Swift EngineSelectionV2.swift, verbatim semantics).
// ---------------------------------------------------------------------------
struct EngineChainItem {
    /// Kind: builtin engine or compatible-instance UUID.
    enum class Kind { Builtin, Compatible };
    Kind kind = Kind::Builtin;
    TranslationEngineType builtin = TranslationEngineType::MtranServer;
    std::string compatibleId;  // UUID string when kind == Compatible

    std::string id;            // unique entry identity (same engine may appear twice)

    static EngineChainItem makeBuiltin(TranslationEngineType t);
    static EngineChainItem makeCompatible(const std::string& uuid);
    /// The builtin engine this entry reports as (compatible → Custom, Swift resolvedEngine).
    TranslationEngineType resolvedEngine() const;
    bool operator==(const EngineChainItem& o) const = default;
};

/// Structural limits (Swift EngineSelectionLimits).
struct EngineSelectionLimits {
    static constexpr int orderedChainLimit = 5;
    static constexpr int compareEnginesLimit = 4;
    static constexpr int compareEnginesMinimum = 2;
};

enum class TranslationScene { Screenshot, TextSelection, TranslateAndInsert };
const char* translationSceneRaw(TranslationScene s);              // "screenshot" ...
std::optional<TranslationScene> translationSceneFromRaw(const std::string& raw);

// ---------------------------------------------------------------------------
// Prompt config (Swift TranslationPromptConfig.swift): three overlay layers
// engine < compatible-instance < scene; placeholder substitution shared with
// providers.
// ---------------------------------------------------------------------------
struct TranslationPromptConfig {
    std::vector<std::pair<TranslationEngineType, std::string>> enginePrompts;
    std::vector<std::pair<std::string, std::string>> compatibleEnginePrompts; // key: instance UUID
    std::vector<std::pair<TranslationScene, std::string>> scenePrompts;

    /// Default screenshot/text prompt (Swift TranslationPromptConfig.defaultPrompt).
    static const std::string& defaultPrompt();
    /// Insert-scene default: concise, no formatting (Swift defaultInsertPrompt).
    static const std::string& defaultInsertPrompt();

    /// Layered resolution exactly as Swift promptPreview: engine → compatible →
    /// scene → insert-scene default (TranslateAndInsert) → default.
    std::string resolve(TranslationEngineType engine,
                        TranslationScene scene,
                        const std::string& compatiblePromptId = "") const;

    /// {source_language}/{target_language}/{text} substitution.
    static std::string substitute(const std::string& templateText,
                                  const std::string& sourceLanguage,
                                  const std::string& targetLanguage,
                                  const std::string& text);
};

// ---------------------------------------------------------------------------
// Translation results (Swift TranslationResult / EngineResult / Bundle).
// ---------------------------------------------------------------------------
struct TranslationResult {
    std::string sourceText;
    std::string translatedText;
    std::string sourceLanguage;  // "Auto" when detected/absent (Swift behavior)
    std::string targetLanguage;

    TranslationResult() = default;
    TranslationResult(std::string src, std::string translated,
                      std::string srcLang, std::string tgtLang);
};

/// Bilingual pair bound to a TextSegment anchor (Swift BilingualSegment).
struct BilingualSegment {
    std::string id;
    TextSegment original;
    std::string translated;
    std::string sourceLanguage;
    std::string targetLanguage;

    BilingualSegment() = default;
    /// From a plain TranslationResult (empty bbox, confidence 1.0 — Swift init(from:)).
    static BilingualSegment fromResult(const TranslationResult& r);
};

/// One engine's outcome inside a chain/compare run (Swift EngineResult).
struct EngineResult {
    TranslationEngineType engine = TranslationEngineType::MtranServer;
    std::vector<BilingualSegment> segments;
    double latencySeconds = 0.0;
    bool failed = false;
    std::string errorText;      // short per-engine failure reason
    std::string displayName;    // compatible-instance display name for UI chips

    bool isSuccess() const { return !failed && !segments.empty(); }
    static EngineResult makeFailure(TranslationEngineType engine, std::string errorText,
                                    double latency = 0.0, std::string displayName = "");
};

/// Selection mode recorded on the bundle (Swift maps ordered/compare onto the
/// legacy EngineSelectionMode storage: ordered → PrimaryWithFallback,
/// compare → Parallel).
enum class EngineSelectionMode { PrimaryWithFallback, Parallel };

/// Whole-run outcome (Swift TranslationResultBundle).
struct TranslationResultBundle {
    std::vector<EngineResult> results;
    TranslationEngineType primaryEngine = TranslationEngineType::MtranServer;
    EngineSelectionMode selectionMode = EngineSelectionMode::PrimaryWithFallback;
    int scene = -1;  // TranslationScene index or -1

    std::vector<TranslationEngineType> successfulEngines() const;
};

// ---------------------------------------------------------------------------
// VLM provider types (Swift VLMProviderType.swift).
// ---------------------------------------------------------------------------
enum class VlmProviderType : std::uint8_t { OpenAI, Claude, Gemini, Ollama, PaddleOCR };
const char* vlmProviderRaw(VlmProviderType t);
std::optional<VlmProviderType> vlmProviderFromRaw(const std::string& raw);

/// OCR engine selector on Windows (Swift OCREngineType: vision → windowsOcr).
enum class OcrEngineType : std::uint8_t { WindowsOcr, PaddleOCR };
const char* ocrEngineRaw(OcrEngineType t);

// ---------------------------------------------------------------------------
// Errors (Swift TranslationProviderError minus localization).
// ErrorCategory is the WinHTTP-layer classification the brief requires:
// network / quota / auth / content — every provider maps its failures onto it.
// ---------------------------------------------------------------------------
enum class ErrorCategory {
    Network,      // unreachable, timeout, cancelled transport
    Quota,        // 429 / rate-limited / insufficient quota
    Auth,         // 401/403, invalid key, bad signature
    Content,      // malformed/unexpected response body, empty translation
    Configuration // invalid base URL, missing credential
};

const char* errorCategoryName(ErrorCategory c);

struct ProviderError {
    ErrorCategory category = ErrorCategory::Network;
    std::string message;
    std::optional<double> retryAfterSeconds;  // quota errors when known

    ProviderError() = default;
    ProviderError(ErrorCategory c, std::string msg) : category(c), message(std::move(msg)) {}
    static ProviderError network(std::string msg) { return {ErrorCategory::Network, std::move(msg)}; }
    static ProviderError quota(std::string msg, std::optional<double> retryAfter = std::nullopt) {
        ProviderError e{ErrorCategory::Quota, std::move(msg)};
        e.retryAfterSeconds = retryAfter;
        return e;
    }
    static ProviderError auth(std::string msg) { return {ErrorCategory::Auth, std::move(msg)}; }
    static ProviderError content(std::string msg) { return {ErrorCategory::Content, std::move(msg)}; }
    static ProviderError configuration(std::string msg) { return {ErrorCategory::Configuration, std::move(msg)}; }
};

}  // namespace tf

#endif  // TF_CORE_MODELS_HPP
