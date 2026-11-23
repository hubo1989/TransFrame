// windows/Tests/manual/smoke_pinned_history.cpp — pinned windows + history
// full smoke (PRD stage 4, FR-14/FR-30/FR-34). Real-run verification against
// a throwaway SQLite store and real topmost HWNDs:
//
//   H1  temp-dir HistoryStore opens (WAL schema v1)
//   H2  all four entry categories insert (translation/screenshot/color/upload)
//       with a real WIC JPEG thumbnail for the screenshot row
//   H3  LIKE search / favorites-only / category filter round-trip
//   H4  recentScreenshots caps at 5, newest first (7 recorded, tray strip read)
//   H5  entryById + favoriteCount
//   H6  recordScreenshot writes the PNG + row; trayRecentShots labels 截图 HH:MM;
//       loadRecentShotPixels restores full-size pixels from disk
//   H7  export JSON/CSV/TXT serialize + file write: BOM/header/markers intact
//   H8  two pinned windows really exist: visible, class TransFramePinnedWnd,
//       EXSTYLE = TOPMOST|TOOLWINDOW|NOACTIVATE|LAYERED|TRANSPARENT (click-through)
//   H9  interactive switch clears WS_EX_TRANSPARENT on that window only
//   H10 gatherAll keeps both visible; unpinAll destroys both (IsWindow false)
//   H11 tray submenu model renders the live fleet (贴图 1 · 32×24 …)
//
// Build: Tests\manual\build_smoke_pinned_history.bat (cl /MD against the
// sln-built static libs). Run from windows/ — it cleans its temp dir.

#ifdef _WIN32

#include "tf/HistoryExport.hpp"
#include "tf/HistoryStore.hpp"
#include "tf/PinnedWindowManager.hpp"
#include "tf/RecentShots.hpp"
#include "tf/ThumbnailCodec.hpp"
#include "tf/TrayIcon.hpp"

#include <windows.h>
#include <objbase.h>  // CoInitializeEx (WIC + shell plumbing)

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace tf;
using namespace tf::core;

namespace {

int g_failures = 0;

void check(bool ok, const char* step, const std::string& detail = "") {
    if (ok) {
        std::printf("[PASS] %s %s\n", step, detail.c_str());
    } else {
        ++g_failures;
        std::printf("[FAIL] %s %s\n", step, detail.c_str());
    }
}

std::string narrow(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                        static_cast<int>(wide.size()), nullptr, 0,
                                        nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                          out.data(), n, nullptr, nullptr);
    return out;
}

/// Solid test image; distinctive corner pixels for the decode round-trip.
PixelBuffer solidImage(int w, int h, std::uint8_t b, std::uint8_t g, std::uint8_t r) {
    PixelBuffer px;
    px.width = w;
    px.height = h;
    px.stride = w * 4;
    px.data = std::shared_ptr<std::uint8_t[]>(new std::uint8_t[static_cast<std::size_t>(w) * h * 4]);
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
        px.data.get()[i * 4 + 0] = b;
        px.data.get()[i * 4 + 1] = g;
        px.data.get()[i * 4 + 2] = r;
        px.data.get()[i * 4 + 3] = 255;
    }
    px.data.get()[0] = 0xFF;               // top-left = blue-ish marker
    px.data.get()[(static_cast<std::size_t>(w) * h - 1) * 4 + 2] = 0xFF;  // bottom-right red
    return px;
}

std::optional<std::string> readAllBytes(const std::wstring& path) {
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) return std::nullopt;
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool writeAllBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    return static_cast<bool>(out);
}

}  // namespace

