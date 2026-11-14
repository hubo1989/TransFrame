#pragma once
// TranslationHistory.h — unified history model (macOS TranslationHistory.swift
// + HistoryStore.swift). Four categories share one record type:
//   translation — sourceText=原文, translatedText=译文, langs=语言名
//   screenshot  — sourceText=描述, translatedText=文件路径, langs=描述字段
//   color       — sourceText="Color Picked", translatedText=hex,
//                 sourceLanguage="RGB Hex", targetLanguage=hex (同值)
//   upload      — sourceText=markdown, translatedText=URL,
//                 sourceLanguage=provider.displayName, targetLanguage="Public URL"
//
// Persisted as a JSON array under SettingsKeys::translationHistory
// ("TransFrame.translationHistory"); thumbnailData is base64 (max ~10KB JPEG).

#include "Json.h"
#include "Uuid.h"

#include <optional>
#include <string>
#include <vector>

namespace tf::core {

enum class HistoryCategory : int32_t {
    Translation,  // "translation"
    Screenshot,   // "screenshot"
    Color,        // "color"
    Upload,       // "upload"
    Invalid = -1,
};

inline std::string toRaw(HistoryCategory c) {
    switch (c) {
        case HistoryCategory::Translation: return "translation";
        case HistoryCategory::Screenshot: return "screenshot";
        case HistoryCategory::Color: return "color";
        case HistoryCategory::Upload: return "upload";
        default: return "";
    }
}
inline HistoryCategory historyCategoryFromRaw(const std::string& raw) {
    if (raw == "translation") { return HistoryCategory::Translation; }
    if (raw == "screenshot") { return HistoryCategory::Screenshot; }
    if (raw == "color") { return HistoryCategory::Color; }
    if (raw == "upload") { return HistoryCategory::Upload; }
    return HistoryCategory::Invalid;
}
inline const char* historyCategoryDisplayName(HistoryCategory c) {
    switch (c) {
        case HistoryCategory::Translation: return "Translation";
        case HistoryCategory::Screenshot: return "Screenshot";
        case HistoryCategory::Color: return "Color";
        case HistoryCategory::Upload: return "Upload";
        default: return "";
    }
}
/// SF Symbol name on macOS; Windows maps to Segoe Fluent Icon glyphs in the UI layer.
inline const char* historyCategorySymbolName(HistoryCategory c) {
    switch (c) {
        case HistoryCategory::Translation: return "character.book.closed";
        case HistoryCategory::Screenshot: return "camera.viewfinder";
        case HistoryCategory::Color: return "eyedropper";
        case HistoryCategory::Upload: return "icloud.and.arrow.up";
        default: return "";
    }
}

struct TranslationHistory {
    Uuid id;
    std::string sourceText;
    std::string translatedText;
    std::string sourceLanguage;
    std::string targetLanguage;
    TimePoint timestamp{};
    std::optional<std::vector<std::uint8_t>> thumbnailData;  // JPEG bytes
    HistoryCategory category = HistoryCategory::Translation;
    bool isFavorite = false;

    // MARK: factories (Swift extensions in HistoryStore.swift)

    static TranslationHistory fromTranslationResult(const struct TranslationResult& result,
                                                    std::vector<std::uint8_t> thumbnail = {});
    /// Color-picked entry: translatedText/targetLanguage both carry the hex.
    static TranslationHistory colorPicked(const std::string& hex) {
        TranslationHistory h;
        h.id = Uuid::randomUuid();
        h.sourceText = "Color Picked";
        h.translatedText = hex;
        h.sourceLanguage = "RGB Hex";
        h.targetLanguage = hex;
        h.category = HistoryCategory::Color;
        return h;
    }
    /// Image-upload entry.
    static TranslationHistory uploaded(const std::string& markdown, const std::string& url,
                                       const std::string& providerName) {
        TranslationHistory h;
        h.id = Uuid::randomUuid();
        h.sourceText = markdown;
        h.translatedText = url;
        h.sourceLanguage = providerName;
        h.targetLanguage = "Public URL";
        h.category = HistoryCategory::Upload;
        return h;
    }
    /// Screenshot entry.
    static TranslationHistory screenshotSaved(const std::string& description,
                                              const std::string& filePath) {
        TranslationHistory h;
        h.id = Uuid::randomUuid();
        h.sourceText = description;
        h.translatedText = filePath;
        h.sourceLanguage = "Screenshot";
        h.targetLanguage = filePath;
        h.category = HistoryCategory::Screenshot;
        return h;
    }

    // MARK: computed (Swift)

    bool hasThumbnail() const { return thumbnailData && !thumbnailData->empty(); }
    std::string sourcePreview() const {
        return sourceText.substr(0, std::min<std::size_t>(sourceText.size(), 500));
    }
    std::string description() const { return sourceLanguage + " → " + targetLanguage; }
    /// Case-insensitive substring match over source+translated (Swift matches).
    bool matches(const std::string& query) const;

    friend bool operator==(const TranslationHistory&, const TranslationHistory&) = default;
};

// ------------------------------------------------------------ JSON codec ---
void to_json(Json& j, const TranslationHistory& h);
void from_json(const Json& j, TranslationHistory& h);

}  // namespace tf::core
