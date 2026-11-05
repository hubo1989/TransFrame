#pragma once
// PromptsPageViewModel.h — 「Prompt」页签 ViewModel（普通 C++ 类，非 WinRT runtime
// class，由 PromptsPage code-behind 持有）。包装 Core 的 tf::TranslationPromptConfig /
// tf::CompatibleConfig / 场景与引擎枚举，不重复定义任何字段。
//
// [集成点] 工程包含路径需含 windows/Core/include 与 windows/Core/vendor
//          （VM 依赖 tf/CoreModels.hpp / tf/TranslationProvider.hpp /
//          tf/Json.hpp → nlohmann/json.hpp）。
//
// 持久化（settings-spec-mac-parity.md §3 末尾）：promptConfig JSON =
//   { "enginePrompts":            { engineRawValue: string },
//     "compatibleEnginePrompts":  { uuid字符串: string },
//     "scenePrompts":             { sceneRawValue: string } }
// serialize/parse 为纯函数（string in/out），对齐 tf::core::parseEngineConfig /
// serializeEngineConfigPatch 的模式；实际落盘 engines.json 由 shell 层完成
// （注册 setChangedHandler，在回调里 mergeIntoEngineConfigJson +
//  tf::core::writeEngineConfigFileAtomic）。

#include "tf/CoreModels.hpp"
#include "tf/Json.hpp"
#include "tf/TranslationProvider.hpp"  // CompatibleConfig（兼容实例，schema compatibleProviderConfigs）

#include <functional>
#include <string>
#include <vector>

namespace tf::ui {

class PromptsPageViewModel {
public:
    /// spec §3.1：支持自定义 Prompt 的内置引擎固定四种（页面行顺序）。
    /// schema 键限 openai/claude/gemini/ollama（settings-config-schema.md promptConfig）。
    static std::vector<TranslationEngineType> const& promptableEngines() {
        static std::vector<TranslationEngineType> const kEngines = {
            TranslationEngineType::OpenAI,
            TranslationEngineType::Claude,
            TranslationEngineType::Gemini,
            TranslationEngineType::Ollama,
        };
        return kEngines;
    }

    /// spec §3.3：场景行顺序（TranslationScene.allCases 对应顺序）。
    static std::vector<TranslationScene> const& sceneOrder() {
        static std::vector<TranslationScene> const kScenes = {
            TranslationScene::Screenshot,
            TranslationScene::TextSelection,
            TranslationScene::TranslateAndInsert,
        };
        return kScenes;
    }

    // ---- 兼容引擎实例（spec §3.2）----
    // 列表由 shell 注入（引擎页签管理的 compatibleProviderConfigs，键 UUID 字符串）；
    // 本 VM 不拥有其持久化，只按 displayName 驱动 UI 行。
    void setCompatibleInstances(std::vector<CompatibleConfig> instances) {
        m_compatible = std::move(instances);
    }
    std::vector<CompatibleConfig> const& compatibleInstances() const { return m_compatible; }
    CompatibleConfig const* findCompatibleInstance(std::string const& id) const {
        for (auto const& inst : m_compatible)
            if (inst.id == id) return &inst;
        return nullptr;
    }

    // ---- 引擎级（enginePrompts）----
    bool hasEnginePrompt(TranslationEngineType engine) const {
        return contains(m_config.enginePrompts, engine);
    }
    std::string enginePrompt(TranslationEngineType engine) const {
        return lookup(m_config.enginePrompts, engine);
    }
    void setEnginePrompt(TranslationEngineType engine, std::string const& text) {
        upsert(m_config.enginePrompts, engine, text);
        notifyChanged();
    }

    // ---- 兼容实例级（compatibleEnginePrompts，key = 实例 UUID 字符串）----
    bool hasCompatiblePrompt(std::string const& instanceId) const {
        return contains(m_config.compatibleEnginePrompts, instanceId);
    }
    std::string compatiblePrompt(std::string const& instanceId) const {
        return lookup(m_config.compatibleEnginePrompts, instanceId);
    }
    void setCompatiblePrompt(std::string const& instanceId, std::string const& text) {
        upsert(m_config.compatibleEnginePrompts, instanceId, text);
        notifyChanged();
    }

