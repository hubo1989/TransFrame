// windows/UI/EnginesPage/EnginesPageTests.cpp
// FR-35 引擎页签 ViewModel 单元测试（纯 C++ 部分；XAML code-behind 不在
// macOS 可编译范围，按任务说明以自查清单覆盖）。
//
// 编译进 Core 测试工程（windows/Tests/TransFrame.Core.Tests.vcxproj，
// 集成由主线做——本分支不改工程文件，见任务说明第 5 点；测试源文件放
// 本目录，编译期 catch_discover / 手动注册即可运行）。
#include <catch2/catch_amalgamated.hpp>

#include "EnginesPageViewModel.hpp"

#include <sstream>

using tf::ui::EnginesPageViewModel;
using tf::ui::ChainRowViewModel;
using tf::ui::CompatibleInstanceRow;
using tf::ui::kEngineChainLimit;
using tf::ui::kCompareEnginesLimit;
using tf::ui::kCompatibleInstancesLimit;

namespace {

ChainRowViewModel BuiltinRow(const std::string& raw) {
    ChainRowViewModel r;
    r.builtinEngineRaw = raw;
    r.displayName = raw;
    return r;
}
CompatibleInstanceRow MakeCompat(const std::string& id, const std::string& name) {
    CompatibleInstanceRow c;
    c.id = id; c.displayName = name;
    c.baseURL = "http://localhost:8000/v1";
    c.modelName = "m";
    return c;
}
std::string JoinRaws(const std::vector<ChainRowViewModel>& rows) {
    std::string s;
    for (auto const& r : rows) s += (r.isCompatible ? r.compatibleId : r.builtinEngineRaw) + ",";
    return s;
}

}  // namespace

TEST_CASE("EnginesPageViewModel chain add/remove/move/limits", "[ui][engines]") {
    EnginesPageViewModel vm;
    // 默认链：[apple]（Core makeDefault）
    REQUIRE(vm.ChainRows().size() == 1);
    REQUIRE(vm.ChainRows()[0].builtinEngineRaw == "apple");
    REQUIRE(vm.ChainCountText() == "1/5");

    // 加到 5 上限
    for (int i = 0; i < 4; ++i) vm.AppendChainRow(BuiltinRow("mtran"));
    REQUIRE(vm.ChainRows().size() == 5);
    REQUIRE(vm.ChainCountText() == "5/5");
    REQUIRE_FALSE(vm.CanAddChainRow());

    // 上限后 append 无效
    vm.AppendChainRow(BuiltinRow("openai"));
    REQUIRE(vm.ChainRows().size() == 5);

    // 删到 1 下限
    for (int i = 0; i < 4; ++i) vm.RemoveChainRow(vm.ChainRows().size() - 1);
    REQUIRE(vm.ChainRows().size() == 1);
    REQUIRE_FALSE(vm.CanRemoveChainRow());
    vm.RemoveChainRow(0);
    REQUIRE(vm.ChainRows().size() == 1);  // 下限保护

    // 移动：[mtran, apple] -> move(0,1) -> [apple, mtran]
    vm.RemoveChainRow(0);
    vm.AppendChainRow(BuiltinRow("mtran"));
    vm.AppendChainRow(BuiltinRow("openai"));
    vm.MoveChainRow(2, 0);
    REQUIRE(JoinRaws(vm.ChainRows()) == "openai,apple,mtran,");

    // 越界安全
    vm.MoveChainRow(99, 0);
    vm.RemoveChainRow(99);
    REQUIRE(vm.ChainRows().size() == 3);
}

TEST_CASE("EnginesPageViewModel compare set 2..4 with enforced minimum", "[ui][engines]") {
    EnginesPageViewModel vm;
    // Core 默认 compareEnginesV2 = [apple, mtran]
    REQUIRE(vm.CompareRows().size() == 2);
    REQUIRE(vm.CompareCountText() == "2/4");
    REQUIRE(vm.CanAddCompareRow());

    vm.AppendCompareRow(BuiltinRow("openai"));
    vm.AppendCompareRow(BuiltinRow("google"));
    REQUIRE(vm.CompareCountText() == "4/4");
    REQUIRE_FALSE(vm.CanAddCompareRow());
    vm.AppendCompareRow(BuiltinRow("deepl"));
    REQUIRE(vm.CompareRows().size() == 4);

    vm.RemoveCompareRow(0);
    REQUIRE(vm.CompareRows().size() == 3);
    REQUIRE(vm.CanRemoveCompareRow());
    vm.RemoveCompareRow(0);
    REQUIRE(vm.CompareRows().size() == 2);
    REQUIRE_FALSE(vm.CanRemoveCompareRow());  // 2 = minimum
    vm.RemoveCompareRow(0);
    REQUIRE(vm.CompareRows().size() == 2);
}

