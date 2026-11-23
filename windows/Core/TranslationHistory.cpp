// TranslationHistory.cpp — JSON codec + matching.
#include "TranslationHistory.h"

#include "TranslationResultBundle.h"

#include <algorithm>
#include <cctype>

namespace tf::core {

TranslationHistory TranslationHistory::fromTranslationResult(
    const TranslationResult& result, std::vector<std::uint8_t> thumbnail) {
    TranslationHistory h;
    h.id = Uuid::randomUuid();
    h.sourceText = result.sourceText;
    h.translatedText = result.translatedText;
    h.sourceLanguage = result.sourceLanguage;
    h.targetLanguage = result.targetLanguage;
    h.timestamp = result.timestamp;
    if (!thumbnail.empty()) { h.thumbnailData = std::move(thumbnail); }
    return h;
}

namespace {
std::string lowercased(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
}  // namespace

bool TranslationHistory::matches(const std::string& query) const {
    if (query.empty()) { return true; }
    const std::string q = lowercased(query);
    return lowercased(sourceText).find(q) != std::string::npos ||
           lowercased(translatedText).find(q) != std::string::npos;
}

// Swift CodingKeys order: id, sourceText, translatedText, sourceLanguage,
// targetLanguage, timestamp, thumbnailData, category, isFavorite.
// decodeIfPresent defaults: category -> translation, isFavorite -> false.
void to_json(Json& j, const TranslationHistory& h) {
    j = Json{{"id", h.id},
             {"sourceText", h.sourceText},
             {"translatedText", h.translatedText},
             {"sourceLanguage", h.sourceLanguage},
             {"targetLanguage", h.targetLanguage},
             {"timestamp", h.timestamp}};
    j["thumbnailData"] = h.thumbnailData ? Json(*h.thumbnailData) : Json();
    j["category"] = toRaw(h.category);
    j["isFavorite"] = h.isFavorite;
}

void from_json(const Json& j, TranslationHistory& h) {
    h.id = j.at("id").get<Uuid>();
    h.sourceText = j.at("sourceText").get<std::string>();
    h.translatedText = j.at("translatedText").get<std::string>();
    h.sourceLanguage = j.at("sourceLanguage").get<std::string>();
    h.targetLanguage = j.at("targetLanguage").get<std::string>();
    h.timestamp = getOptionalOr<TimePoint>(j, "timestamp", TimePoint{});
    h.thumbnailData = getOptional<std::vector<std::uint8_t>>(j, "thumbnailData");
    const auto category = getOptional<std::string>(j, "category");
    h.category = category ? historyCategoryFromRaw(*category) : HistoryCategory::Translation;
    h.isFavorite = getOptionalOr<bool>(j, "isFavorite", false);
}

}  // namespace tf::core
