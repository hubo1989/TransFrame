// PromptsPageViewModelTests.cpp — Prompt 页签 VM（三层 prompt 的
// parse/serialize 纯函数、空文本清除语义、resolve 分层预览、
// mergeIntoEngineConfigJson 未知键保留、变更回调）。
#include <catch2/catch_amalgamated.hpp>

#include "PromptsPageViewModel.h"

#include "tf/CoreModels.hpp"
#include "tf/TranslationProvider.hpp"

#include <nlohmann/json.hpp>

#include <string>

using Catch::Approx;

using tf::ui::PromptsPageViewModel;
using tf::TranslationPromptConfig;
using tf::TranslationScene;
using tf::TranslationEngineType;

TEST_CASE("prompts vm static catalogs match spec", "[prompts][settings]") {
    REQUIRE(PromptsPageViewModel::promptableEngines().size() == 4);
    CHECK(PromptsPageViewModel::promptableEngines()[0] == TranslationEngineType::OpenAI);
    CHECK(PromptsPageViewModel::promptableEngines()[3] == TranslationEngineType::Ollama);
    REQUIRE(PromptsPageViewModel::sceneOrder().size() == 3);
    CHECK(PromptsPageViewModel::sceneOrder()[0] == TranslationScene::Screenshot);
    CHECK(PromptsPageViewModel::sceneOrder()[2] == TranslationScene::TranslateAndInsert);
}

TEST_CASE("prompts parse accepts engines.json full text and bare object",
          "[prompts][settings]") {
    const std::string full = R"({
  "promptConfig": {
    "enginePrompts": {"openai": "E", "claude": "C"},
    "compatibleEnginePrompts": {"uuid-1": "K1"},
    "scenePrompts": {"screenshot": "S"}
  },
  "otherSection": true
})";
    auto fromFull = PromptsPageViewModel::parsePromptConfig(full);
    REQUIRE(fromFull.ok);
    PromptsPageViewModel vmFull;
    vmFull.loadConfig(fromFull.config);
    CHECK(vmFull.enginePrompt(TranslationEngineType::OpenAI) == "E");

    auto bare = PromptsPageViewModel::parsePromptConfig(
        R"({"enginePrompts":{"gemini":"G"}})");
    REQUIRE(bare.ok);
    PromptsPageViewModel vmBare;
    vmBare.loadConfig(bare.config);
    CHECK(vmBare.enginePrompt(TranslationEngineType::Gemini) == "G");

    auto bad = PromptsPageViewModel::parsePromptConfig("not json at all");
    CHECK_FALSE(bad.ok);
    CHECK_FALSE(bad.error.empty());
}

TEST_CASE("prompts parse filters non-promptable engines and bad types",
          "[prompts][settings]") {
    auto parsed = PromptsPageViewModel::parsePromptConfig(R"({
  "enginePrompts": {"openai": "E", "mtran": "M"},
  "scenePrompts": {"screenshot": "S", "bogusScene": "X"},
  "compatibleEnginePrompts": {"id2": "K2", "notString": 5}
})");
    REQUIRE(parsed.ok);
    PromptsPageViewModel vm;
    vm.loadConfig(parsed.config);
    CHECK(vm.hasEnginePrompt(TranslationEngineType::OpenAI));
    CHECK_FALSE(vm.hasEnginePrompt(TranslationEngineType::MtranServer));  // schema 限四种
    CHECK(vm.hasScenePrompt(TranslationScene::Screenshot));
    CHECK(vm.compatiblePrompt("id2") == "K2");     // 非字符串条目跳过
    CHECK_FALSE(vm.hasCompatiblePrompt("notString"));
}

TEST_CASE("prompts serialize round trips all three layers", "[prompts][settings]") {
    TranslationPromptConfig cfg;
    cfg.enginePrompts.emplace_back(TranslationEngineType::Claude, "C text");
    cfg.compatibleEnginePrompts.emplace_back("uuid-9", "K text");
    cfg.scenePrompts.emplace_back(TranslationScene::TextSelection, "TS text");

    const std::string json = PromptsPageViewModel::serializePromptConfig(cfg);
    auto back = PromptsPageViewModel::parsePromptConfig(json);
    REQUIRE(back.ok);
    CHECK(back.config.enginePrompts.size() == 1);
    CHECK(back.config.compatibleEnginePrompts.size() == 1);
    CHECK(back.config.scenePrompts.size() == 1);
    CHECK(back.config.enginePrompts[0].second == "C text");
    CHECK(back.config.compatibleEnginePrompts[0].first == "uuid-9");
    CHECK(back.config.scenePrompts[0].first == TranslationScene::TextSelection);
}

