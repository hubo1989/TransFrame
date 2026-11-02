// tf/OcrEngines.hpp
// OCR engines: Windows.Media.Ocr mapping (bbox normalization port of Swift
// OCRText.from(VNRecognizedTextObservation) — Windows returns top-left
// origin, so the mapping is a direct scale), PaddleOCR external-process
// protocol (fast/precise parsers + health cache), and the VLM OCR skeleton
// with five provider slots (brief §1).
//
// The Windows.Media.Ocr invocation itself is WinRT (NativeShell/
// WindowsOcrEngine.winrt.cpp, NOT RUN on this macOS host); everything here is
// the pure mapping/protocol layer that Catch2 verifies.
#ifndef TF_OCR_ENGINES_HPP
#define TF_OCR_ENGINES_HPP

#include "tf/CoreModels.hpp"
#include "tf/HttpTransport.hpp"
#include "tf/LLMTranslationCore.hpp"  // Outcome<T, E>
#include "tf/TranslationProvider.hpp"  // CredentialStorePtr

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tf {

// ---------------------------------------------------------------------------
/// Unified OCR engine interface (Swift AnyOCREngine; the image handle is the
/// cross-platform PixelBuffer — BGRA8 bytes + size — so Core stays testable
/// without WinRT bitmap types).
struct OcrImage {
    std::int64_t width = 0;
    std::int64_t height = 0;
    std::string bgra;  // width*height*4 bytes, top-down
};

class IOcrEngine {
public:
    virtual ~IOcrEngine() = default;
    virtual std::string id() const = 0;
    virtual std::string name() const = 0;
    virtual bool isAvailable() const = 0;
    /// Runs OCR; segments carry normalized bboxes (top-left origin) +
    /// confidence, matching macOS TextSegment schema.
    virtual Outcome<OcrResult, ProviderError> recognize(const OcrImage& image) = 0;
};

// ---------------------------------------------------------------------------
/// Windows.Media.Ocr engine (FR-15). Language-pack detection gates
/// availability; missing pack for a requested language → gray.
class WindowsOcrEngine final : public IOcrEngine {
public:
    /// One language the engine may recognize (BCP-47 tag ↔ installed pack).
    struct Language {
        std::string tag;        // "en-US", "zh-Hans", ...
        std::string displayName;
    };

    /// Language-inventory seam: implemented by NativeShell (WinRT
    /// OcrEngine.AvailableRecognizerLanguages) and mocked in tests. Returns
    /// installed packs.
    using LanguageProvider = std::function<std::vector<Language>()>;

    /// Recognition primitive implemented by NativeShell over WinRT:
    /// returns raw line results (pixel rects, top-left origin) for the image
    /// using the given language tags.
    struct RawLine {
        std::string text;
        std::int64_t x = 0, y = 0, w = 0, h = 0;  // pixels
        float confidence = 0.0f;                  // Windows OCR gives no score → 1.0
    };
    using Recognizer = std::function<std::optional<ProviderError>(
        const OcrImage&, const std::vector<std::string>& languageTags,
        std::vector<RawLine>& outLines)>;

    WindowsOcrEngine(LanguageProvider languageProvider, Recognizer recognizer,
                     float minimumConfidence = 0.0f);

    std::string id() const override { return "windowsOcr"; }
    std::string name() const override { return "Windows OCR"; }
    /// At least one recognizer language pack installed.
    bool isAvailable() const override;
    Outcome<OcrResult, ProviderError> recognize(const OcrImage& image) override;

    /// Installed language packs (cache-free; the WinRT call is cheap).
    std::vector<Language> installedLanguages() const;
    /// Whether a language pack for the tag (or its base) is installed —
    /// drives the gray state for missing Chinese packs (FR-15).
    bool hasLanguagePack(const std::string& bcp47Tag) const;

    /// Maps raw pixel-space lines to TextSegments with normalized bboxes.
    /// Confidence defaults to 1.0 (Windows OCR exposes no score; macOS parity
    /// keeps the schema field).
    static std::vector<TextSegment> mapLines(const std::vector<RawLine>& lines,
                                             std::int64_t imageWidth, std::int64_t imageHeight,
                                             float minimumConfidence = 0.0f);

    /// Language-tag normalization: zh → zh-Hans (Windows pack naming), en →
    /// en-US, ja → ja, ko → ko.
    static std::string normalizeLanguageTag(const std::string& tag);

private:
    LanguageProvider languageProvider_;
    Recognizer recognizer_;
    float minimumConfidence_;
};

// ---------------------------------------------------------------------------
/// PaddleOCR external-process engine (FR-16): fast (ocr CLI, ~1s) / precise
/// (doc_parser VL-1.5, ~12s) modes; JSON protocol parsers are pure logic and
/// unit-tested here; the process spawn lives in NativeShell.
class PaddleOcrEngine final : public IOcrEngine {
public:
    enum class Mode { Fast, Precise };

    struct Configuration {
        std::vector<std::string> languages = {"ch", "en"};  // PaddleOCR lang codes
        float minimumConfidence = 0.0f;
        bool useGPU = false;
        bool useDirectionClassify = true;
        Mode mode = Mode::Fast;
        std::string executablePath;  // empty → availability check fails
    };

    /// Process-runner seam (NativeShell: CreateProcess + pipes).
    /// Arguments are passed argv-style, never through a shell (Swift
    /// security: no shell metacharacter path).
    using ProcessRunner = std::function<std::optional<ProviderError>(
        const std::string& executable, const std::vector<std::string>& args, int timeoutSeconds,
        std::string& outStdout, std::string& outStderr)>;

    /// Availability seam: filesystem check for the executable + version
    /// probe (Swift PaddleOCRChecker.performFullCheck).
    using AvailabilityChecker = std::function<bool(const std::string& executablePath)>;

    PaddleOcrEngine(Configuration config, ProcessRunner runner,
                    AvailabilityChecker availability);

    std::string id() const override { return "paddleocr"; }
    std::string name() const override { return "PaddleOCR"; }
    bool isAvailable() const override;
    Outcome<OcrResult, ProviderError> recognize(const OcrImage& image) override;

    /// Builds the argv for the current configuration (Swift buildArguments).
    std::vector<std::string> buildArguments(const std::string& imagePath) const;

    /// Fast-mode parser: {"res": {"rec_texts": [...], "rec_scores": [...],
    /// "rec_boxes": [[x1,y1,x2,y2], ...]}} → normalized segments.
    static std::vector<TextSegment> parseFastMode(const std::string& json,
                                                  std::int64_t imageWidth,
                                                  std::int64_t imageHeight,
                                                  float minimumConfidence = 0.0f);

    /// Precise-mode parser: {"res": {"parsing_res_list": [{"block_content":
    /// ..., "block_bbox": [x1,y1,x2,y2], "block_label": ...}]}} — skips
    /// chart/seal/image/table/figure blocks; default confidence 0.9.
    static std::vector<TextSegment> parsePreciseMode(const std::string& json,
                                                     std::int64_t imageWidth,
                                                     std::int64_t imageHeight);

    /// Python-literal → JSON normalization for the `{'res': ...}` stderr
    /// payload: single→double quotes, True/False/None, numpy arrays, "8."
    /// floats (Swift convertPythonDictToJson).
    static std::string pythonDictToJson(const std::string& pythonLiteral);

    /// Extracts the {'res': ...} tail from mixed stdout/stderr output and
    /// strips ANSI color codes (Swift executePaddleOCR).
    static std::string extractResultJson(const std::string& stdoutText,
                                         const std::string& stderrText);

private:
    Configuration config_;
    ProcessRunner runner_;
    AvailabilityChecker availability_;
};

// ---------------------------------------------------------------------------
/// PaddleOCR availability cache (Swift PaddleOCRChecker state + TTL).
/// Pure logic; tests drive the clock.
class PaddleOcrHealthCache {
public:
    explicit PaddleOcrHealthCache(double ttlSeconds = 300.0);
    std::optional<bool> cached(const std::string& executable, double now) const;
    void store(const std::string& executable, bool available, double now);
    void invalidateAll();

private:
    double ttl_;
    std::map<std::string, std::pair<bool, double>> cache_;
};

// ---------------------------------------------------------------------------
/// VLM OCR skeleton (FR-17): five provider slots (OpenAI/Claude/Gemini/
/// Ollama/PaddleOCR-VL). HTTP details reuse the translation layer's
/// transport + response parsing; the five analyze() implementations are
/// NativeShell-side (image upload shapes differ per provider) — this class
/// owns routing, prompt building, and segment extraction.
class VlmOcrEngine final : public IOcrEngine {
public:
    /// Per-provider analyze primitive implemented by NativeShell (base64/URL
    /// image upload, provider-specific body). Returns raw VLM JSON text.
    using Analyzer = std::function<std::optional<ProviderError>(
        VlmProviderType provider, const std::string& prompt, const OcrImage& image,
        std::string& outRawJson)>;

    VlmOcrEngine(Analyzer analyzer, CredentialStorePtr credentials);

    std::string id() const override { return "vlm"; }
    std::string name() const override { return "VLM"; }
    /// Available when at least one provider has credentials (or Ollama local
    /// endpoint is configured — keyless).
    bool isAvailable() const override;
    Outcome<OcrResult, ProviderError> recognize(const OcrImage& image) override;

    /// The extraction prompt (Swift VLM prompt: JSON array of {text,
    /// confidence, box{x,y,width,height}} in normalized coordinates).
    static const std::string& extractionPrompt();

    /// Parses the VLM's JSON (plain array, {"results": [...]}, or chat
    /// completion content embedding a ```json block) into segments with
    /// normalized bboxes. box values are already normalized (0..1).
    static std::vector<TextSegment> parseSegments(const std::string& rawJson,
                                                  std::int64_t imageWidth,
                                                  std::int64_t imageHeight);

    /// Provider slots present (brief: 五 Provider 接口位).
    static std::vector<VlmProviderType> supportedProviders();

    void setActiveProvider(VlmProviderType provider) { activeProvider_ = provider; }

private:
    Analyzer analyzer_;
    CredentialStorePtr credentials_;
    VlmProviderType activeProvider_ = VlmProviderType::OpenAI;
};

}  // namespace tf

#endif  // TF_OCR_ENGINES_HPP