TEST_CASE("EnginesPageViewModel builtin enable default and candidates", "[ui][engines]") {
    EnginesPageViewModel vm;
    // 9 卡片：mtran..custom（CoreModels allTranslationEngines 顺序）
    auto raws = vm.BuiltinCardEngineRaws();
    REQUIRE(raws.size() == 9);
    REQUIRE(raws[0] == "mtran");
    REQUIRE(raws[8] == "custom");

    // 默认：apple enabled（条目缺失 = true），其余 false
    REQUIRE(vm.BuiltinEnabled("apple"));
    REQUIRE_FALSE(vm.BuiltinEnabled("mtran"));
    REQUIRE_FALSE(vm.BuiltinEnabled("custom"));

    // custom 不可选（添加菜单排除）
    REQUIRE_FALSE(vm.BuiltinSelectable("custom"));
    REQUIRE(vm.BuiltinSelectable("mtran"));

    // 候选 = apple + isEnabled 内置（排除 custom）+ 兼容实例
    auto cands = vm.ChainCandidates();
    REQUIRE(cands.size() == 1);  // 仅 apple（其余未启用、无兼容实例）
    REQUIRE(cands[0].builtinEngineRaw == "apple");

    vm.SetBuiltinEnabled("openai", true);
    vm.SetBuiltinEnabled("custom", true);  // 即使启用也不进候选
    vm.AppendCompatibleInstance(MakeCompat("11111111-1111-1111-1111-111111111111", "MyLocal"));
    cands = vm.ChainCandidates();
    REQUIRE(cands.size() == 3);  // apple, openai, 兼容实例
    REQUIRE(cands[2].isCompatible);
    REQUIRE(cands[2].compatibleId == "11111111-1111-1111-1111-111111111111");
    REQUIRE(cands[2].displayName == "MyLocal");
}

TEST_CASE("EnginesPageViewModel compatible instances limit 5 and in-use flag", "[ui][engines]") {
    EnginesPageViewModel vm;
    for (int i = 0; i < 5; ++i) {
        CompatibleInstanceRow c;
        c.id = "id-" + std::to_string(i);
        c.displayName = "C" + std::to_string(i);
        vm.AppendCompatibleInstance(c);
    }
    REQUIRE(vm.CompatibleCountText() == "5/5");
    REQUIRE(vm.CompatibleLimitReached());
    CompatibleInstanceRow sixth;
    sixth.id = "id-5"; sixth.displayName = "C5";
    vm.AppendCompatibleInstance(sixth);
    REQUIRE(vm.CompatibleInstances().size() == 5);

    // 引用状态：把 id-0 放进链 → InUse
    REQUIRE_FALSE(vm.CompatibleInUse("id-0"));
    ChainRowViewModel row;
    row.isCompatible = true;
    row.compatibleId = "id-0";
    row.displayName = "C0";
    vm.AppendChainRow(row);
    REQUIRE(vm.CompatibleInUse("id-0"));
    REQUIRE_FALSE(vm.CompatibleInUse("id-1"));

    // 查找
    REQUIRE(vm.FindCompatible("id-3").displayName == "C3");
    REQUIRE(vm.FindCompatible("nope").id.empty());
}

TEST_CASE("EnginesPageViewModel VLM + PaddleOCR mirror + persistence protocol", "[ui][engines]") {
    // 纯内存模式（无持久化闭包）：Save* 返回 false 但不崩溃
    {
        EnginesPageViewModel vm;
        REQUIRE(vm.IsValidVlmProvider("openai"));
        REQUIRE_FALSE(vm.IsValidVlmProvider("nope"));
        vm.SetVlmProvider("gemini");
        REQUIRE(vm.VlmProvider() == "gemini");
        REQUIRE_FALSE(vm.VlmShowsOpenAIMode());  // 非 openai 隐藏模式选择
        vm.SetVlmProvider("openai");
        REQUIRE(vm.VlmShowsOpenAIMode());
        vm.SetVlmOpenAIMode("responses");
        REQUIRE(vm.VlmOpenAIMode() == "responses");
        REQUIRE_FALSE(vm.SaveVlm());  // 无 save 闭包

        REQUIRE_FALSE(vm.PaddleOcrInstalled());
        REQUIRE(vm.PaddleOcrVersionText() == "未安装");
        vm.SetPaddleOcrVersionText("3.0.0");
        REQUIRE(vm.PaddleOcrInstalled());
        vm.SetPaddleOcrMode("precise");
        vm.SetPaddleOcrUseCloud(true);
        vm.SetPaddleOcrCloudBaseURL("http://cloud:9/v1");
        vm.SetPaddleOcrCloudModelId("vl5");
        vm.SetPaddleOcrCloudAPIKey("sk-test");
        REQUIRE(vm.PaddleOcrCloudHasAPIKey());
        REQUIRE_FALSE(vm.SavePaddleOcr());
    }

    // 带持久化闭包：显式 Save 写 JSON；round-trip 经 Core parse
    {
        std::string saved;
        int saveCount = 0;
        EnginesPageViewModel::Persistence p;
        p.saveEnginesJson = [&](const std::string& json) { saved = json; ++saveCount; };
        p.loadEnginesJson = [&]() { return saved; };
        {
            EnginesPageViewModel vm(std::move(p));
            REQUIRE(vm.EngineModeV2() == "ordered");
            vm.SetEngineModeV2("compare");
            REQUIRE(vm.IsCompareMode());
            REQUIRE(vm.SaveEngineModeV2());
            REQUIRE(saveCount == 1);
            REQUIRE_FALSE(saved.empty());
        }
        {
            EnginesPageViewModel::Persistence p2;
            p2.loadEnginesJson = [&]() { return saved; };
            EnginesPageViewModel vm2(std::move(p2));
            REQUIRE(vm2.EngineModeV2() == "compare");
            REQUIRE(vm2.CompareRows().size() == 2);  // Core 默认 [apple, mtran]
        }
    }
}

