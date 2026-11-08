// TrayMenu.cpp — portable tray menu model (buildTrayMenuModel).
// Windows Shell_NotifyIcon plumbing stays in src/TrayIcon.cpp; this TU keeps
// the menu model linkable on every platform so TrayMenuTests run everywhere.
//
// FR-37: labels resolve through the Localization tables (selected -> en ->
// hardcoded zh fallback), so the menu follows the UI language without a
// restart; tests that never load tables keep seeing the zh fallbacks.
#include "tf/TrayIcon.hpp"
#include "tf/Localization.hpp"

#include <algorithm>

namespace tf {

namespace {

using core::loc::tw;

void pushAction(std::vector<TrayMenuItem>& items, std::wstring label,
                TrayCommand command, bool separatorAfter = false) {
    TrayMenuItem item;
    item.label = std::move(label);
    item.command = command;
    item.enabled = true;
    item.separatorAfter = separatorAfter;
    items.push_back(std::move(item));
}

std::vector<TrayMenuItem> baseItems() {
    // FR-34: full-screen / region / translate / text-selection / insert.
    std::vector<TrayMenuItem> items;
    pushAction(items, tw("menu.capture.full.screen", L"全屏截图"), TrayCommand::FullScreen);
    pushAction(items, tw("menu.capture.selection", L"区域截图"), TrayCommand::Region);
    pushAction(items, tw("win.tray.translateMode", L"翻译模式"), TrayCommand::Translate);
    pushAction(items, tw("win.tray.textSelection", L"划词翻译"), TrayCommand::TextSelection);
    pushAction(items, tw("win.tray.translateInsert", L"翻译并插入"),
               TrayCommand::TranslateInsert, /*separatorAfter=*/true);
    return items;
}

}  // namespace

std::vector<TrayMenuItem> buildTrayMenu(const std::vector<RecentShot>& recentShots) {
    return buildTrayMenuModel(recentShots, {}).items;
}

TrayMenuModel buildTrayMenuModel(const std::vector<RecentShot>& recentShots,
                                 const std::vector<PinnedSnapshot>& pinned) {
    TrayMenuModel model;
    model.items = baseItems();

    // Recent shots: up to 5 thumbnails (PRD FR-34), newest first.
    const std::size_t recentCount = std::min<std::size_t>(recentShots.size(), 5);
    for (std::size_t i = 0; i < recentCount; ++i) {
        TrayMenuItem item;
        item.label = recentShots[i].label.empty()
                         ? core::loc::formatArgsWide(tw("win.tray.recentShot", L"截图 %@"),
                                               {std::to_wstring(i + 1)})
                         : recentShots[i].label;
        item.command = TrayCommand::RecentShot;
        item.enabled = true;
        item.separatorAfter = (i + 1 == recentCount);
        item.recentIndex = static_cast<int>(i);
        item.thumbnailHint = recentShots[i].thumbnailPath;
        model.items.push_back(std::move(item));
    }

    // FR-14 置顶管理: submenu parent — enabled (and opening a submenu) only
    // while a pin exists; the disabled placeholder keeps the layout stable.
    {
        TrayMenuItem item;
        item.label = tw("win.tray.pinnedManager", L"置顶管理");
        item.command = TrayCommand::PinnedManager;
        item.enabled = !pinned.empty();
        model.items.push_back(std::move(item));
    }
    model.pinnedItems = buildPinnedSubmenu(pinned);

    pushAction(model.items, tw("win.tray.history", L"历史记录"), TrayCommand::History);
    pushAction(model.items, tw("win.tray.settings", L"设置"), TrayCommand::Settings);
    pushAction(model.items, tw("win.tray.checkUpdates", L"检查更新"),
               TrayCommand::CheckUpdates);
    pushAction(model.items, tw("win.tray.replayOnboarding", L"重新查看引导"),
               TrayCommand::ReplayOnboarding);
    pushAction(model.items, tw("win.tray.exit", L"退出"), TrayCommand::Exit);
    return model;
}

std::vector<TrayMenuItem> buildPinnedSubmenu(const std::vector<PinnedSnapshot>& pinned) {
    std::vector<TrayMenuItem> items;
    // No pins -> no submenu at all (the parent stays the disabled
    // placeholder; the fleet actions are meaningless without a fleet).
    if (pinned.empty()) return items;
    for (std::size_t i = 0; i < pinned.size(); ++i) {
        std::wstring label = core::loc::formatArgsWide(tw("win.tray.pinnedItem", L"贴图 %@"),
                                                 {std::to_wstring(i + 1)});
        label += L" · " + std::to_wstring(pinned[i].widthPx) + L"×" +
                 std::to_wstring(pinned[i].heightPx);
        if (!pinned[i].clickThrough) {
            label += L" · " + std::wstring(tw("win.tray.pinnedInteractive", L"可交互"));
        }
        TrayMenuItem item;
        item.label = std::move(label);
        item.command = TrayCommand::PinnedSelect;
        item.enabled = true;
        item.separatorAfter = (i + 1 == pinned.size());  // fleet actions below
        item.pinnedIndex = static_cast<int>(i);
        items.push_back(std::move(item));
    }
    if (!items.empty()) items.back().separatorAfter = true;
    pushAction(items, tw("win.tray.toggleThrough", L"切换穿透"),
               TrayCommand::PinnedToggleThrough);
    pushAction(items, tw("win.tray.gather", L"聚拢到前台"), TrayCommand::PinnedGather);
    pushAction(items, tw("win.tray.closeAll", L"全部关闭"), TrayCommand::PinnedCloseAll);
    return items;
}

const TrayMenuItem* trayMenuItemForId(const std::vector<TrayMenuItem>& items, unsigned id) {
    if (id == 0 || id > items.size() || !items[id - 1].enabled) return nullptr;
    return &items[id - 1];
}

const TrayMenuItem* trayMenuPinnedItemForId(const std::vector<TrayMenuItem>& pinnedItems,
                                            unsigned id) {
    if (id <= kPinnedMenuIdBase || id > kPinnedMenuIdBase + pinnedItems.size()) {
        return nullptr;
    }
    const TrayMenuItem& item = pinnedItems[id - kPinnedMenuIdBase - 1];
    return item.enabled ? &item : nullptr;
}

} // namespace tf
