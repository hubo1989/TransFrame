// TranslationResultBundle.cpp — method bodies + JSON codecs.
#include "TranslationResultBundle.h"

#include <algorithm>

namespace tf::core {

// -------------------------------------------------------- TranslationResult --
std::optional<TranslationResult> TranslationResult::combine(
    const std::vector<TranslationResult>& results) {
    if (results.empty()) { return std::nullopt; }
    TranslationResult out;
    out.sourceText = results.front().sourceText;
    out.sourceLanguage = results.front().sourceLanguage;
    out.targetLanguage = results.front().targetLanguage;
    for (std::size_t i = 0; i < results.size(); ++i) {
        out.translatedText += results[i].translatedText;
        if (i + 1 < results.size()) { out.sourceText += "\n"; }
    }
    return out;
}

void to_json(Json& j, const TranslationResult& r) {
    j = Json{{"sourceText", r.sourceText},
             {"translatedText", r.translatedText},
             {"sourceLanguage", r.sourceLanguage},
             {"targetLanguage", r.targetLanguage},
             {"timestamp", r.timestamp}};
}
void from_json(const Json& j, TranslationResult& r) {
    r.sourceText = j.at("sourceText").get<std::string>();
    r.translatedText = j.at("translatedText").get<std::string>();
    r.sourceLanguage = j.at("sourceLanguage").get<std::string>();
    r.targetLanguage = j.at("targetLanguage").get<std::string>();
    r.timestamp = getOptionalOr<TimePoint>(j, "timestamp", TimePoint{});
}

// -------------------------------------------------------- BilingualSegment --
BilingualSegment BilingualSegment::fromResult(const TranslationResult& result) {
    BilingualSegment s;
    s.id = Uuid::randomUuid();
    TextSegment original;
    original.id = Uuid::randomUuid();
    original.text = result.sourceText;
    original.boundingBox = Rect::zero();
    original.confidence = 1.0f;
    s.original = std::move(original);
    s.translated = result.translatedText;
    s.sourceLanguage = result.sourceLanguage;
    s.targetLanguage = result.targetLanguage;
    return s;
}

BilingualSegment BilingualSegment::fromSegment(const TextSegment& segment,
                                               const std::string& translatedText,
                                               std::optional<std::string> sourceLanguage,
                                               std::string targetLanguage) {
    BilingualSegment s;
    s.id = segment.id;
    s.original = segment;
    s.translated = translatedText;
    s.sourceLanguage = std::move(sourceLanguage);
    s.targetLanguage = std::move(targetLanguage);
    return s;
}

void to_json(Json& j, const BilingualSegment& s) {
    j = Json{{"id", s.id}, {"original", s.original}, {"translated", s.translated},
             {"targetLanguage", s.targetLanguage}};
    j["sourceLanguage"] = s.sourceLanguage ? Json(*s.sourceLanguage) : Json();
}
void from_json(const Json& j, BilingualSegment& s) {
    s.id = j.at("id").get<Uuid>();
    s.original = j.at("original").get<TextSegment>();
    s.translated = j.at("translated").get<std::string>();
    s.targetLanguage = j.at("targetLanguage").get<std::string>();
    s.sourceLanguage = getOptional<std::string>(j, "sourceLanguage");
}

// ----------------------------------------------------------- error category --
const char* toRaw(TranslationErrorCategory c) {
    switch (c) {
        case TranslationErrorCategory::TranslationFailed: return "translationFailed";
        case TranslationErrorCategory::IncompleteResponse: return "incompleteResponse";
        case TranslationErrorCategory::RateLimited: return "rateLimited";
        case TranslationErrorCategory::AuthenticationFailed: return "authenticationFailed";
        case TranslationErrorCategory::NetworkError: return "networkError";
        case TranslationErrorCategory::EngineUnavailable: return "engineUnavailable";
        default: return "translationFailed";
    }
}
TranslationErrorCategory translationErrorFromRaw(const std::string& raw) {
    if (raw == "incompleteResponse") { return TranslationErrorCategory::IncompleteResponse; }
    if (raw == "rateLimited") { return TranslationErrorCategory::RateLimited; }
    if (raw == "authenticationFailed") { return TranslationErrorCategory::AuthenticationFailed; }
    if (raw == "networkError") { return TranslationErrorCategory::NetworkError; }
    if (raw == "engineUnavailable") { return TranslationErrorCategory::EngineUnavailable; }
    return TranslationErrorCategory::TranslationFailed;
}

// ------------------------------------------------------------ EngineResult --
void to_json(Json& j, const EngineResult& r) {
    j = Json{{"engine", toRaw(r.engine)}, {"segments", r.segments},
             {"latency", r.latency}, {"id", r.id}};
    j["error"] = r.error ? Json{{"category", toRaw(r.error->category)},
                                {"message", r.error->message}}
                         : Json();
    j["displayName"] = r.displayName ? Json(*r.displayName) : Json();
}
void from_json(const Json& j, EngineResult& r) {
    const std::string raw = j.at("engine").get<std::string>();
    r.engine = fromRaw(TranslationEngineType{}, raw);
    r.segments = j.at("segments").get<std::vector<BilingualSegment>>();
    r.latency = getOptionalOr<double>(j, "latency", 0.0);
    r.id = j.at("id").get<Uuid>();
    if (auto it = j.find("error"); it != j.end() && it->is_object()) {
        EngineError e;
        e.category = translationErrorFromRaw(it->at("category").get<std::string>());
        e.message = it->at("message").get<std::string>();
        r.error = std::move(e);
    }
    r.displayName = getOptional<std::string>(j, "displayName");
}

// --------------------------------------------------- TranslationResultBundle --
std::vector<BilingualSegment> TranslationResultBundle::primaryResult() const {
    for (const auto& r : results) {
        if (r.engine == primaryEngine && r.isSuccess()) { return r.segments; }
    }
    for (const auto& r : results) {
        if (r.isSuccess()) { return r.segments; }
    }
    return {};
}

bool TranslationResultBundle::hasErrors() const {
    return std::any_of(results.begin(), results.end(),
                       [](const EngineResult& r) { return r.error.has_value(); });
}

bool TranslationResultBundle::allFailed() const {
    return !results.empty() &&
           std::all_of(results.begin(), results.end(),
                       [](const EngineResult& r) { return r.error.has_value(); });
}

std::vector<TranslationEngineType> TranslationResultBundle::successfulEngines() const {
    std::vector<TranslationEngineType> out;
    for (const auto& r : results) {
        if (r.isSuccess()) { out.push_back(r.engine); }
    }
    return out;
}

std::vector<TranslationEngineType> TranslationResultBundle::failedEngines() const {
    std::vector<TranslationEngineType> out;
    for (const auto& r : results) {
        if (!r.isSuccess()) { out.push_back(r.engine); }
    }
    return out;
}

const EngineResult* TranslationResultBundle::resultFor(TranslationEngineType engine) const {
    for (const auto& r : results) {
        if (r.engine == engine) { return &r; }
    }
    return nullptr;
}

const std::vector<BilingualSegment>* TranslationResultBundle::segmentsFor(
    TranslationEngineType engine) const {
    const EngineResult* r = resultFor(engine);
    return r ? &r->segments : nullptr;
}

TranslationResultBundle TranslationResultBundle::single(
    TranslationEngineType engine, std::vector<BilingualSegment> segments,
    std::optional<TranslationScene> scene) {
    TranslationResultBundle b;
    b.results.push_back(EngineResult(engine, std::move(segments), 0.0));
    b.primaryEngine = engine;
    b.selectionMode = EngineSelectionMode::PrimaryWithFallback;
    b.scene = scene;
    return b;
}

TranslationResultBundle TranslationResultBundle::failed(TranslationEngineType engine,
                                                        EngineError error,
                                                        std::optional<TranslationScene> scene) {
    TranslationResultBundle b;
    b.results.push_back(EngineResult::failed(engine, std::move(error)));
    b.primaryEngine = engine;
    b.selectionMode = EngineSelectionMode::PrimaryWithFallback;
    b.scene = scene;
    return b;
}

}  // namespace tf::core
