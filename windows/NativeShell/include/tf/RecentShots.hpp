// tf/RecentShots.hpp — bridge between the capture pipeline and the history
// store for the FR-34 tray "最近 5 张截图" strip. Win32 (WIC save + files).
//
// recordScreenshot  worker-thread call after a successful capture: full PNG
//                   under <localDir>\recent\<uuid>.png (pruned to
//                   kKeepPngFiles newest) + in-row JPEG thumbnail; inserts
//                   the screenshot-category entry the store queries back.
// trayRecentShots   main-thread read: newest 5 as tray menu RecentShot rows
//                   ("截图 HH:MM" labels).
// loadRecentShotPixels  full-resolution pixels for an entry (disk PNG first,
//                   row thumbnail as the fallback when the file was pruned).
#ifndef TF_RECENT_SHOTS_HPP
#define TF_RECENT_SHOTS_HPP

#include "tf/CoreTypes.hpp"
#include "tf/TrayIcon.hpp"
#include "tf/HistoryStore.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tf::shots {

/// PNG files kept under the recent-shots dir (store rows live longer; older
/// entries degrade to their thumbnail on reopen).
constexpr int kKeepPngFiles = 12;

/// Best-effort; returns false without touching the store on any failure.
bool recordScreenshot(core::HistoryStore& store, const PixelBuffer& pixels,
                      const std::wstring& recentDir);

/// Newest-5 screenshot entries as tray rows (label "截图 HH:MM").
std::vector<RecentShot> trayRecentShots(const core::HistoryStore& store);

/// Pixels for a screenshot entry (disk PNG via translatedText path; falls
/// back to the in-row thumbnail). Invalid buffer when both fail.
PixelBuffer loadRecentShotPixels(const core::TranslationHistory& entry);

}  // namespace tf::shots

#endif  // TF_RECENT_SHOTS_HPP