TEST_CASE("prompts set empty text erases the layer entry", "[prompts][settings]") {
    PromptsPageViewModel vm;
    vm.setEnginePrompt(TranslationEngineType::OpenAI, "");
    CHECK_FALSE(vm.hasEnginePrompt(TranslationEngineType::OpenAI));  // 空文本不建空条目
    vm.setEnginePrompt(TranslationEngineType::OpenAI, "custom");
    CHECK(vm.hasEnginePrompt(TranslationEngineType::OpenAI));
    vm.setEnginePrompt(TranslationEngineType::OpenAI, "");           // 保存空 = 清除
    CHECK_FALSE(vm.hasEnginePrompt(TranslationEngineType::OpenAI));
}

TEST_CASE("prompts resolve layers scene over compatible over engine over defaults",
          "[prompts][settings]") {
    PromptsPageViewModel vm;
    vm.loadConfig({});
    vm.setEnginePrompt(TranslationEngineType::Custom, "ENGINE");
    vm.setCompatiblePrompt("uuid-1", "COMPAT");
    vm.setScenePrompt(TranslationScene::Screenshot, "SCENE");

    // 兼容层只在 engine==Custom 且指定实例 id 时参与。
    CHECK(vm.resolvePreview(TranslationEngineType::Custom, TranslationScene::Screenshot,
                            "uuid-1") == "SCENE");
    CHECK(vm.resolvePreview(TranslationEngineType::Custom, TranslationScene::Screenshot,
                            "") == "SCENE");       // 场景优先，无兼容 id
    // 无场景条目 → 兼容实例层
    CHECK(vm.resolvePreview(TranslationEngineType::Custom, TranslationScene::TextSelection,
                            "uuid-1") == "COMPAT");
    // 非 Custom 引擎 → 跳过兼容层落到引擎层
    vm.setEnginePrompt(TranslationEngineType::OpenAI, "OPENAI");
    CHECK(vm.resolvePreview(TranslationEngineType::OpenAI, TranslationScene::TextSelection,
                            "uuid-1") == "OPENAI");
    // 插入场景无任何条目 → 插入默认模板；无条目场景 → 全局默认
    CHECK(vm.resolvePreview(TranslationEngineType::Gemini,
                            TranslationScene::TranslateAndInsert) ==
          PromptsPageViewModel::defaultInsertPromptText());
    CHECK(vm.resolvePreview(TranslationEngineType::Gemini,
                            TranslationScene::TextSelection) ==
          PromptsPageViewModel::defaultPromptText());
}

TEST_CASE("prompts merge preserves other engines.json sections", "[prompts][settings]") {
    const std::string existing = R"({
  "targetLang": "zh",
  "engineChainV2": [{"engine": "apple"}],
  "futureKey": [1, 2]
})";
    TranslationPromptConfig cfg;
    cfg.enginePrompts.emplace_back(TranslationEngineType::Ollama, "O");
    const std::string merged =
        PromptsPageViewModel::mergeIntoEngineConfigJson(existing, cfg);

    nlohmann::json root = nlohmann::json::parse(merged, nullptr, false);
    REQUIRE_FALSE(root.is_discarded());
    CHECK(root["promptConfig"]["enginePrompts"]["ollama"] == "O");
    CHECK(root["targetLang"] == "zh");            // 其它分支键保留
    CHECK(root["engineChainV2"].is_array());
    CHECK(root["futureKey"].size() == 2);

    // 非法 existing → 以仅含 promptConfig 的新对象重建
    const std::string rebuilt =
        PromptsPageViewModel::mergeIntoEngineConfigJson("{ broken", cfg);
    auto root2 = nlohmann::json::parse(rebuilt, nullptr, false);
    REQUIRE_FALSE(root2.is_discarded());
    CHECK(root2.contains("promptConfig"));
}

TEST_CASE("prompts change handler fires on every layer edit", "[prompts][settings]") {
    PromptsPageViewModel vm;
    int fired = 0;
    vm.setChangedHandler([&] { ++fired; });
    vm.setEnginePrompt(TranslationEngineType::OpenAI, "a");
    vm.setCompatiblePrompt("i", "b");
    vm.setScenePrompt(TranslationScene::Screenshot, "c");
    CHECK(fired == 3);
}
