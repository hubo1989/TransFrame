// EnginesPageViewModelTests.cpp — 引擎页签 VM（engineConfigs /
// compatibleProviderConfigs 的 JSON 编解码纯函数、SetPersistence 装载、
// 显式 Save 经闭包落盘、链/对比/兼容实例限额）。
#include <catch2/catch_amalgamated.hpp>

#include "EnginesPageViewModel.hpp"

#include "tf/CoreModels.hpp"
#include "tf/EngineConfigStore.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <string>

using Catch::Approx;

using tf::ui::CompatibleInstanceRow;
using tf::ui::EnginesPageViewModel;
using tf::EngineConfig;

TEST_CASE("engine configs parse skips invalid entries and defaults absent",
          "[engines][settings]") {
    const std::string json = R"({
  "engineConfigs": {
    "openai": {"isEnabled": true,
               "options": {"baseURL": "http://x", "modelName": "m",
                            "timeout": 20, "maxTokens": 1000, "temperature": 0.3}},
    "mtran": {"isEnabled": false},
    "claude": "not an object",
    "gemini": {"isEnabled": false, "options": {"temperature": 9.9}}
  }
})";
    auto configs = EnginesPageViewModel::ParseEngineConfigs(json);
    REQUIRE(configs.size() == 3);                 // claude 非法条目跳过
    CHECK(configs.at("openai").isEnabled);
    CHECK(configs.at("openai").baseURL == "http://x");
    CHECK(configs.at("openai").modelName == "m");
    CHECK(configs.at("openai").timeoutSeconds.value_or(0) == Approx(20.0));
    CHECK(configs.at("openai").maxTokens.value_or(0) == 1000);
    CHECK(configs.at("openai").temperature.value_or(0) == Approx(0.3));
    CHECK_FALSE(configs.at("mtran").isEnabled);
    CHECK(configs.at("gemini").temperature.has_value() == false);  // 越界丢弃
}

TEST_CASE("engine configs serialize writes only set option fields",
          "[engines][settings]") {
    std::map<std::string, EngineConfig> configs;
    EngineConfig openai;
    openai.isEnabled = true;
    openai.baseURL = "http://y";
    openai.modelName = "gpt-test";
    configs["openai"] = openai;
    EngineConfig mtran;
    mtran.isEnabled = false;                      // options 全空 → 不写 options
    configs["mtran"] = mtran;

    const std::string json = EnginesPageViewModel::SerializeEngineConfigs(configs);
    nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    REQUIRE_FALSE(root.is_discarded());
    CHECK(root["openai"]["isEnabled"] == true);
    CHECK(root["openai"]["options"]["baseURL"] == "http://y");
    CHECK(root["openai"]["options"]["modelName"] == "gpt-test");
    CHECK(root["openai"]["options"].contains("timeout") == false);  // 未设置字段不写
    CHECK(root["mtran"]["options"].empty());       // options 为空对象（无字段）

    // round trip：序列化的是 engineConfigs 段，解析吃 engines.json 全文。
    const std::string fullText =
        R"({"engineConfigs": )" + json + "}";
    auto back = EnginesPageViewModel::ParseEngineConfigs(fullText);
    REQUIRE(back.size() == 2);
    CHECK(back.at("openai").baseURL == "http://y");
    CHECK(back.at("mtran").isEnabled == false);
}

TEST_CASE("compatible instances parse and serialize round trip",
          "[engines][settings]") {
    const std::string json = R"({
  "compatibleProviderConfigs": [
    {"id": "uuid-a", "displayName": "本地", "baseURL": "http://127.0.0.1:8000/v1",
     "modelName": "qwen", "hasAPIKey": false},
    {"id": "uuid-b", "displayName": "云端", "baseURL": "", "modelName": "",
     "hasAPIKey": true},
    {"displayName": "缺 id 跳过"}
  ]
})";
    auto rows = EnginesPageViewModel::ParseCompatibleInstances(json);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].displayName == "本地");
    CHECK(rows[0].hasAPIKey == false);
    CHECK(rows[1].id == "uuid-b");

    const std::string out = EnginesPageViewModel::SerializeCompatibleInstances(rows);
    nlohmann::json root = nlohmann::json::parse(out, nullptr, false);
    REQUIRE_FALSE(root.is_discarded());
    REQUIRE(root.size() == 2);
    CHECK(root[0]["id"] == "uuid-a");
    CHECK(root[1]["hasAPIKey"] == true);
    auto back = EnginesPageViewModel::ParseCompatibleInstances(
        R"({"compatibleProviderConfigs": )" + out + "}");
    REQUIRE(back.size() == 2);
    CHECK(back[0].baseURL == "http://127.0.0.1:8000/v1");
}