    // ---- 场景级（scenePrompts）----
    bool hasScenePrompt(TranslationScene scene) const {
        return contains(m_config.scenePrompts, scene);
    }
    std::string scenePrompt(TranslationScene scene) const {
        return lookup(m_config.scenePrompts, scene);
    }
    void setScenePrompt(TranslationScene scene, std::string const& text) {
        upsert(m_config.scenePrompts, scene, text);
        notifyChanged();
    }

    // ---- 默认模板：直接调用 Core（CoreModels.cpp TranslationPromptConfig::
    //      defaultPrompt / defaultInsertPrompt），不在 UI 层抄写模板文本 ----
    static std::string const& defaultPromptText() { return TranslationPromptConfig::defaultPrompt(); }
    static std::string const& defaultInsertPromptText() { return TranslationPromptConfig::defaultInsertPrompt(); }

    /// 解析优先级预览：完全复用 Core resolve——场景 > 兼容实例（仅当
    /// engine==Custom 且 compatibleId 非空时参与）> 引擎 > 插入场景默认 >
    /// 全局默认。
    /// [待确认] spec §3 末尾「兼容引擎 prompt 与场景 prompt 的叠加次序细节」——
    /// 当前保守选择：以 Core TranslationPromptConfig::resolve 的实现为唯一权威，
    /// UI 层不另行叠加或改写次序。
    std::string resolvePreview(TranslationEngineType engine, TranslationScene scene,
                               std::string const& compatibleId = "") const {
        return m_config.resolve(engine, scene, compatibleId);
    }

    // ---- 持久化：纯函数，string in/out（对齐 EngineConfigStore 模式）----
    struct ParseResult {
        TranslationPromptConfig config;  // 缺字段 / 类型不符 → 回落空配置（分层默认）
        bool ok = true;                  // 仅当输入不是合法 JSON 对象时为 false
        std::string error;               // !ok 时的人类可读原因
    };

    /// 解析 engines.json 全文（读取其中 "promptConfig" 成员）或裸 promptConfig 对象。
    /// 容忍未知键与类型不符（对齐 tf::core::parseEngineConfig）；enginePrompts 键限
    /// openai/claude/gemini/ollama，其余引擎 raw 一律跳过。
    static ParseResult parsePromptConfig(std::string const& jsonUtf8) {
        ParseResult result;
        Json parsed = Json::parse(jsonUtf8, /*callback*/ nullptr, /*allow_exceptions*/ false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            result.ok = false;
            result.error = "prompt config JSON is not a valid object";
            return result;
        }
        Json const* root = &parsed;
        if (parsed.contains("promptConfig")) {
            Json const& pc = parsed.at("promptConfig");
            if (!pc.is_object()) {
                result.ok = false;
                result.error = "promptConfig is not an object";
                return result;
            }
            root = &pc;
        }

        auto readStringMap = [&](char const* key, auto&& onEntry) {
            if (!root->contains(key)) return;
            Json const& m = root->at(key);
            if (!m.is_object()) return;  // 类型不符容忍跳过
            for (auto it = m.begin(); it != m.end(); ++it) {
                if (!it.value().is_string()) continue;
                onEntry(it.key(), it.value().get<std::string>());
            }
        };

        readStringMap("enginePrompts", [&](std::string const& k, std::string const& v) {
            auto engine = translationEngineFromRaw(k);
            if (engine && isPromptableEngine(*engine))
                result.config.enginePrompts.emplace_back(*engine, v);
        });
        readStringMap("compatibleEnginePrompts", [&](std::string const& k, std::string const& v) {
            result.config.compatibleEnginePrompts.emplace_back(k, v);
        });
        readStringMap("scenePrompts", [&](std::string const& k, std::string const& v) {
            if (auto scene = translationSceneFromRaw(k))
                result.config.scenePrompts.emplace_back(*scene, v);
        });
        return result;
    }

