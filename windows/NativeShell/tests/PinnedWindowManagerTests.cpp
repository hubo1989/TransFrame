// tests/PinnedWindowManagerTests.cpp — FR-14 pure semantics: the pinned
// fleet state machine (PinnedWindowState) and the 置顶管理 tray submenu
// model (buildPinnedSubmenu / buildTrayMenuModel). The Win32 HWND fleet is
// covered by Tests/manual/smoke_pinned_history.cpp.
#include "catch_amalgamated.hpp"

#include "tf/PinnedWindowManager.hpp"
#include "tf/TrayIcon.hpp"

#include <algorithm>

using namespace tf;

namespace {

PinnedSnapshot snap(std::uint64_t id, int w, int h, bool through) {
    return PinnedSnapshot{id, w, h, through};
}

bool hasCommand(const std::vector<TrayMenuItem>& items, TrayCommand c) {
    return std::any_of(items.begin(), items.end(),
                       [c](const TrayMenuItem& i) { return i.command == c; });
}

}  // namespace

// ---------------------------------------------------------------------------
// PinnedWindowState — macOS PinnedWindowsManager semantics
// ---------------------------------------------------------------------------
TEST_CASE("pins start click-through and visible", "[pinned][state]") {
    PinnedWindowState state;
    const auto out = state.pin(0, 800, 600);
    REQUIRE(out.ok);
    CHECK(out.createdNew);
    CHECK(out.id != 0);
    const auto pins = state.snapshot();
    REQUIRE(pins.size() == 1);
    CHECK(pins[0].clickThrough);  // 默认点击穿透
    CHECK(pins[0].visible);
    CHECK(pins[0].widthPx == 800);
    CHECK(pins[0].heightPx == 600);
}

TEST_CASE("re-pin raises the existing window instead of duplicating", "[pinned][state]") {
    PinnedWindowState state;
    const auto first = state.pin(0, 100, 50);
    REQUIRE(first.ok);
    state.setClickThrough(first.id, false);
    state.setAllClickThrough(true);

    const auto second = state.pin(first.id, 100, 50);
    REQUIRE(second.ok);
    CHECK_FALSE(second.createdNew);  // raised, not duplicated
    CHECK(second.id == first.id);
    CHECK(state.count() == 1);
    // The raised window is topmost again (front of the z-order list).
    CHECK(state.snapshot().front().id == first.id);
}

TEST_CASE("fleet caps at 10 pins (macOS maxPinnedWindows)", "[pinned][state]") {
    PinnedWindowState state;
    for (int i = 0; i < 10; ++i) {
        REQUIRE(state.pin(0, 10, 10).ok);
    }
    const auto rejected = state.pin(0, 10, 10);
    CHECK_FALSE(rejected.ok);
    CHECK(state.count() == 10);
    // But an existing id still raises fine at the limit.
    const auto existing = state.pin(3, 10, 10);
    CHECK(existing.ok);
    CHECK_FALSE(existing.createdNew);
}

TEST_CASE("click-through toggles per window and fleet-wide", "[pinned][state]") {
    PinnedWindowState state;
    const auto a = state.pin(0, 10, 10);
    const auto b = state.pin(0, 10, 10);
    REQUIRE(a.ok);
    REQUIRE(b.ok);

    CHECK(state.toggleClickThrough(a.id));  // -> interactive
    // (a sits somewhere in the z list; check it by id.)
    bool aInteractive = false;
    for (const auto& p : state.snapshot()) {
        if (p.id == a.id) aInteractive = !p.clickThrough;
    }
    CHECK(aInteractive);

    // Fleet toggle: any click-through window -> all become interactive
    // (macOS resetAllInteractions), then all flip back together.
    CHECK(state.setAllClickThrough(false) == 1);  // only b changed
    for (const auto& p : state.snapshot()) CHECK_FALSE(p.clickThrough);
    CHECK(state.setAllClickThrough(true) == 2);
    for (const auto& p : state.snapshot()) CHECK(p.clickThrough);
}

TEST_CASE("unpin removes one; unpinAll clears the fleet", "[pinned][state]") {
    PinnedWindowState state;
    const auto a = state.pin(7, 10, 10);
    const auto b = state.pin(0, 10, 10);
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    CHECK(state.unpin(7));
    CHECK_FALSE(state.isPinned(7));
    CHECK(state.isPinned(b.id));
    CHECK_FALSE(state.unpin(999));  // unknown id is a no-op
    state.unpinAll();
    CHECK(state.count() == 0);
    CHECK(state.snapshot().empty());
}

TEST_CASE("gatherAll marks every window visible and keeps order", "[pinned][state]") {
    PinnedWindowState state;
    const auto a = state.pin(0, 10, 10);
    const auto b = state.pin(0, 10, 10);
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    // Newest pin sits at the front (top of z order).
    CHECK(state.snapshot().front().id == b.id);
    state.gatherAll();
    for (const auto& p : state.snapshot()) CHECK(p.visible);
    CHECK(state.snapshot().front().id == b.id);  // order unchanged
}

