// tests/TrayMenuTests.cpp — tray menu model (FR-34; FR-37 labels).
#include "catch_amalgamated.hpp"

#include "tf/Localization.hpp"
#include "tf/TrayIcon.hpp"

#include <algorithm>

using namespace tf;

TEST_CASE("menu contains all eleven commands plus recent shots", "[tray][menu]") {
    const std::vector<RecentShot> recents{
        {1, L"a.png", L"14:32  1920x1080"},
        {2, L"b.png", L"14:30  800x600"},
    };
    const auto items = buildTrayMenu(recents);

    // Find helper.
    auto has = [&](TrayCommand c) {
        return std::any_of(items.begin(), items.end(),
                           [c](const TrayMenuItem& i) { return i.command == c; });
    };
    CHECK(has(TrayCommand::FullScreen));
    CHECK(has(TrayCommand::Region));
    CHECK(has(TrayCommand::Translate));
    CHECK(has(TrayCommand::TextSelection));
    CHECK(has(TrayCommand::TranslateInsert));
    CHECK(has(TrayCommand::PickColor));       // FR-32 eyedropper entry
    CHECK(has(TrayCommand::ColorHistory));    // FR-41 after 历史记录
    CHECK(has(TrayCommand::PinnedManager));
    CHECK(has(TrayCommand::History));
    CHECK(has(TrayCommand::Settings));
    CHECK(has(TrayCommand::CheckUpdates));
    CHECK(has(TrayCommand::ReplayOnboarding));
    CHECK(has(TrayCommand::Exit));

    // 取色历史 sits right after 历史记录 (spec §4.1 tray placement).
    auto itHistory = std::find_if(items.begin(), items.end(), [](const TrayMenuItem& i) {
        return i.command == TrayCommand::History;
    });
    REQUIRE(itHistory != items.end());
    REQUIRE(itHistory + 1 != items.end());
    CHECK((itHistory + 1)->command == TrayCommand::ColorHistory);
}

TEST_CASE("recent shots appear in order with correct indices", "[tray][recent]") {
    const std::vector<RecentShot> recents{
        {10, L"r0.png", L"latest"},
        {9,  L"r1.png", L"prev"},
        {8,  L"r2.png", L"older"},
    };
    const auto items = buildTrayMenu(recents);

    std::vector<int> indices;
    for (const auto& i : items) {
        if (i.command == TrayCommand::RecentShot) indices.push_back(i.recentIndex);
    }
    REQUIRE(indices.size() == 3);
    CHECK(indices[0] == 0);  // newest first
    CHECK(indices[1] == 1);
    CHECK(indices[2] == 2);
}

TEST_CASE("recent strip is capped at 5 entries", "[tray][recent]") {
    std::vector<RecentShot> recents(8);
    for (int i = 0; i < 8; ++i) {
        recents[i].screenshotId = 100 + i;
        recents[i].label = L"shot " + std::to_wstring(i);
    }
    const auto items = buildTrayMenu(recents);
    const auto count = std::count_if(items.begin(), items.end(), [](const TrayMenuItem& i) {
        return i.command == TrayCommand::RecentShot;
    });
    CHECK(count == 5);
}

TEST_CASE("no recent shots -> no RecentShot entries, menu still complete",
          "[tray][recent]") {
    const auto items = buildTrayMenu({});
    const auto count = std::count_if(items.begin(), items.end(), [](const TrayMenuItem& i) {
        return i.command == TrayCommand::RecentShot;
    });
    CHECK(count == 0);
    CHECK(items.size() == 13);  // 6 actions + placeholder + history/color-history + settings/updates/onboarding/exit
}

TEST_CASE("capture commands are enabled; PinnedManager stays a disabled placeholder",
          "[tray][menu]") {
    const auto items = buildTrayMenu({});
    for (const auto& i : items) {
        switch (i.command) {
            case TrayCommand::PinnedManager:
                CHECK_FALSE(i.enabled);  // last placeholder (task D)
                break;
            default:
                CHECK(i.enabled);
                break;
        }
    }
}

TEST_CASE("recent-shot labels default to a generated name when empty", "[tray][recent]") {
    const std::vector<RecentShot> recents{{42, L"", L""}};
    const auto items = buildTrayMenu(recents);
    const auto it = std::find_if(items.begin(), items.end(), [](const TrayMenuItem& i) {
        return i.command == TrayCommand::RecentShot;
    });
    REQUIRE(it != items.end());
    CHECK_FALSE(it->label.empty());
    CHECK(it->thumbnailHint.empty());  // no thumbnail path supplied
}

TEST_CASE("popup IDs dispatch first and last actions without offset", "[tray][menu]") {
    const auto items = buildTrayMenu({});
    CHECK(trayMenuItemForId(items, 0) == nullptr);
    CHECK(trayMenuItemForId(items, 999) == nullptr);
    REQUIRE(trayMenuItemForId(items, 1));
    CHECK(trayMenuItemForId(items, 1)->command == TrayCommand::FullScreen);
    REQUIRE(trayMenuItemForId(items, 13));
    CHECK(trayMenuItemForId(items, 13)->command == TrayCommand::Exit);
    CHECK(trayMenuItemForId(items, 7) == nullptr);  // disabled placeholder (PinnedManager)
    REQUIRE(trayMenuItemForId(items, 11));
    CHECK(trayMenuItemForId(items, 11)->command == TrayCommand::CheckUpdates);
    REQUIRE(trayMenuItemForId(items, 12));
    CHECK(trayMenuItemForId(items, 12)->command == TrayCommand::ReplayOnboarding);
}

TEST_CASE("labels resolve through the localization tables (FR-37)", "[tray][locale]") {
    auto& loc = core::loc::Localization::instance();
    // Table with one translated key; everything else keeps the zh fallback.
    core::loc::StringTable table =
        core::loc::StringTable::fromJson(R"({"win.tray.exit": "Quitten"})");
    loc.installTable("de", std::move(table));
    // Save/restore state so other tests keep the fallback behavior.
    const std::string previous = loc.currentLanguageRaw();
    loc.setLanguage("de");
    const auto items = buildTrayMenu({});
    const auto exit = std::find_if(items.begin(), items.end(), [](const TrayMenuItem& i) {
        return i.command == TrayCommand::Exit;
    });
    REQUIRE(exit != items.end());
    CHECK(exit->label == L"Quitten");
    const auto fullScreen = std::find_if(items.begin(), items.end(),
                                         [](const TrayMenuItem& i) {
                                             return i.command == TrayCommand::FullScreen;
                                         });
    REQUIRE(fullScreen != items.end());
    CHECK(fullScreen->label == L"\x5168\x5C4F\x622A\x56FE");  // zh fallback 全屏截图

    loc.setLanguage(previous);
    core::loc::StringTable empty;
    loc.installTable("de", std::move(empty));  // erase the test table
    const auto restored = buildTrayMenu({});
    const auto exitRestored = std::find_if(
        restored.begin(), restored.end(),
        [](const TrayMenuItem& i) { return i.command == TrayCommand::Exit; });
    REQUIRE(exitRestored != restored.end());
    CHECK(exitRestored->label == L"\x9000\x51FA");  // 退出 fallback restored
}
