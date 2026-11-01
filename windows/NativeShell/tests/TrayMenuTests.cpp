// tests/TrayMenuTests.cpp — tray menu model (FR-34).
#include "catch_amalgamated.hpp"

#include "tf/TrayIcon.hpp"

#include <algorithm>

using namespace tf;

TEST_CASE("menu contains all ten commands plus recent shots", "[tray][menu]") {
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
    CHECK(has(TrayCommand::PinnedManager));
    CHECK(has(TrayCommand::History));
    CHECK(has(TrayCommand::Settings));
    CHECK(has(TrayCommand::CheckUpdates));
    CHECK(has(TrayCommand::Exit));
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
    CHECK(items.size() == 10);
}

TEST_CASE("capture commands are enabled; future slots are disabled placeholders",
          "[tray][menu]") {
    const auto items = buildTrayMenu({});
    for (const auto& i : items) {
        switch (i.command) {
            case TrayCommand::PinnedManager:
            case TrayCommand::History:
            case TrayCommand::Settings:
            case TrayCommand::CheckUpdates:
                CHECK_FALSE(i.enabled);  // placeholders until tasks C/D land
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
