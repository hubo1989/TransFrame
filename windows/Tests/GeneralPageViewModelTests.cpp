// GeneralPageViewModelTests.cpp — 通用页签 VM（保存位置/格式质量路由/
// 界面语言/显式 Save 合并 engines.json + appLanguage 顶层键）。
#include <catch2/catch_amalgamated.hpp>

#include "GeneralPageViewModel.hpp"

#include "tf/EngineConfigStore.hpp"

#include <nlohmann/json.hpp>

#include <string>

using Catch::Approx;

using tf::ui::GeneralPageViewModel;

TEST_CASE("general vm defaults and format validation", "[general][settings]") {
    GeneralPageViewModel vm;
    CHECK(vm.DefaultFormat() == "png");
    CHECK(vm.QualityVisible() == false);          // png 无质量参数
    CHECK(vm.ActiveQuality() == 0.0);
    CHECK(vm.AppLanguage() == "system");
    CHECK(vm.HasCustomSaveLocation() == false);
    CHECK(vm.CheckForUpdates() == true);          // PRD 默认开
    CHECK(vm.LaunchAtLogin() == false);

    CHECK(vm.IsValidFormat("png"));
    CHECK(vm.IsValidFormat("jpeg"));
    CHECK(vm.IsValidFormat("heic"));
    CHECK_FALSE(vm.IsValidFormat("jpg"));         // 非法格式拒绝
    vm.SetDefaultFormat("jpg");
    CHECK(vm.DefaultFormat() == "png");           // 拒绝后保持原值
}

TEST_CASE("general vm quality routes by format with clamp", "[general][settings]") {
    GeneralPageViewModel vm;
    vm.SetDefaultFormat("jpeg");
    vm.SetActiveQuality(0.5);
    CHECK(vm.ActiveQuality() == Approx(0.5).epsilon(0.001));
    CHECK(vm.EffectiveHeicQuality() == Approx(0.9).epsilon(0.001));  // heic 不受影响

    vm.SetActiveQuality(0.0);                     // clamp 到下限 0.1
    CHECK(vm.ActiveQuality() == Approx(0.1).epsilon(0.001));
    vm.SetActiveQuality(5.0);                     // clamp 到上限 1.0
    CHECK(vm.ActiveQuality() == Approx(1.0).epsilon(0.001));

    vm.SetDefaultFormat("heic");
    vm.SetActiveQuality(0.7);
    CHECK(vm.ActiveQuality() == Approx(0.7).epsilon(0.001));
    CHECK(vm.EffectiveJpegQuality() == Approx(1.0).epsilon(0.001));  // jpeg 保持 clamp 后值

    vm.SetDefaultFormat("png");
    CHECK(vm.QualityVisible() == false);
    vm.SetActiveQuality(0.8);                     // png 下路由无落点
    CHECK(vm.ActiveQuality() == 0.0);
}

TEST_CASE("general vm appLanguage options reject unknown raw", "[general][settings]") {
    GeneralPageViewModel vm;
    const auto& options = GeneralPageViewModel::AppLanguageOptions();
    REQUIRE(options.size() == 11);                // system + 10 语言（Core AppLanguage）
    CHECK(options.front().raw == "system");
    CHECK(options.front().displayName == L"跟随系统");

    vm.SetAppLanguage("klingon");                 // 未收录 raw 拒绝
    CHECK(vm.AppLanguage() == "system");
    vm.SetAppLanguage("zh-Hans");
    CHECK(vm.AppLanguage() == "zh-Hans");
}

TEST_CASE("general vm load reads store fields and appLanguage key", "[general][settings]") {
    const std::string enginesJson = R"({
  "saveLocation": "D:\\tmp",
  "defaultFormat": "jpeg",
  "jpegQuality": 0.6,
  "heicQuality": 0.7,
  "appLanguage": "ja",
  "unknownTopLevelKey": {"a": 1}
})";
    GeneralPageViewModel vm({[&] { return enginesJson; }, {}});
    CHECK(vm.SaveLocation() == "D:\\tmp");
    CHECK(vm.DefaultFormat() == "jpeg");
    CHECK(vm.ActiveQuality() == Approx(0.6).epsilon(0.001));
    CHECK(vm.AppLanguage() == "ja");
}

TEST_CASE("general vm save merges patch and preserves unknown keys", "[general][settings]") {
    const std::string existing = R"({
  "saveLocation": "",
  "defaultFormat": "png",
  "engineChainV2": [{"engine": "apple"}],
  "someFutureKey": 42
})";
    std::string saved;
    GeneralPageViewModel vm({[&] { return existing; },
                             [&](const std::string& text) { saved = text; }});
    vm.SetDefaultFormat("heic");
    vm.SetActiveQuality(0.8);
    vm.SetSaveLocation("  C:\\shots  ");          // 两端空白裁剪
    vm.SetAppLanguage("en");
    REQUIRE(vm.Save());

    nlohmann::json root = nlohmann::json::parse(saved, nullptr, false);
    REQUIRE_FALSE(root.is_discarded());
    CHECK(root["defaultFormat"] == "heic");
    CHECK(root["heicQuality"] == Approx(0.8).epsilon(0.001));
    CHECK(root["saveLocation"] == "C:\\shots");
    CHECK(root["appLanguage"] == "en");           // schema §2.1 键由本页写入
    CHECK(root["someFutureKey"] == 42);           // 未知键保留
    CHECK(root["engineChainV2"].is_array());      // 其它分支 section 不动
}

TEST_CASE("general vm save without closure returns false", "[general][settings]") {
    GeneralPageViewModel vm;                      // 纯内存模式
    CHECK_FALSE(vm.Save());
}

TEST_CASE("general vm patch only carries this page's fields", "[general][settings]") {
    GeneralPageViewModel vm;
    vm.SetSaveLocation("X");
    tf::core::EngineConfig patch = vm.Patch();
    CHECK(patch.saveLocation == "X");
    CHECK(patch.defaultFormat == "png");
    CHECK(patch.jpegQuality == Approx(0.9).epsilon(0.001));
    CHECK(patch.heicQuality == Approx(0.9).epsilon(0.001));
}
