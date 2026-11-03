// TrayMenu.cpp — portable tray menu model (buildTrayMenu).
// Windows Shell_NotifyIcon plumbing stays in src/TrayIcon.cpp; this TU keeps
// the menu model linkable on every platform so TrayMenuTests run everywhere.
#include "tf/TrayIcon.hpp"

#include <algorithm>

namespace tf {

std::vector<TrayMenuItem> buildTrayMenu(const std::vector<RecentShot>& recentShots) {
    // FR-34: full-screen / region / translate / text-selection / insert /
    // pinned-manager / history / settings / check-updates / exit + recent 5.
    std::vector<TrayMenuItem> items;
    items.push_back({L"全屏截图", TrayCommand::FullScreen, true, false, -1, {}});
    items.push_back({L"区域截图", TrayCommand::Region, true, false, -1, {}});
    items.push_back({L"翻译模式", TrayCommand::Translate, true, false, -1, {}});
    items.push_back({L"划词翻译", TrayCommand::TextSelection, true, false, -1, {}});
    items.push_back({L"翻译并插入", TrayCommand::TranslateInsert, true, true, -1, {}});

    // Recent shots: up to 5 thumbnails (PRD FR-34), newest first.
    const std::size_t recentCount = std::min<std::size_t>(recentShots.size(), 5);
    for (std::size_t i = 0; i < recentCount; ++i) {
        TrayMenuItem item;
        item.label = recentShots[i].label.empty()
                         ? (L"截图 " + std::to_wstring(i + 1))
                         : recentShots[i].label;
        item.command = TrayCommand::RecentShot;
        item.enabled = true;
        item.separatorAfter = (i + 1 == recentCount);
        item.recentIndex = static_cast<int>(i);
        item.thumbnailHint = recentShots[i].thumbnailPath;
        items.push_back(std::move(item));
    }

    items.push_back({L"置顶管理", TrayCommand::PinnedManager, false, false, -1, {}});
    items.push_back({L"历史记录", TrayCommand::History, false, false, -1, {}});
    items.push_back({L"设置", TrayCommand::Settings, false, false, -1, {}});
    items.push_back({L"检查更新", TrayCommand::CheckUpdates, true, false, -1, {}});
    items.push_back({L"退出", TrayCommand::Exit, true, false, -1, {}});
    return items;
}

const TrayMenuItem* trayMenuItemForId(const std::vector<TrayMenuItem>& items, unsigned id) {
    if (id == 0 || id > items.size() || !items[id - 1].enabled) return nullptr;
    return &items[id - 1];
}

} // namespace tf