TEST_CASE("engines vm loads mirrors via persistence and saves through closure",
          "[engines][settings]") {
    std::string stored = R"({
  "engineConfigs": {"openai": {"isEnabled": true, "options": {"modelName": "m1"}}},
  "compatibleProviderConfigs": [],
  "vlmProvider": "openai"
})";
    std::string saved;
    // 测试镜像句柄（与 stored 一致；闭包按页面 BindPersistence 同款语义：
    // VM 全文 + engineConfigs 段覆盖）。
    std::map<std::string, tf::EngineConfig> vmMirror;
    tf::EngineConfig openai = tf::EngineConfig::make(tf::TranslationEngineType::OpenAI);
    openai.isEnabled = true;
    openai.modelName = "m1";
    vmMirror["openai"] = openai;

    EnginesPageViewModel::Persistence persistence;
    persistence.loadEnginesJson = [&] { return stored; };
    persistence.saveEnginesJson = [&](const std::string& text) {
        nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
        if (root.is_discarded() || !root.is_object()) root = nlohmann::json::object();
        root["engineConfigs"] = nlohmann::json::parse(
            EnginesPageViewModel::SerializeEngineConfigs(vmMirror), nullptr, false);
        saved = root.dump(2) + "\n";
    };

    EnginesPageViewModel vm(persistence);
    CHECK(vm.VlmProvider() == "openai");
    CHECK(vm.EngineConfigMirror().at("openai").modelName == "m1");

    vm.SetVlmBaseURL("http://localhost:11434/v1");
    REQUIRE(vm.SaveVlm());                        // 显式 Save → 闭包收到全文
    nlohmann::json root = nlohmann::json::parse(saved, nullptr, false);
    REQUIRE_FALSE(root.is_discarded());
    CHECK(root["vlmBaseURL"] == "http://localhost:11434/v1");  // Core 扁平键
    CHECK(root["engineConfigs"]["openai"]["options"]["modelName"] == "m1");

    stored = saved;                               // SaveVlm 后重绑（页 OnShown 同路径）
    vm.SetPersistence(persistence);
    CHECK(vm.VlmBaseURL() == "http://localhost:11434/v1");
    CHECK(vm.EngineConfigMirror().at("openai").modelName == "m1");
}

TEST_CASE("engines vm save without closure returns false", "[engines][settings]") {
    EnginesPageViewModel vm;                      // 纯内存模式
    CHECK_FALSE(vm.SaveVlm());
    CHECK_FALSE(vm.SaveEngineConfigs());
}

TEST_CASE("engines vm builtin toggles drive the enable mirror", "[engines][settings]") {
    EnginesPageViewModel vm;
    const auto raws = vm.BuiltinCardEngineRaws();
    REQUIRE(raws.size() >= 8);                    // 9 卡（spec §2 分组）
    CHECK(vm.BuiltinEnabled(raws.front()) == false);  // 未配置默认关
    vm.SetBuiltinEnabled(raws.front(), true);
    CHECK(vm.BuiltinEnabled(raws.front()) == true);
}

TEST_CASE("engines vm chain and compatible limits", "[engines][settings]") {
    EnginesPageViewModel vm;
    for (int i = 0; i < 5; ++i) {
        REQUIRE(vm.CanAddChainRow());
        tf::ui::ChainRowViewModel row;
        row.builtinEngineRaw = "apple";
        vm.AppendChainRow(row);
    }
    CHECK(vm.ChainCountText() == "5/5");
    CHECK_FALSE(vm.CanAddChainRow());             // 链上限 5（spec §2.3）
    CHECK(vm.CanRemoveChainRow());
    vm.RemoveChainRow(0);
    CHECK(vm.ChainCountText() == "4/5");

    for (int i = 0; i < 5; ++i) {
        REQUIRE(vm.CanAddCompatibleInstance());
        CompatibleInstanceRow row;
        row.id = "uuid-" + std::to_string(i);
        row.displayName = "inst" + std::to_string(i);
        vm.AppendCompatibleInstance(row);
    }
    CHECK(vm.CompatibleCountText() == "5/5");
    CHECK(vm.CompatibleLimitReached());           // 兼容实例上限 5（spec §2.6）
    vm.RemoveCompatibleInstance("uuid-2");
    CHECK(vm.CompatibleInstances().size() == 4);

    // 空 id 行原样进入（UUID 由页面 NewCompatibleSheetDraft 路径生成后回填）。
    CompatibleInstanceRow blank;
    blank.displayName = "blank";
    vm.AppendCompatibleInstance(blank);
    CHECK(vm.CompatibleInstances().back().displayName == "blank");
}
