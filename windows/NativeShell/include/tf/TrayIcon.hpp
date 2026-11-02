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
    PinnedManager,     // 置顶管理 (placeholder, task D)
    History,           // 历史 (placeholder)
    Settings,          // 设置 (placeholder)
    CheckUpdates,      // 检查更新 (placeholder)
    Exit,              // 退出
    RecentShot,        // recent screenshot #n (index in payload)
};

struct TrayMenuItem {
    std::wstring label;
    TrayCommand command = TrayCommand::None;
    bool enabled = true;
    bool separatorAfter = false;
    /// For RecentShot entries: index into the recent-shots list (0-based).
    int recentIndex = -1;
    /// Thumbnail bitmap path/HBITMAP placeholder (task C/history integration).
    std::wstring thumbnailHint;
};

struct RecentShot {
    std::uint64_t screenshotId = 0;
    std::wstring thumbnailPath;  // small cached JPEG/PNG (history store supplies)
    std::wstring label;          // e.g. "14:32  1920x1080"
};

using TrayCommandHandler = std::function<void(TrayCommand command, int recentIndex)>;

/// Pure menu construction: actions + placeholders + recent strip (<= 5).
std::vector<TrayMenuItem> buildTrayMenu(const std::vector<RecentShot>& recentShots);

class TrayIconService {
public:
    explicit TrayIconService(TrayCommandHandler handler);
    ~TrayIconService();

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

    /// Left-click opens menu (Win11 style); right-click is a no-op here.
    void showMenuAtCursor();

    bool isInstalled() const { return installed_; }

private:
    void dispatch(TrayCommand command, int recentIndex);

    TrayCommandHandler handler_;
    std::vector<RecentShot> recent_;
    void* callbackWindow_ = nullptr;  // HWND (void* keeps the header Win32-free)
    bool installed_ = false;
};

} // namespace tf

#endif // TF_TRAY_ICON_HPP
