#pragma once
// TranslationResultBundle.h — BilingualSegment + EngineResult + bundle
// (macOS BilingualSegment.swift / TranslationResultBundle.swift /
//  TranslationResult.swift). Engine errors are carried as a string category
//  (Swift uses arbitrary Error payloads; the cross-platform persistence layer
//  only needs the category + message).

#include "CaptureDocument.h"   // TextSegment, Rect
#include "EngineSelectionV2.h"
#include "Json.h"
#include "TranslationEngineType.h"
#include "TranslationScene.h"
#include "Uuid.h"

#include <optional>
#include <string>
#include <vector>

namespace tf::core {

/// A single translation (macOS TranslationResult).
struct TranslationResult {
    std::string sourceText;
    std::string translatedText;
    std::string sourceLanguage;
    std::string targetLanguage;
    TimePoint timestamp{};

    std::string description() const { return sourceLanguage + " → " + targetLanguage; }
    bool hasChanges() const { return sourceText != translatedText; }
    static TranslationResult emptyFor(const std::string& text) {
        TranslationResult r;
        r.sourceText = text;
        r.translatedText = text;
        r.sourceLanguage = "Unknown";
        r.targetLanguage = "Unknown";
        return r;
    }
    /// Concatenates results (Swift combine): first langs, joined texts.
    static std::optional<TranslationResult> combine(const std::vector<TranslationResult>& results);

    friend bool operator==(const TranslationResult&, const TranslationResult&) = default;
};

/// One bilingual overlay segment (macOS BilingualSegment).
struct BilingualSegment {
    Uuid id;
    TextSegment original;
    std::string translated;
    std::optional<std::string> sourceLanguage;
    std::string targetLanguage;

    const std::string& sourceText() const { return original.text; }
    const Rect& boundingBox() const { return original.boundingBox; }
    Rect pixelBoundingBox(const Size& imageSize) const {
        return original.pixelBoundingBox(imageSize);
    }
    static BilingualSegment fromResult(const TranslationResult& result);
    static BilingualSegment fromSegment(const TextSegment& segment,
                                        const std::string& translatedText,
                                        std::optional<std::string> sourceLanguage,
                                        std::string targetLanguage);

    friend bool operator==(const BilingualSegment&, const BilingualSegment&) = default;
};

/// Error categories mirror Swift TranslationProviderError cases.
enum class TranslationErrorCategory : int32_t {
    TranslationFailed,
    IncompleteResponse,
    RateLimited,
    AuthenticationFailed,
    NetworkError,
    EngineUnavailable,
};

const char* toRaw(TranslationErrorCategory c);
TranslationErrorCategory translationErrorFromRaw(const std::string& raw);

struct EngineError {
    TranslationErrorCategory category = TranslationErrorCategory::TranslationFailed;
    std::string message;

    EngineError() = default;
    EngineError(TranslationErrorCategory c, std::string msg)
        : category(c), message(std::move(msg)) {}

    friend bool operator==(const EngineError&, const EngineError&) = default;
};

/// Result from one engine (macOS EngineResult; id generated at creation).
struct EngineResult {
    TranslationEngineType engine = TranslationEngineType::MtranServer;
    std::vector<BilingualSegment> segments;
    double latency = 0.0;
    std::optional<EngineError> error;
    Uuid id;
    /// UI chip label for compatible instances (two instances must not both
    /// render as "Custom" — PR #93).
    std::optional<std::string> displayName;

    EngineResult() : id(Uuid::randomUuid()) {}
    EngineResult(TranslationEngineType engine_, std::vector<BilingualSegment> segments_,
                 double latency_, std::optional<EngineError> error_ = std::nullopt,
                 std::optional<std::string> name = std::nullopt)
        : engine(engine_),
          segments(std::move(segments_)),
          latency(latency_),
          error(std::move(error_)),
          id(Uuid::randomUuid()),
          displayName(std::move(name)) {}

    static EngineResult failed(TranslationEngineType engine, EngineError error,
                               double latency = 0.0) {
        return EngineResult(engine, {}, latency, std::move(error));
    }

    bool isSuccess() const { return !error && !segments.empty(); }

    friend bool operator==(const EngineResult&, const EngineResult&) = default;
};

/// Legacy four-mode selection (migration source only; Swift EngineSelectionMode).
enum class EngineSelectionMode : int32_t {
    PrimaryWithFallback,  // "primary_fallback"
    Parallel,             // "parallel"
    QuickSwitch,          // "quick_switch"
    SceneBinding,         // "scene_binding"
    Invalid = -1,
};
inline std::string toRaw(EngineSelectionMode m) {
    switch (m) {
        case EngineSelectionMode::PrimaryWithFallback: return "primary_fallback";
        case EngineSelectionMode::Parallel: return "parallel";
        case EngineSelectionMode::QuickSwitch: return "quick_switch";
        case EngineSelectionMode::SceneBinding: return "scene_binding";
        default: return "";
    }
}
inline EngineSelectionMode engineSelectionModeFromRaw(const std::string& raw) {
    if (raw == "primary_fallback") { return EngineSelectionMode::PrimaryWithFallback; }
    if (raw == "parallel") { return EngineSelectionMode::Parallel; }
    if (raw == "quick_switch") { return EngineSelectionMode::QuickSwitch; }
    if (raw == "scene_binding") { return EngineSelectionMode::SceneBinding; }
    return EngineSelectionMode::Invalid;
}

/// Multi-engine bundle (macOS TranslationResultBundle).
struct TranslationResultBundle {
    std::vector<EngineResult> results;
    TranslationEngineType primaryEngine = TranslationEngineType::MtranServer;
    EngineSelectionMode selectionMode = EngineSelectionMode::PrimaryWithFallback;
    std::optional<TranslationScene> scene;
    TimePoint timestamp{};

    /// Primary engine's segments, else first successful engine's.
    std::vector<BilingualSegment> primaryResult() const;
    bool hasErrors() const;
    bool allFailed() const;
    std::vector<TranslationEngineType> successfulEngines() const;
    std::vector<TranslationEngineType> failedEngines() const;
    const EngineResult* resultFor(TranslationEngineType engine) const;
    const std::vector<BilingualSegment>* segmentsFor(TranslationEngineType engine) const;

    static TranslationResultBundle single(TranslationEngineType engine,
                                          std::vector<BilingualSegment> segments,
                                          std::optional<TranslationScene> scene = std::nullopt);
    static TranslationResultBundle failed(TranslationEngineType engine, EngineError error,
                                          std::optional<TranslationScene> scene = std::nullopt);

    friend bool operator==(const TranslationResultBundle&, const TranslationResultBundle&) = default;
};

// ------------------------------------------------------------ JSON codecs ---
void to_json(Json& j, const BilingualSegment& s);
void from_json(const Json& j, BilingualSegment& s);
void to_json(Json& j, const EngineResult& r);
void from_json(const Json& j, EngineResult& r);
void to_json(Json& j, const TranslationResult& r);
void from_json(const Json& j, TranslationResult& r);

}  // namespace tf::core
