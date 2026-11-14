// LanguagePageViewModelTests.cpp — 语言页签 VM（选项表构造、raw 有效性
// 防御、Attach 绑定 Core EngineConfig 的读写）。
#include <catch2/catch_amalgamated.hpp>

using Catch::Approx;

#include "LanguagePageViewModel.h"

#include "tf/EngineConfigStore.hpp"

using tf::ui::LanguagePageViewModel;
using tf::ui::LanguageOption;

TEST_CASE("language vm option tables match spec shape", "[language][settings]") {
    LanguagePageViewModel vm;
    auto const& sources = vm.SourceLanguageOptions();
    auto const& targets = vm.TargetLanguageOptions();
    REQUIRE(sources.size() == 30);                // auto + 29 语言
    REQUIRE(targets.size() == 30);                // "" + 29 语言（无 auto）
    CHECK(sources.front().raw == "auto");
    CHECK(sources.front().displayName == L"自动检测");
    CHECK(targets.front().raw == LanguagePageViewModel::kFollowSystemRawValue);
    CHECK(targets.front().displayName == L"跟随系统");
    for (auto const& t : targets) {
        CHECK(t.raw != "auto");                   // 目标语言不含 auto
    }
    // spec §4 表顺序抽查：en / zh-Hans / zh-Hant / ja / ko 居首
    CHECK(sources[1].raw == "en");
    CHECK(sources[2].raw == "zh-Hans");
    CHECK(sources[3].raw == "zh-Hant");
    CHECK(targets[1].raw == "en");
    CHECK(targets[2].raw == "zh-Hans");
}

TEST_CASE("language vm unattached reads schema defaults", "[language][settings]") {
    LanguagePageViewModel vm;
    CHECK_FALSE(vm.IsAttached());
    CHECK(vm.GlobalSourceLanguage() == "auto");
    CHECK(vm.GlobalTargetLanguage() == "");       // 空 = 跟随系统
    CHECK(vm.InsertSourceLanguage() == "auto");
    CHECK(vm.InsertTargetLanguage() == "");
}

TEST_CASE("language vm setters write attached config and reject unknown raw",
          "[language][settings]") {
    tf::core::EngineConfig config;
    LanguagePageViewModel vm;
    vm.Attach(config);
    REQUIRE(vm.IsAttached());

    vm.SetGlobalSourceLanguage("zh-Hans");
    vm.SetGlobalTargetLanguage("ja");
    vm.SetInsertSourceLanguage("en");
    vm.SetInsertTargetLanguage("");               // 跟随系统 = 空串
    CHECK(config.sourceLang == "zh-Hans");
    CHECK(config.targetLang == "ja");
    CHECK(config.translateAndInsert.sourceLang == "en");
    CHECK(config.translateAndInsert.targetLang == "");

    vm.SetGlobalSourceLanguage("klingon");        // 未收录值不落库
    CHECK(config.sourceLang == "zh-Hans");
    vm.SetGlobalTargetLanguage("auto");           // auto 对目标语言非法
    CHECK(config.targetLang == "ja");
    vm.SetInsertSourceLanguage("");               // 空串对源语言非法
    CHECK(config.translateAndInsert.sourceLang == "en");
}

TEST_CASE("language vm index lookup drives combo selection", "[language][settings]") {
    tf::core::EngineConfig config;
    config.sourceLang = "fr";
    config.targetLang = "";                       // 空串 = 跟随系统（下标 0）
    config.translateAndInsert.sourceLang = "xx";  // 未知值：index -1，原值保留
    LanguagePageViewModel vm;
    vm.Attach(config);

    CHECK(vm.IndexOfSourceOption("fr") == 6);     // auto,en,zh-Hans,zh-Hant,ja,ko,fr
    CHECK(vm.IndexOfTargetOption("") == 0);
    CHECK(vm.IndexOfSourceOption("xx") == -1);
    CHECK(config.translateAndInsert.sourceLang == "xx");  // 不擅自改写未知值
}

TEST_CASE("language vm display names cover specials", "[language][settings]") {
    CHECK(LanguagePageViewModel::DisplayNameForRaw("") == L"跟随系统");
    CHECK(LanguagePageViewModel::DisplayNameForRaw("auto") == L"自动检测");
    CHECK(LanguagePageViewModel::DisplayNameForRaw("zh-Hans") == L"简体中文");
    CHECK(LanguagePageViewModel::DisplayNameForRaw("qq") == L"qq");  // 未知 raw 保守显示
}