TEST_CASE("auto ids never collide with explicit ids", "[pinned][state]") {
    PinnedWindowState state;
    REQUIRE(state.pin(5, 10, 10).ok);
    for (int i = 0; i < 4; ++i) REQUIRE(state.pin(0, 10, 10).ok);
    // 5 explicit + 4 auto = 5 windows; the 5th auto pin must be id 6+, not 5.
    CHECK(state.count() == 5);
    const auto next = state.pin(0, 10, 10);
    REQUIRE(next.ok);
    CHECK(next.id >= 6);
}

// ---------------------------------------------------------------------------
// Tray 置顶管理 submenu (buildPinnedSubmenu / buildTrayMenuModel)
// ---------------------------------------------------------------------------
TEST_CASE("pinned submenu lists pins with 序号+尺寸 then fleet actions",
          "[tray][pinned]") {
    const std::vector<PinnedSnapshot> pins{snap(1, 800, 600, true),
                                           snap(2, 1920, 1080, false)};
    const auto sub = buildPinnedSubmenu(pins);
    REQUIRE(sub.size() == 5);
    CHECK(sub[0].command == TrayCommand::PinnedSelect);
    CHECK(sub[0].pinnedIndex == 0);
    CHECK(sub[0].label == L"贴图 1 · 800×600");
    CHECK(sub[1].label.find(L"可交互") != std::wstring::npos);
    CHECK(sub[1].pinnedIndex == 1);
    CHECK(sub[1].separatorAfter);  // fleet actions behind a separator
    CHECK(sub[2].command == TrayCommand::PinnedToggleThrough);
    CHECK(sub[3].command == TrayCommand::PinnedGather);
    CHECK(sub[4].command == TrayCommand::PinnedCloseAll);
}

TEST_CASE("empty fleet -> empty submenu and disabled placeholder parent",
          "[tray][pinned]") {
    const auto model = buildTrayMenuModel({}, {});
    CHECK(model.pinnedItems.empty());
    const auto it = std::find_if(model.items.begin(), model.items.end(),
                                 [](const TrayMenuItem& i) {
                                     return i.command == TrayCommand::PinnedManager;
                                 });
    REQUIRE(it != model.items.end());
    CHECK_FALSE(it->enabled);  // placeholder stays disabled
}

TEST_CASE("live fleet -> enabled 置顶管理 with submenu attached", "[tray][pinned]") {
    const auto model = buildTrayMenuModel({}, {snap(9, 64, 48, true)});
    REQUIRE(model.pinnedItems.size() == 4);  // 1 pin + 3 fleet actions
    const auto it = std::find_if(model.items.begin(), model.items.end(),
                                 [](const TrayMenuItem& i) {
                                     return i.command == TrayCommand::PinnedManager;
                                 });
    REQUIRE(it != model.items.end());
    CHECK(it->enabled);
}

TEST_CASE("submenu id lookup maps kPinnedMenuIdBase+n to the right action",
          "[tray][pinned]") {
    const std::vector<PinnedSnapshot> pins{snap(1, 10, 10, true), snap(2, 20, 20, true)};
    const auto sub = buildPinnedSubmenu(pins);
    REQUIRE(trayMenuPinnedItemForId(sub, kPinnedMenuIdBase) == nullptr);
    REQUIRE(trayMenuPinnedItemForId(sub, kPinnedMenuIdBase + 99) == nullptr);
    REQUIRE(trayMenuPinnedItemForId(sub, 1) == nullptr);  // flat ids rejected
    REQUIRE(trayMenuPinnedItemForId(sub, kPinnedMenuIdBase + 1));
    CHECK(trayMenuPinnedItemForId(sub, kPinnedMenuIdBase + 1)->pinnedIndex == 0);
    REQUIRE(trayMenuPinnedItemForId(sub, kPinnedMenuIdBase + 2));
    CHECK(trayMenuPinnedItemForId(sub, kPinnedMenuIdBase + 2)->pinnedIndex == 1);
    CHECK(trayMenuPinnedItemForId(sub, kPinnedMenuIdBase + 3)->command ==
          TrayCommand::PinnedToggleThrough);
    CHECK(trayMenuPinnedItemForId(sub, kPinnedMenuIdBase + 5)->command ==
          TrayCommand::PinnedCloseAll);
}

TEST_CASE("flat menu keeps its shape with and without pins", "[tray][pinned]") {
    const auto bare = buildTrayMenu({});
    const auto withPins = buildTrayMenuModel({}, {snap(1, 10, 10, true)}).items;
    REQUIRE(bare.size() == withPins.size());
    CHECK(hasCommand(withPins, TrayCommand::FullScreen));
    CHECK(hasCommand(withPins, TrayCommand::Exit));
    // Flat id alignment is unchanged (History still resolves to the same id).
    std::size_t historyAt = 0;
    for (std::size_t i = 0; i < bare.size(); ++i) {
        if (bare[i].command == TrayCommand::History) historyAt = i;
    }
    REQUIRE(historyAt > 0);
    REQUIRE(trayMenuItemForId(withPins, static_cast<unsigned>(historyAt) + 1));
    CHECK(trayMenuItemForId(withPins, static_cast<unsigned>(historyAt) + 1)->command ==
          TrayCommand::History);
}
