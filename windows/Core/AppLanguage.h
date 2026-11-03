#pragma once
// AppLanguage.h — UI display languages (macOS AppLanguage.swift).

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

namespace tf::core {

enum class AppLanguage : int32_t {
    System, English, SimplifiedChinese, German, Spanish, French, Italian,
    Japanese, Korean, Portuguese, Russian, Invalid = -1,
};

inline std::string toRaw(AppLanguage l) {
    switch (l) {
        case AppLanguage::System: return "system";
        case AppLanguage::English: return "en";
        case AppLanguage::SimplifiedChinese: return "zh-Hans";
        case AppLanguage::German: return "de";
        case AppLanguage::Spanish: return "es";
        case AppLanguage::French: return "fr";
        case AppLanguage::Italian: return "it";
        case AppLanguage::Japanese: return "ja";
        case AppLanguage::Korean: return "ko";
        case AppLanguage::Portuguese: return "pt";
        case AppLanguage::Russian: return "ru";
        default: return "";
    }
}

inline AppLanguage appLanguageFromRaw(const std::string& raw) {
    if (raw == "system") { return AppLanguage::System; }
    if (raw == "en") { return AppLanguage::English; }
    if (raw == "zh-Hans") { return AppLanguage::SimplifiedChinese; }
    if (raw == "de") { return AppLanguage::German; }
    if (raw == "es") { return AppLanguage::Spanish; }
    if (raw == "fr") { return AppLanguage::French; }
    if (raw == "it") { return AppLanguage::Italian; }
    if (raw == "ja") { return AppLanguage::Japanese; }
    if (raw == "ko") { return AppLanguage::Korean; }
    if (raw == "pt") { return AppLanguage::Portuguese; }
    if (raw == "ru") { return AppLanguage::Russian; }
    return AppLanguage::Invalid;
}

inline const char* displayName(AppLanguage l) {
    switch (l) {
        case AppLanguage::System: return "System";  // localized in the WinUI layer
        case AppLanguage::English: return "English";
        case AppLanguage::SimplifiedChinese: return "简体中文";
        case AppLanguage::German: return "Deutsch";
        case AppLanguage::Spanish: return "Español";
        case AppLanguage::French: return "Français";
        case AppLanguage::Italian: return "Italiano";
        case AppLanguage::Japanese: return "日本語";
        case AppLanguage::Korean: return "한국어";
        case AppLanguage::Portuguese: return "Português";
        case AppLanguage::Russian: return "Русский";
        default: return "";
    }
}

constexpr AppLanguage kAllAppLanguages[] = {
    AppLanguage::System, AppLanguage::English, AppLanguage::SimplifiedChinese,
    AppLanguage::German, AppLanguage::Spanish, AppLanguage::French,
    AppLanguage::Italian, AppLanguage::Japanese, AppLanguage::Korean,
    AppLanguage::Portuguese, AppLanguage::Russian,
};

inline std::optional<AppLanguage> appLanguageFromLocale(std::string identifier) {
    std::replace(identifier.begin(), identifier.end(), '_', '-');
    std::transform(identifier.begin(), identifier.end(), identifier.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (identifier.rfind("zh-hans", 0) == 0 || identifier == "zh-cn" ||
        identifier == "zh-sg" || identifier == "zh") {
        return AppLanguage::SimplifiedChinese;
    }
    for (AppLanguage l : kAllAppLanguages) {
        if (l == AppLanguage::System) { continue; }
        std::string raw = toRaw(l);
        std::transform(raw.begin(), raw.end(), raw.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (identifier == raw || identifier.rfind(raw + "-", 0) == 0) { return l; }
    }
    return std::nullopt;
}

}  // namespace tf::core
