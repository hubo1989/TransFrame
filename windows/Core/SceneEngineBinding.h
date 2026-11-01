#pragma once
// SceneEngineBinding.h — legacy scene→engine binding kept ONLY as the
// migration-source type (macOS marks it deprecated). New code uses
// EngineChainItem. Encoded as an array under "TransFrame.sceneBindings".
//
// Swift CodingKeys (alphabetical JSON): customPrompt, fallbackEnabled,
// fallbackEngine, primaryEngine, scene.

#include "Json.h"
#include "TranslationEngineType.h"
#include "TranslationScene.h"

#include <optional>
#include <string>

namespace tf::core {

struct SceneEngineBinding {
    TranslationScene scene = TranslationScene::Screenshot;
    TranslationEngineType primaryEngine = TranslationEngineType::MtranServer;
    std::optional<TranslationEngineType> fallbackEngine;
    bool fallbackEnabled = true;
    std::optional<std::string> customPrompt;

    static SceneEngineBinding defaultFor(TranslationScene scene) {
        // macOS default: apple + mtran fallback. Windows has no apple engine;
        // the default primary becomes MTranServer (PRD §6.12). With MTranServer
        // as primary, an OpenAI-compatible Custom instance is the fallback.
        // TODO(schema-align): confirm the Windows default fallback engine with
        // the task-C owner (macOS uses mtranServer here).
        SceneEngineBinding b;
        b.scene = scene;
        b.primaryEngine = TranslationEngineType::MtranServer;
        b.fallbackEngine = TranslationEngineType::Custom;
        b.fallbackEnabled = true;
        return b;
    }

    void toJson(Json& j) const {
        j = Json{{"scene", toRaw(scene)},
                 {"primaryEngine", toRaw(primaryEngine)},
                 {"fallbackEnabled", fallbackEnabled}};
        j["fallbackEngine"] = fallbackEngine ? Json(toRaw(*fallbackEngine)) : Json();
        j["customPrompt"] = customPrompt ? Json(*customPrompt) : Json();
    }
    static SceneEngineBinding fromJson(const Json& j) {
        SceneEngineBinding b;
        const std::string sceneRaw = j.at("scene").get<std::string>();
        b.scene = fromRaw(TranslationScene{}, sceneRaw);
        if (b.scene == TranslationScene::Invalid) {
            throw std::invalid_argument("unknown scene: " + sceneRaw);
        }
        const std::string primaryRaw = j.at("primaryEngine").get<std::string>();
        b.primaryEngine = fromRaw(TranslationEngineType{}, primaryRaw);
        if (b.primaryEngine == TranslationEngineType::Invalid) {
            throw std::invalid_argument("unknown primaryEngine: " + primaryRaw);
        }
        if (auto raw = getOptional<std::string>(j, "fallbackEngine")) {
            const TranslationEngineType e = fromRaw(TranslationEngineType{}, *raw);
            // macOS-written "apple" degrades to "no fallback" on Windows.
            if (e != TranslationEngineType::Invalid) { b.fallbackEngine = e; }
        }
        b.fallbackEnabled = getOptionalOr<bool>(j, "fallbackEnabled", true);
        b.customPrompt = getOptional<std::string>(j, "customPrompt");
        return b;
    }
};

inline void to_json(Json& j, const SceneEngineBinding& b) { b.toJson(j); }
inline void from_json(const Json& j, SceneEngineBinding& b) { b = SceneEngineBinding::fromJson(j); }

}  // namespace tf::core