int main() {
    std::printf("== TransFrame pinned windows + history smoke (stage 4 FR-14/FR-30) ==\n");
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Temp workspace (cleaned on exit). NOTE: the dir name must not contain
    // "transframe" — screenshot rows carry their file path, and the H3 LIKE
    // search would match the path by accident.
    wchar_t tempRoot[MAX_PATH] = {};
    ::GetTempPathW(MAX_PATH, tempRoot);
    std::wstring dir = std::wstring(tempRoot) + L"tf_pinned_smoke_" +
                       std::to_wstring(::GetCurrentProcessId());
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(dir), ec);
    std::filesystem::create_directories(std::filesystem::path(dir), ec);
    const std::wstring dbPath = dir + L"\\history.db";

    // ---- H1: store opens ---------------------------------------------------
    std::string openError;
    auto store = HistoryStore::open(narrow(dbPath), &openError);
    check(static_cast<bool>(store), "H1 HistoryStore open", openError);
    if (!store) {
        std::printf("== smoke complete: %d failure(s) ==\n", g_failures + 1);
        return 1;
    }

    // ---- H2: four categories, real thumbnail -------------------------------
    TranslationHistory translation;
    translation.id = Uuid::randomUuid();
    translation.sourceText = "Hello TransFrame smoke";
    translation.translatedText = "你好 TransFrame 冒烟";
    translation.sourceLanguage = "auto";
    translation.targetLanguage = "zh";
    translation.timestamp = now();
    translation.category = HistoryCategory::Translation;
    check(store->insert(translation), "H2 insert translation");

    const PixelBuffer shotPixels = solidImage(32, 24, 0x20, 0x80, 0xd0);
    TranslationHistory screenshot = TranslationHistory::screenshotSaved(
        "截图 32x24", narrow(dir + L"\\direct.png"));
    screenshot.id = Uuid::randomUuid();
    screenshot.timestamp = now();
    const auto thumb = encodeJpegThumbnail(shotPixels, 480, 80);
    check(!thumb.empty(), "H2 WIC thumbnail encode",
          "jpeg=" + std::to_string(thumb.size()) + "B");
    screenshot.thumbnailData = thumb;
    check(store->insert(screenshot), "H2 insert screenshot (with thumbnail)");

    TranslationHistory color = TranslationHistory::colorPicked("#D4A017");
    color.timestamp = now();
    check(store->insert(color), "H2 insert color");
    check(store->setFavorite(color.id.toString(), true), "H2 favorite the color entry");

    TranslationHistory upload = TranslationHistory::uploaded(
        "![img](x.png)", "https://example.com/u/abc.png", "TestProvider");
    upload.timestamp = now();
    check(store->insert(upload), "H2 insert upload");
    check(store->totalEntries() == 4, "H2 store holds 4 entries");

    // ---- H3: search / favorites / category ---------------------------------
    {
        HistoryQuery q;
        q.searchText = "transframe";
        const auto hits = store->query(q);
        check(hits.size() == 1 && hits[0].id == translation.id,
              "H3 LIKE search hits translation (ASCII case-insensitive)");
    }
    {
        HistoryQuery q;
        q.favoritesOnly = true;
        const auto favs = store->query(q);
        check(favs.size() == 1 && favs[0].id == color.id, "H3 favorites filter");
    }
    {
        HistoryQuery q;
        q.category = HistoryCategory::Screenshot;
        const auto shots = store->query(q);
        check(shots.size() == 1 && shots[0].hasThumbnail(),
              "H3 category filter (screenshot carries thumbnail bytes)");
    }

    // ---- H4: recentScreenshots cap + order ----------------------------------
    for (int i = 0; i < 7; ++i) {
        TranslationHistory shot = TranslationHistory::screenshotSaved(
            "shot " + std::to_string(i), "");
        shot.id = Uuid::randomUuid();
        shot.timestamp = TimePoint{std::chrono::seconds(std::time(nullptr) + i * 60)};
        store->insert(shot);
    }
    {
        const auto recents = store->recentScreenshots(5);
        check(recents.size() == 5, "H4 recentScreenshots capped at 5",
              "got " + std::to_string(recents.size()));
        bool descending = true;
        for (std::size_t i = 1; i < recents.size(); ++i) {
            if (recents[i - 1].timestamp < recents[i].timestamp) descending = false;
        }
        check(descending && recents.front().sourceText == "shot 6",
              "H4 newest first", "front=" + recents.front().sourceText);
    }

    // ---- H5: entryById + favoriteCount --------------------------------------
    {
        const auto found = store->entryById(translation.id.toString());
        check(found && found->sourceText == translation.sourceText, "H5 entryById");
        check(store->favoriteCount() == 1, "H5 favoriteCount == 1");
    }

    // ---- H6: recordScreenshot / tray strip / pixel restore ------------------
    check(shots::recordScreenshot(*store, shotPixels, dir + L"\\recent"),
          "H6 recordScreenshot (PNG + row)");
    {
        const auto strip = shots::trayRecentShots(*store);
        check(!strip.empty() && strip.front().label.rfind(L"截图 ", 0) == 0,
              "H6 tray strip labels", "front label utf8=" +
                                          narrow(strip.front().label));
        // Find the just-recorded row: the one whose file path points under
        // \recent\ (the H4 rows carry future timestamps and empty paths).
        TranslationHistory recorded;
        for (const auto& e : store->recentScreenshots(20)) {
            if (e.translatedText.find("\\recent\\") != std::string::npos) {
                recorded = e;
                break;
            }
        }
        check(!recorded.id.toString().empty(), "H6 recorded row readable");
        if (!recorded.id.toString().empty()) {
            const PixelBuffer restored = shots::loadRecentShotPixels(recorded);
            check(restored.valid() && restored.width == 32 && restored.height == 24,
                  "H6 loadRecentShotPixels restores full size",
                  restored.valid() ? std::to_string(restored.width) + "x" +
                                         std::to_string(restored.height)
                                   : "invalid");
        }
    }

    // ---- H7: export JSON/CSV/TXT --------------------------------------------
    const std::vector<TranslationHistory> allRows = [&] {
        HistoryQuery q;
        q.limit = 500;
        return store->query(q);
    }();
    {
        const std::string json = serializeHistoryExport(allRows, HistoryExportFormat::Json);
        const std::wstring jsonPath = dir + L"\\export.json";
        bool ok = writeAllBytes(jsonPath, json);
        const auto back = readAllBytes(jsonPath);
        ok = ok && back && *back == json && json.find("\"count\"") != std::string::npos &&
             json.find("你好 TransFrame 冒烟") != std::string::npos &&
             json.find("https://example.com/u/abc.png") != std::string::npos &&
             json.find("#D4A017") != std::string::npos;
        check(ok, "H7 JSON export round-trip",
              json.size() > 0 ? std::to_string(json.size()) + "B" : "empty");
    }
    {
        const std::string csv = serializeHistoryExport(allRows, HistoryExportFormat::Csv);
        const std::wstring csvPath = dir + L"\\export.csv";
        const bool ok = writeAllBytes(csvPath, csv);
        const auto back = readAllBytes(csvPath);
        const bool content = back && back->size() >= 3 &&
                             static_cast<unsigned char>((*back)[0]) == 0xEF &&
                             static_cast<unsigned char>((*back)[1]) == 0xBB &&
                             static_cast<unsigned char>((*back)[2]) == 0xBF &&
                             back->find("id,category,source,translated") != std::string::npos &&
                             back->find("#D4A017") != std::string::npos;
        check(ok && content, "H7 CSV export (BOM + header + rows)");
    }
    {
        const std::string txt = serializeHistoryExport(allRows, HistoryExportFormat::Txt);
        const std::wstring txtPath = dir + L"\\export.txt";
        const bool ok = writeAllBytes(txtPath, txt);
        const auto back = readAllBytes(txtPath);
        const bool content = back && back->find("[取色]") != std::string::npos &&
                             back->find("---") != std::string::npos &&
                             back->find("Hello TransFrame smoke") != std::string::npos;
        check(ok && content, "H7 TXT export blocks");
    }

    // ---- H8: two real pinned windows ----------------------------------------
    auto& pins = PinnedWindowManager::instance();
    const bool pinned1 = pins.pin(0, std::make_shared<PixelBuffer>(solidImage(32, 24, 0, 0, 255)),
                                  std::nullopt);
    const bool pinned2 = pins.pin(77, std::make_shared<PixelBuffer>(solidImage(64, 48, 0, 128, 0)),
                                  std::nullopt);
    check(pinned1 && pinned2 && pins.count() == 2, "H8 pin two windows");
    HWND h1 = nullptr;
    HWND h2 = static_cast<HWND>(pins.hwndForId(77));
    for (const PinnedWindowInfo& info : pins.snapshot()) {
        if (info.id != 77) h1 = static_cast<HWND>(pins.hwndForId(info.id));
    }
    check(h1 != nullptr && h2 != nullptr, "H8 HWNDs resolvable by id");
    if (h1 && h2) {
        auto styleBits = [](HWND h) { return ::GetWindowLongPtrW(h, GWL_EXSTYLE); };
        constexpr LONG_PTR wanted = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                                    WS_EX_LAYERED | WS_EX_TRANSPARENT;
        const LONG_PTR s1 = styleBits(h1);
        const LONG_PTR s2 = styleBits(h2);
        check((s1 & wanted) == wanted && (s2 & wanted) == wanted,
              "H8 EXSTYLE: topmost+tool+noactivate+layered+transparent");
        check(::IsWindowVisible(h1) && ::IsWindowVisible(h2), "H8 both visible");
        wchar_t cls[64] = {};
        ::GetClassNameW(h1, cls, 64);
        check(std::wcscmp(cls, L"TransFramePinnedWnd") == 0, "H8 pinned window class");
        const bool dims = pins.snapshot().size() == 2;
        check(dims, "H8 snapshot lists both",
              "w0=" + std::to_string(pins.snapshot()[0].widthPx));
    }

    // ---- H9: click-through toggle -------------------------------------------
    if (h2) {
        check(pins.setClickThrough(77, false), "H9 set interactive (id 77)");
        const LONG_PTR s2 = ::GetWindowLongPtrW(h2, GWL_EXSTYLE);
        check((s2 & WS_EX_TRANSPARENT) == 0 && (s2 & WS_EX_TOPMOST) != 0,
              "H9 TRANSPARENT cleared, TOPMOST kept");
        bool interactive = false;
        for (const PinnedWindowInfo& info : pins.snapshot()) {
            if (info.id == 77) interactive = !info.clickThrough;
        }
        check(interactive, "H9 snapshot reports interactive");
        if (h1) {
            const LONG_PTR s1 = ::GetWindowLongPtrW(h1, GWL_EXSTYLE);
            check((s1 & WS_EX_TRANSPARENT) != 0, "H9 other window untouched");
        }
    }

    // ---- H10: gather + close all --------------------------------------------
    pins.gatherAll();
    {
        bool allVisible = true;
        for (const PinnedWindowInfo& info : pins.snapshot()) {
            allVisible = allVisible && info.visible &&
                         ::IsWindowVisible(static_cast<HWND>(pins.hwndForId(info.id)));
        }
        check(allVisible && pins.count() == 2, "H10 gatherAll keeps both visible");
    }
    pins.unpinAll();
    check(pins.count() == 0, "H10 unpinAll clears fleet");
    if (h1 && h2) {
        check(!::IsWindow(h1) && !::IsWindow(h2), "H10 both HWNDs destroyed");
    }
    PinnedWindowManager::destroyInstance();

    // ---- H11: tray submenu renders a live fleet ------------------------------
    {
        PinnedWindowState state;
        const auto a = state.pin(0, 32, 24);
        const auto b = state.pin(0, 64, 48);
        std::vector<PinnedSnapshot> snap;
        for (const PinnedWindowInfo& info : state.snapshot()) {
            snap.push_back({info.id, info.widthPx, info.heightPx, info.clickThrough});
        }
        const auto sub = buildPinnedSubmenu(snap);
        check(sub.size() == 5 && a.ok && b.ok &&
                  sub[0].label == L"贴图 1 · 64×48" &&
                  sub[4].command == TrayCommand::PinnedCloseAll,
              "H11 tray submenu lists fleet + actions", narrow(sub[0].label));
    }

    std::filesystem::remove_all(std::filesystem::path(dir), ec);
    std::printf("== smoke complete: %d failure(s) ==\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}

#else  // !_WIN32

#include <cstdio>

int main() {
    std::printf(
        "smoke_pinned_history: Windows-only manual smoke (build with MSVC on "
        "Windows; this host is not Windows — NOT RUN).\n");
    return 0;
}

#endif  // _WIN32
