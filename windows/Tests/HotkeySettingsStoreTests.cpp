// HotkeySettingsStoreTests.cpp — settings.json 快捷键段 round-trip。
#include <catch2/catch_amalgamated.hpp>

#include "tf/HotkeySettingsStore.hpp"

#include <nlohmann/json.hpp>

using tf::core::HotkeyBinding;
using tf::core::kHotkeyActionNames;
using tf::core::hotkeyActionNameFromSettingsKey;
using tf::core::hotkeySettingsKeyFor;
using tf::core::parseHotkeyBindings;
using tf::core::serializeHotkeyBindings;

TEST_CASE("hotkey settings key mapping is stable", "[hotkeys][settings]") {
    CHECK(hotkeySettingsKeyFor("fullScreen") == "TransFrame.fullScreenShortcut");
    CHECK(hotkeySettingsKeyFor("region") == "TransFrame.selectionShortcut");
    CHECK(hotkeySettingsKeyFor("translate") == "TransFrame.translationModeShortcut");
    CHECK(hotkeySettingsKeyFor("textSelection") ==
          "TransFrame.textSelectionTranslationShortcut");
    CHECK(hotkeySettingsKeyFor("translateInsert") ==
          "TransFrame.translateAndInsertShortcut");
    CHECK_FALSE(hotkeySettingsKeyFor("bogus").empty() == false);  // unknown -> empty string
    CHECK_FALSE(hotkeyActionNameFromSettingsKey("TransFrame.unknownShortcut").has_value());
}

TEST_CASE("hotkey bindings round-trip through settings.json text", "[hotkeys][settings]") {
    std::vector<HotkeyBinding> bindings;
    for (int i = 0; i < 5; ++i) {
        HotkeyBinding b;
        b.actionName = kHotkeyActionNames[i];
        b.vk = 0x41 + static_cast<std::uint32_t>(i);
        b.modifiers = 3;  // MOD_ALT | MOD_CONTROL
        b.enabled = true;
        bindings.push_back(b);
    }
    const std::string merged = serializeHotkeyBindings("", bindings);
    const auto back = parseHotkeyBindings(merged);
    REQUIRE(back.size() == bindings.size());
    for (std::size_t i = 0; i < back.size(); ++i) {
        CHECK(back[i].actionName == bindings[i].actionName);
        CHECK(back[i].vk == bindings[i].vk);
        CHECK(back[i].modifiers == bindings[i].modifiers);
        CHECK(back[i].enabled);
    }
}

TEST_CASE("hotkey serialize preserves unknown settings keys", "[hotkeys][settings]") {
    const std::string existing =
        R"({"TransFrame.jpegQuality":85,"TransFrame.strokeColor":"#FF0000"})";
    HotkeyBinding b;
    b.actionName = "region";
    b.vk = 65;
    b.modifiers = 3;
    const std::string merged = serializeHotkeyBindings(existing, {b});

    const auto root = nlohmann::json::parse(merged);
    CHECK(root.at("TransFrame.jpegQuality") == 85);
    CHECK(root.at("TransFrame.strokeColor") == "#FF0000");
    CHECK(root.at("TransFrame.selectionShortcut").at("vk") == 65);
}

TEST_CASE("hotkey parse skips invalid entries but keeps valid ones", "[hotkeys][settings]") {
    const std::string text =
        R"({"TransFrame.selectionShortcut":{"vk":65,"modifiers":3},)"
        R"( "TransFrame.fullScreenShortcut":{"vk":-1,"modifiers":3},)"
        R"( "TransFrame.translationModeShortcut":"junk"})";
    const auto back = parseHotkeyBindings(text);
    REQUIRE(back.size() == 1);
    CHECK(back[0].actionName == "region");
}

TEST_CASE("hotkey parse of junk text yields empty", "[hotkeys][settings]") {
    CHECK(parseHotkeyBindings("not json").empty());
    CHECK(parseHotkeyBindings("").empty());
}

TEST_CASE("hotkey enabled=false round-trips", "[hotkeys][settings]") {
    HotkeyBinding b;
    b.actionName = "translate";
    b.vk = 84;
    b.modifiers = 3;
    b.enabled = false;
    const auto back = parseHotkeyBindings(serializeHotkeyBindings("", {b}));
    REQUIRE(back.size() == 1);
    CHECK_FALSE(back[0].enabled);
}
