// AdvancedPageViewModelTests.cpp — 高级页签 VM（预设色表、写值 clamp、
// 全部重置、persistencePatch 经 Core patch 合并保留未知键）。
#include <catch2/catch_amalgamated.hpp>

using Catch::Approx;

#include "AdvancedPageViewModel.h"

#include "tf/EngineConfigStore.hpp"

#include <nlohmann/json.hpp>

using winrt::TransFrame::UI::AdvancedPageViewModel;
using tf::core::ColorConfig;
using tf::core::EngineConfig;

TEST_CASE("advanced vm preset catalog has nine spec colors", "[advanced][settings]") {
    const auto& presets = AdvancedPageViewModel::presetColors();
    REQUIRE(presets.size() == 9);
    CHECK(std::string(presets[0].name) == "red");
    CHECK(std::string(presets[8].name) == "black");
    CHECK(presets[0].color.red == Approx(1.0));   // mac .red 的 sRGB 近似（非纯 (1,0,0)）
    CHECK(presets[0].color.green == Approx(0.23).epsilon(0.01));
}

TEST_CASE("advanced vm carries config and clamps edits", "[advanced][settings]") {
    EngineConfig config;
    config.strokeColor = ColorConfig::makeRed();
    config.strokeWidth = 3.0;
    config.textSize = 18.0;
    AdvancedPageViewModel vm(config);
    CHECK(vm.strokeWidth() == Approx(3.0));
    CHECK(vm.textSize() == Approx(18.0));
    CHECK(vm.isColorSelected(ColorConfig::makeRed()));

    CHECK(vm.setStrokeWidth(99.0));               // clamp 到 20
    CHECK(vm.strokeWidth() == Approx(20.0));
    CHECK(vm.setStrokeWidth(0.0));                // clamp 到 1
    CHECK(vm.strokeWidth() == Approx(1.0));
    CHECK(vm.setStrokeWidth(7.5));
    CHECK(vm.strokeWidth() == Approx(7.5));

    CHECK(vm.setTextSize(500.0));                 // clamp 到 72
    CHECK(vm.textSize() == Approx(72.0));
    CHECK(vm.setTextSize(1.0));                   // clamp 到 8
    CHECK(vm.textSize() == Approx(8.0));

    CHECK_FALSE(vm.setStrokeWidth(7.5));          // 同值返回 false（不落盘信号）
    const auto& blue = AdvancedPageViewModel::presetColors()[4].color;
    CHECK(vm.setStrokeColor(blue));
    CHECK(vm.isColorSelected(blue));
    CHECK_FALSE(vm.isColorSelected(ColorConfig::makeRed()));
}

TEST_CASE("advanced vm reset restores the three page fields only",
          "[advanced][settings]") {
    EngineConfig config;
    config.strokeColor = ColorConfig{0.0, 0.0, 1.0, 1.0};  // blue
    config.strokeWidth = 12.0;
    config.textSize = 30.0;
    config.targetLang = "ja";                     // 非本页字段
    AdvancedPageViewModel vm(config);
    vm.resetToDefaults();
    CHECK(vm.strokeColor() == AdvancedPageViewModel::defaultStrokeColor());
    CHECK(vm.strokeWidth() == Approx(AdvancedPageViewModel::kDefaultStrokeWidth));
    CHECK(vm.textSize() == Approx(AdvancedPageViewModel::kDefaultTextSize));
    CHECK(vm.config().targetLang == "ja");        // 其它字段不动
}

TEST_CASE("advanced vm persistence patch merges over existing engines.json",
          "[advanced][settings]") {
    const std::string existing = R"({
  "strokeWidth": 5.0,
  "textSize": 20.0,
  "strokeColor": {"red": 0.1, "green": 0.2, "blue": 0.3, "opacity": 1.0},
  "unknownBranch": {"x": 1}
})";
    AdvancedPageViewModel vm(EngineConfig{});
    vm.setStrokeColor(ColorConfig::makeRed());
    vm.setStrokeWidth(2.5);
    vm.setTextSize(16.0);

    const std::string merged = vm.serializePatchInto(existing);
    nlohmann::json root = nlohmann::json::parse(merged, nullptr, false);
    REQUIRE_FALSE(root.is_discarded());
    CHECK(root["strokeWidth"] == Approx(2.5));
    CHECK(root["textSize"] == Approx(16.0));
    CHECK(root["strokeColor"]["red"] == Approx(1.0).epsilon(0.001));
    CHECK(root["unknownBranch"]["x"] == 1);       // 未知键保留
}

TEST_CASE("advanced vm persistencePatch touches only annotation fields",
          "[advanced][settings]") {
    AdvancedPageViewModel vm(EngineConfig{});
    vm.setStrokeWidth(4.0);
    EngineConfig patch = vm.persistencePatch();
    CHECK(patch.strokeWidth == Approx(4.0));
    CHECK(patch.targetLang.empty());              // patch 其余字段为默认
}
