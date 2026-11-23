#pragma once
// TranslationScene.h — scenes for engine binding (macOS TranslationScene.swift).

#include <optional>
#include <string>

namespace tf::core {

enum class TranslationScene : int32_t {
    Screenshot,        // "screenshot"
    TextSelection,     // "text_selection"
    TranslateAndInsert,  // "translate_and_insert"
    Invalid = -1,
};

inline std::string toRaw(TranslationScene s) {
    switch (s) {
        case TranslationScene::Screenshot: return "screenshot";
        case TranslationScene::TextSelection: return "text_selection";
        case TranslationScene::TranslateAndInsert: return "translate_and_insert";
        default: return "";
    }
}

inline TranslationScene fromRaw(TranslationScene, const std::string& raw) {
    if (raw == "screenshot") { return TranslationScene::Screenshot; }
    if (raw == "text_selection") { return TranslationScene::TextSelection; }
    if (raw == "translate_and_insert") { return TranslationScene::TranslateAndInsert; }
    return TranslationScene::Invalid;
}

inline std::optional<TranslationScene> sceneFromRaw(const std::string& raw) {
    const TranslationScene s = fromRaw(TranslationScene{}, raw);
    if (s == TranslationScene::Invalid) { return std::nullopt; }
    return s;
}

constexpr TranslationScene kAllScenes[] = {
    TranslationScene::Screenshot,
    TranslationScene::TextSelection,
    TranslationScene::TranslateAndInsert,
};

}  // namespace tf::core
