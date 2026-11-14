// windows/UI/GeneralPage/GeneralPageViewModel.cpp
// FR-35 通用页签 ViewModel 实现（纯 C++，字段对照见 .hpp 文件头）。
#include "GeneralPageViewModel.hpp"

#include "AppLanguage.h"  // Core task-A 模型：AppLanguage 10 语言表

namespace tf::ui {
namespace {

std::string Trimmed(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}

/// UTF-8 → UTF-16（Core displayName 是 UTF-8 字面量，如 "简体中文"）。
std::wstring Utf8ToWide(const char* s) {
    std::wstring out;
    for (; *s; ++s) {
        const unsigned char c = static_cast<unsigned char>(*s);
        if (c < 0x80) {
            out.push_back(static_cast<wchar_t>(c));
        } else if ((c & 0xE0) == 0xC0) {
            const unsigned char c1 = static_cast<unsigned char>(*(++s));
            out.push_back(static_cast<wchar_t>(((c & 0x1F) << 6) | (c1 & 0x3F)));
        } else if ((c & 0xF0) == 0xE0) {
            const unsigned char c1 = static_cast<unsigned char>(*(++s));
            const unsigned char c2 = static_cast<unsigned char>(*(++s));
            out.push_back(static_cast<wchar_t>(((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) |
                                              (c2 & 0x3F)));
        } else if ((c & 0xF8) == 0xF0) {
            const unsigned char c1 = static_cast<unsigned char>(*(++s));
            const unsigned char c2 = static_cast<unsigned char>(*(++s));
            const unsigned char c3 = static_cast<unsigned char>(*(++s));
            const std::uint32_t cp =
                ((c & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
            out.push_back(static_cast<wchar_t>(0xD800 + ((cp - 0x10000) >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + ((cp - 0x10000) & 0x3FF)));
        }
    }
    return out;
}

}  // namespace

GeneralPageViewModel::GeneralPageViewModel(Persistence persistence)
    : persistence_(std::move(persistence)) {
    Load();
}

void GeneralPageViewModel::SetSaveLocation(std::string_view path) {
    saveLocation_ = Trimmed(std::string(path));
}

bool GeneralPageViewModel::IsValidFormat(std::string_view v) const {
    return v == "png" || v == "jpeg" || v == "heic";
}

void GeneralPageViewModel::SetDefaultFormat(std::string_view v) {
    if (IsValidFormat(v)) defaultFormat_ = std::string(v);
}

double GeneralPageViewModel::ActiveQuality() const {
    if (defaultFormat_ == "jpeg") return jpegQuality_;
    if (defaultFormat_ == "heic") return heicQuality_;
    return 0.0;  // png 无质量参数
}

void GeneralPageViewModel::SetActiveQuality(double q) {
    if (q < kQualityMin) q = kQualityMin;
    if (q > kQualityMax) q = kQualityMax;
    if (defaultFormat_ == "jpeg") jpegQuality_ = q;
    else if (defaultFormat_ == "heic") heicQuality_ = q;
}

const std::vector<AppLanguageOption>& GeneralPageViewModel::AppLanguageOptions() {
    static const std::vector<AppLanguageOption> kOptions = [] {
        std::vector<AppLanguageOption> out;
        out.push_back({"system", L"跟随系统"});
        for (auto lang : {core::AppLanguage::English, core::AppLanguage::SimplifiedChinese,
                          core::AppLanguage::German, core::AppLanguage::Spanish,
                          core::AppLanguage::French, core::AppLanguage::Italian,
                          core::AppLanguage::Japanese, core::AppLanguage::Korean,
                          core::AppLanguage::Portuguese, core::AppLanguage::Russian}) {
            out.push_back({core::toRaw(lang), Utf8ToWide(core::displayName(lang))});
        }
        return out;
    }();
    return kOptions;
}

void GeneralPageViewModel::SetAppLanguage(std::string_view raw) {
    for (const auto& opt : AppLanguageOptions()) {
        if (opt.raw == raw) {
            appLanguage_ = std::string(raw);
            return;
        }
    }
}

tf::core::EngineConfig GeneralPageViewModel::Patch() const {
    tf::core::EngineConfig patch{};
    patch.saveLocation = saveLocation_;
    patch.defaultFormat = defaultFormat_;
    patch.jpegQuality = jpegQuality_;
    patch.heicQuality = heicQuality_;
    return patch;
}

void GeneralPageViewModel::Load() {
    if (!persistence_.loadEnginesJson) return;
    const std::string text = persistence_.loadEnginesJson();
    auto parsed = core::parseEngineConfig(text);
    if (parsed.ok) {
        const tf::core::EngineConfig& c = parsed.config;
        saveLocation_ = c.saveLocation;
        defaultFormat_ = c.defaultFormat;
        jpegQuality_ = c.jpegQuality;
        heicQuality_ = c.heicQuality;
    }
    Json root = Json::parse(text, nullptr, false);
    if (!root.is_discarded() && root.is_object() && root.contains("appLanguage") &&
        root.at("appLanguage").is_string()) {
        std::string raw = root.at("appLanguage").get<std::string>();
        for (const auto& opt : AppLanguageOptions()) {
            if (opt.raw == raw) {
                appLanguage_ = raw;
                break;
            }
        }
    }
}

bool GeneralPageViewModel::Save() {
    if (!persistence_.saveEnginesJson) return false;
    std::string existing =
        persistence_.loadEnginesJson ? persistence_.loadEnginesJson() : std::string{};
    std::string merged = core::serializeEngineConfigPatch(existing, Patch());
    Json root = Json::parse(merged, nullptr, false);
    if (!root.is_discarded() && root.is_object()) {
        root["appLanguage"] = appLanguage_;
        merged = root.dump(2) + "\n";
    }
    persistence_.saveEnginesJson(merged);
    return true;
}

}  // namespace tf::ui
