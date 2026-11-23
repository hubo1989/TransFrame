// tf/TrayIcon.hpp
// Tray icon (FR-34): Shell_NotifyIcon, left-click menu with capture actions +
// placeholders + recent-5-screenshot thumbnail strip.
//
// Menu model is pure (testable); the Shell_NotifyIcon / Win32 menu plumbing
// lives in src/TrayIcon.cpp and is exercised on Windows only.
#ifndef TF_TRAY_ICON_HPP
#define TF_TRAY_ICON_HPP

#include "CoreTypes.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace tf {

enum class TrayCommand : std::uint8_t {
    None = 0,
    FullScreen,        // 全屏
    Region,            // 区域
    Translate,         // 翻译
    TextSelection,     // 划词
    TranslateInsert,   // 插入
    PickColor,        // 取色器 (FR-32)
    PinnedManager,     // 置顶管理 (submenu parent; disabled while no pins)
    History,           // 历史记录
    ColorHistory,      // 取色历史 (FR-41)
    Settings,          // 设置
    CheckUpdates,      // 检查更新
    ReplayOnboarding,  // 重新查看引导 (FR-39)
    Exit,              // 退出
    RecentShot,        // recent screenshot #n (index in payload)
    // FR-14 置顶管理 submenu actions (payload int = pinnedIndex where noted).
    PinnedSelect,        // bring pinned window #n to front (payload index)
    PinnedToggleThrough, // 切换穿透 (fleet-wide click-through <-> interactive)
    PinnedGather,        // 聚拢到前台 (tile + raise all pins)
    PinnedCloseAll,      // 全部关闭
};

struct TrayMenuItem {
    std::wstring label;
    TrayCommand command = TrayCommand::None;
    bool enabled = true;
    bool separatorAfter = false;
    /// For RecentShot entries: index into the recent-shots list (0-based).
    int recentIndex = -1;
    /// For PinnedSelect entries: index into the pinned-window list (0-based).
    int pinnedIndex = -1;
    /// Thumbnail bitmap path/HBITMAP placeholder (task C/history integration).
    std::wstring thumbnailHint;
};

struct RecentShot {
    std::uint64_t screenshotId = 0;
    std::wstring thumbnailPath;  // small cached JPEG/PNG (history store supplies)
    std::wstring label;          // e.g. "14:32  1920x1080"
};

/// Pinned-window fleet state as seen by the tray (PinnedWindowManager feeds
/// it; buildPinnedSubmenu renders it).
struct PinnedSnapshot {
    std::uint64_t id = 0;
    int widthPx = 0;
    int heightPx = 0;
    bool clickThrough = true;
};

/// Full tray menu: flat items + the 置顶管理 submenu (empty = placeholder).
struct TrayMenuModel {
    std::vector<TrayMenuItem> items;
    std::vector<TrayMenuItem> pinnedItems;
};

/// Right-aligned key equivalents for the capture rows (mac NSMenu parity:
/// the status menu shows each action's live shortcut). Empty string = no hint
/// (action disabled or hotkey not registered).
struct TrayShortcutHints {
    std::wstring fullScreen;
    std::wstring region;
    std::wstring translate;
    std::wstring textSelection;
    std::wstring translateInsert;
};

/// Win32 popup menu IDs for the pinned submenu live above this base so they
/// cannot collide with the flat item IDs (1..N).
constexpr unsigned kPinnedMenuIdBase = 1000;

/// Balloon severity for showBalloon (maps to NIIF_* flags).
enum class BalloonIcon : std::uint8_t { Info, Warning, Error };

using TrayCommandHandler = std::function<void(TrayCommand command, int payloadIndex)>;

/// Pure menu construction: actions + recent strip (<= 5), no pins (submenu
/// stays a disabled placeholder — kept for the original call sites/tests).
std::vector<TrayMenuItem> buildTrayMenu(const std::vector<RecentShot>& recentShots);

/// Pure menu construction with the pinned-fleet submenu (FR-14). `hints`
/// renders as the right-aligned key-equivalent column ("" = none).
TrayMenuModel buildTrayMenuModel(const std::vector<RecentShot>& recentShots,
                                 const std::vector<PinnedSnapshot>& pinned,
                                 const TrayShortcutHints& hints = {});

/// Pure 置顶管理 submenu: per-pin rows (序号+尺寸, interactive pins marked),
/// then 切换穿透 / 聚拢到前台 / 全部关闭.
std::vector<TrayMenuItem> buildPinnedSubmenu(const std::vector<PinnedSnapshot>& pinned);

/// Win32 popup IDs start at 1; 0 means dismissed. Disabled IDs cannot dispatch.
const TrayMenuItem* trayMenuItemForId(const std::vector<TrayMenuItem>& items, unsigned id);

/// Submenu lookup for ids of the form kPinnedMenuIdBase + n (n counts from 1).
const TrayMenuItem* trayMenuPinnedItemForId(const std::vector<TrayMenuItem>& pinnedItems,
                                            unsigned id);

class TrayIconService {
public:
    explicit TrayIconService(TrayCommandHandler handler);
    ~TrayIconService();

    /// Info balloon (toast stand-in until the notification channel lands):
    /// fires the callback message with NIN_BALLOONUSERCLICK when clicked.
    void showBalloon(const std::wstring& title, const std::wstring& text);

    TrayIconService(const TrayIconService&) = delete;
    TrayIconService& operator=(const TrayIconService&) = delete;

    /// Create the Shell_NotifyIcon entry (call once, after message window up).
    bool install();

    /// Route tray callbacks to the App's hidden message window (call before
    /// install; safe after — reinstalls the icon against the new parent).
    void setCallbackWindow(void* hwnd);  // HWND

    /// Remove the tray entry (app exit / crash-safe teardown).
    void remove();

    /// Swap recent-shot list; menu rebuilds on next open (FR-34: 5 thumbnails).
    void setRecentShots(std::vector<RecentShot> shots);

    /// Swap pinned-fleet snapshot; the 置顶管理 submenu rebuilds on next open
    /// (empty = the placeholder stays disabled).
    void setPinnedWindows(std::vector<PinnedSnapshot> pinned);

    /// Swap live hotkey hints; the capture rows show them right-aligned on
    /// the next open (call after applySpecs / settings saves).
    void setShortcutHints(TrayShortcutHints hints);

    /// Left-click and right-click both open the menu.
    void showMenuAtCursor();

    /// FR-50.7: show a tray balloon (Shell_NotifyIcon NIF_INFO). Returns
    /// false when the icon is not installed (caller falls back to a dialog).
    bool showBalloon(const std::wstring& title, const std::wstring& text,
                     BalloonIcon icon);

    bool isInstalled() const { return installed_; }

private:
    void dispatch(TrayCommand command, int recentIndex);

    TrayCommandHandler handler_;
    std::vector<RecentShot> recent_;
    std::vector<PinnedSnapshot> pinned_;
    TrayShortcutHints hints_;
    void* callbackWindow_ = nullptr;  // HWND (void* keeps the header Win32-free)
    bool installed_ = false;
};

} // namespace tf

#endif // TF_TRAY_ICON_HPP
