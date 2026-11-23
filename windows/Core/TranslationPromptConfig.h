#pragma once
// TranslationPromptConfig.h — prompt selection + template substitution.
// Priority and templates are byte-for-byte the macOS implementation in
// TransFrame/Models/TranslationPromptConfig.swift:
//   scene-specific > compatible-specific > engine-specific > default.

#include "Json.h"
#include "TranslationEngineType.h"
#include "TranslationScene.h"

#include <map>
#include <optional>
#include <string>
#include <utility>

namespace tf::core {

struct TranslationPromptConfig {
    std::map<TranslationEngineType, std::string> enginePrompts;
    std::map<std::string, std::string> compatibleEnginePrompts;
    std::map<TranslationScene, std::string> scenePrompts;

    static constexpr const char* kDefaultPrompt =
        "Translate the following text from {source_language} to {target_language}.\n"
        "Provide only the translation without any explanations or additional text.\n\n"
        "Text to translate:\n"
        "{text}";
    static constexpr const char* kDefaultInsertPrompt =
        "Translate the following text from {source_language} to {target_language}.\n"
        "The translation will be inserted at the cursor position.\n"
        "Provide only the translation without any explanations, formatting, or additional text.\n"
        "Keep the translation concise and natural for the target language.\n\n"
        "Text to translate:\n"
        "{text}";

    std::string promptPreview(TranslationEngineType engine, TranslationScene scene,
                              std::optional<std::string> compatiblePromptID = std::nullopt) const {
        if (auto it = scenePrompts.find(scene); it != scenePrompts.end() && !it->second.empty()) {
            return it->second;
        }
        if (engine == TranslationEngineType::Custom && compatiblePromptID) {
            if (auto it = compatibleEnginePrompts.find(*compatiblePromptID);
                it != compatibleEnginePrompts.end() && !it->second.empty()) {
                return it->second;
            }
        }
        if (auto it = enginePrompts.find(engine); it != enginePrompts.end() && !it->second.empty()) {
            return it->second;
        }
        return scene == TranslationScene::TranslateAndInsert ? kDefaultInsertPrompt : kDefaultPrompt;
    }

    /// Resolve variables using Swift's sequential replacingOccurrences order.
    /// Any placeholder-shaped text inside `sourceLanguage` or `targetLanguage`
    /// is therefore also substituted by later passes, matching Swift semantics.
    std::string resolvedPrompt(TranslationEngineType engine, TranslationScene scene,
                               const std::string& sourceLanguage,
                               const std::string& targetLanguage,
                               const std::string& text,
                               std::optional<std::string> compatiblePromptID = std::nullopt) const {
        std::string prompt = promptPreview(engine, scene, std::move(compatiblePromptID));
        replaceAll(prompt, "{source_language}", sourceLanguage);
        replaceAll(prompt, "{target_language}", targetLanguage);
        replaceAll(prompt, "{text}", text);
        return prompt;
    }

    bool hasCustomPrompts() const {
        return !enginePrompts.empty() || !compatibleEnginePrompts.empty() || !scenePrompts.empty();
    }
    void reset() {
        enginePrompts.clear();
        compatibleEnginePrompts.clear();
        scenePrompts.clear();
    }

    friend bool operator==(const TranslationPromptConfig&, const TranslationPromptConfig&) = default;

    void toJson(Json& j) const {
        Json engine = Json::array();
        for (const auto& [k, v] : enginePrompts) {
            engine.push_back(toRaw(k));
            engine.push_back(v);
        }
        Json scene = Json::array();
        for (const auto& [k, v] : scenePrompts) {
            scene.push_back(toRaw(k));
            scene.push_back(v);
        }
        Json compatible = Json::object();
        for (const auto& [k, v] : compatibleEnginePrompts) { compatible[k] = v; }
        j = Json{{"enginePrompts", std::move(engine)},
                 {"compatibleEnginePrompts", std::move(compatible)},
                 {"scenePrompts", std::move(scene)}};
    }

    static TranslationPromptConfig fromJson(const Json& j) {
        TranslationPromptConfig c;
        if (auto it = j.find("enginePrompts"); it != j.end() && it->is_array()) {
            for (size_t i = 0; i + 1 < it->size(); i += 2) {
                if (!(*it)[i].is_string() || !(*it)[i + 1].is_string()) { continue; }
                const TranslationEngineType e = fromRaw(TranslationEngineType{}, (*it)[i].get<std::string>());
                if (e != TranslationEngineType::Invalid) {
                    c.enginePrompts[e] = (*it)[i + 1].get<std::string>();
                }
            }
        }
        if (auto it = j.find("compatibleEnginePrompts"); it != j.end() && it->is_object()) {
            for (auto entry = it->begin(); entry != it->end(); ++entry) {
                if (entry.value().is_string()) {
                    c.compatibleEnginePrompts[entry.key()] = entry.value().get<std::string>();
                }
            }
        }
        if (auto it = j.find("scenePrompts"); it != j.end() && it->is_array()) {
            for (size_t i = 0; i + 1 < it->size(); i += 2) {
                if (!(*it)[i].is_string() || !(*it)[i + 1].is_string()) { continue; }
                const TranslationScene s = fromRaw(TranslationScene{}, (*it)[i].get<std::string>());
                if (s != TranslationScene::Invalid) {
                    c.scenePrompts[s] = (*it)[i + 1].get<std::string>();
                }
            }
        }
        return c;
    }

private:
    static void replaceAll(std::string& s, const std::string& from, const std::string& to) {
        if (from.empty()) { return; }
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
};

inline void to_json(Json& j, const TranslationPromptConfig& c) { c.toJson(j); }
inline void from_json(const Json& j, TranslationPromptConfig& c) {
    c = TranslationPromptConfig::fromJson(j);
}

}  // namespace tf::core