    /// 序列化为 promptConfig 对象（spec §3 末尾结构；dump(2) 风格与
    /// EngineConfigStore serializeEngineConfigPatch 一致）。
    static std::string serializePromptConfig(TranslationPromptConfig const& cfg) {
        Json enginePrompts = Json::object();
        for (auto const& [engine, prompt] : cfg.enginePrompts)
            enginePrompts[translationEngineRaw(engine)] = prompt;
        Json compatibleEnginePrompts = Json::object();
        for (auto const& [id, prompt] : cfg.compatibleEnginePrompts)
            compatibleEnginePrompts[id] = prompt;
        Json scenePrompts = Json::object();
        for (auto const& [scene, prompt] : cfg.scenePrompts)
            scenePrompts[translationSceneRaw(scene)] = prompt;

        Json out = Json::object();
        out["enginePrompts"] = std::move(enginePrompts);
        out["compatibleEnginePrompts"] = std::move(compatibleEnginePrompts);
        out["scenePrompts"] = std::move(scenePrompts);
        return out.dump(2);
    }

    /// 把 promptConfig 合并进 engines.json 全文，保留其余键原样（对齐
    /// tf::core::serializeEngineConfigPatch 的「未知键保留」语义；其他分支
    /// 拥有的 section 不受影响）。existingJson 为空或非法 → 以仅含
    /// promptConfig 的新对象重建。
    static std::string mergeIntoEngineConfigJson(std::string const& existingJsonUtf8,
                                                 TranslationPromptConfig const& cfg) {
        Json root = Json::parse(existingJsonUtf8, nullptr, false);
        if (root.is_discarded() || !root.is_object())
            root = Json::object();
        root["promptConfig"] = Json::parse(serializePromptConfig(cfg), nullptr, false);
        return root.dump(2) + "\n";
    }

    // ---- 配置装载 / 读取 ----
    void loadConfig(TranslationPromptConfig cfg) {
        m_config = std::move(cfg);
        notifyChanged();
    }
    TranslationPromptConfig const& config() const { return m_config; }

    /// [集成点] shell 层注册：任一 Prompt 变更后回调（在回调里落盘
    /// engines.json：mergeIntoEngineConfigJson + writeEngineConfigFileAtomic）。
    void setChangedHandler(std::function<void()> handler) { m_onChanged = std::move(handler); }

private:
    static bool isPromptableEngine(TranslationEngineType e) {
        for (auto engine : promptableEngines())
            if (engine == e) return true;
        return false;
    }

    // 三层 overlay 的 vector<pair> 查改（键类型 K 各不相同，逻辑同构）。
    template <typename K>
    static bool contains(std::vector<std::pair<K, std::string>> const& v, K const& key) {
        for (auto const& p : v)
            if (p.first == key) return true;
        return false;
    }

    template <typename K>
    static std::string lookup(std::vector<std::pair<K, std::string>> const& v, K const& key) {
        for (auto const& p : v)
            if (p.first == key) return p.second;
        return {};
    }

    /// [待确认] 空文本保存语义（spec §3.5 未写明）。当前保守选择：
    /// 保存空文本 = 清除该层自定义（等价未设置，resolve 回落下一层默认），
    /// 不保留空串条目。
    template <typename K>
    static void upsert(std::vector<std::pair<K, std::string>>& v, K const& key,
                       std::string const& text) {
        for (auto it = v.begin(); it != v.end(); ++it) {
            if (it->first == key) {
                if (text.empty())
                    v.erase(it);
                else
                    it->second = text;
                return;
            }
        }
        if (!text.empty())
            v.emplace_back(key, text);
    }

    void notifyChanged() {
        if (m_onChanged) m_onChanged();
    }

    TranslationPromptConfig m_config;
    std::vector<CompatibleConfig> m_compatible;
    std::function<void()> m_onChanged;
};

}  // namespace tf::ui