TEST_CASE("EnginesPageViewModel sheet drafts commit/discard semantics", "[ui][engines]") {
    EnginesPageViewModel vm;
    // 内置 draft：默认值（未配置 → make(engine) 默认）
    auto d = vm.OpenBuiltinSheetDraft("mtran");
    REQUIRE(d.engine == tf::TranslationEngineType::MtranServer);
    REQUIRE_FALSE(d.isEnabled);
    // 提交
    d.isEnabled = true;
    d.baseURL = "http://mtran.local:8989";
    vm.CommitBuiltinSheet(d);
    REQUIRE(vm.BuiltinEnabled("mtran"));
    REQUIRE(vm.OpenBuiltinSheetDraft("mtran").baseURL == "http://mtran.local:8989");

    // OpenAI 预设联动
    vm.SetVlmBaseURL("https://api.openai.com/v1");
    vm.SetOpenAIPreset("openAI");
    REQUIRE(vm.OpenAIShowsAPIKey());
    vm.SetOpenAIPreset("ollamaLocal");
    REQUIRE_FALSE(vm.OpenAIShowsAPIKey());
    vm.SetOpenAIPreset("custom");
    vm.SetVlmBaseURL("http://localhost:11434");
    REQUIRE_FALSE(vm.OpenAIShowsAPIKey());  // custom + 本地 URL 隐藏
    vm.SetVlmBaseURL("https://api.openai.com/v1");
    REQUIRE(vm.OpenAIShowsAPIKey());

    // 兼容 draft：新建模板 + 提交 + 更新
    auto nd = vm.NewCompatibleSheetDraft();
    REQUIRE(nd.baseURL == "http://localhost:8000/v1");
    nd.id = "22222222-2222-2222-2222-222222222222";
    nd.displayName = "Local vLLM";
    vm.CommitCompatibleSheet(nd);
    REQUIRE(vm.CompatibleInstances().size() == 1);
    nd.displayName = "Local vLLM 2";
    vm.CommitCompatibleSheet(nd);  // 更新（同 id）
    REQUIRE(vm.CompatibleInstances().size() == 1);
    REQUIRE(vm.FindCompatible(nd.id).displayName == "Local vLLM 2");
}

TEST_CASE("EnginesPageViewModel health four-state + colors", "[ui][engines]") {
    EnginesPageViewModel vm;
    REQUIRE(vm.HealthOf("mtran") == "gray");           // 默认灰
    vm.SetHealthOf("mtran", "green");
    vm.SetHealthOf("openai", "red");
    vm.SetHealthOf("gemini", "orange");
    REQUIRE(vm.HealthOf("mtran") == "green");
    REQUIRE(vm.HealthColorHex("mtran") == "#10893E");
    REQUIRE(vm.HealthColorHex("openai") == "#C42B1C");
    REQUIRE(vm.HealthColorHex("gemini") == "#FF8C00");
    REQUIRE(vm.HealthColorHex("unknown-key") == "#9E9E9E");
}

TEST_CASE("EnginesPageViewModel chain candidate excludes disabled engines", "[ui][engines]") {
    EnginesPageViewModel vm;
    vm.SetBuiltinEnabled("mtran", true);
    vm.SetBuiltinEnabled("google", true);
    auto cands = vm.ChainCandidates();
    // apple（默认启用）+ mtran + google
    REQUIRE(cands.size() == 3);
    REQUIRE(cands[0].builtinEngineRaw == "apple");
    REQUIRE(cands[1].builtinEngineRaw == "mtran");
    REQUIRE(cands[2].builtinEngineRaw == "google");

    // 禁用后从候选消失
    vm.SetBuiltinEnabled("google", false);
    REQUIRE(vm.ChainCandidates().size() == 2);
}
