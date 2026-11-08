// AppMain.cpp — TransFrame Windows app entry (screenshot-translate flow v1).
//
// Wires the merged modules into the real user flow:
//   tray icon (FR-34) + global hotkeys (FR-33) + selection overlay (FR-01/02)
//   + capture chain (WGC→GDI, PRD §3.2) + Windows OCR (FR-15)
//   + MTranServer translation (default engine, localhost:8989)
//   + result popup (source + translation + copy).
//
// Region flow (Ctrl+Alt+A / tray 区域截图):
//   overlay session -> region -> capture -> OCR -> merge visual lines
//   -> translate -> result popup near the region.
// Full-screen flow (Ctrl+Alt+F): primary display capture, same pipeline.
// 划词 (Ctrl+Alt+D): UIA/clipboard selection text -> translate -> popup.
// 翻译并插入 (Ctrl+Alt+I): 划词 -> translate -> re-validate -> paste.
// Esc during overlay cancels; tray menu Exit quits.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <winrt/Windows.Foundation.h>
#include "UpdateService.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// NativeShell (capture shell + Win32 plumbing)
#include "tf/CaptureService.hpp"
#include "tf/CoreTypes.hpp"
#include "tf/DisplayMonitor.hpp"
#include "tf/HotkeyManager.hpp"
#include "tf/NativeShellInit.hpp"
#include "tf/OcrEngines.hpp"
#include "tf/PinnedWindowManager.hpp"
#include "tf/PreviewWindow.hpp"
#include "tf/RecentShots.hpp"
#include "tf/ResultWindow.hpp"
#include "tf/SelectionOverlay.hpp"
#include "tf/TextSelectionInsertion.hpp"
#include "tf/TrayIcon.hpp"
#include "tf/Win32OcrUiaHosts.h"

// Core (translation services)
#include "EngineBootstrap.hpp"
#include "tf/MTranServerProvider.hpp"
#include "tf/CoreModels.hpp"
#include "tf/CredManKeyStore.h"
#include "tf/TranslationScheduler.hpp"
#include "tf/VisualLineGrouping.hpp"

// Settings + history (FR-35/FR-30, wt-settings branch)
#include "tf/EngineConfigStore.hpp"
#include "tf/HistoryStore.hpp"
#include "tf/HistoryWindow.hpp"
#include "tf/HotkeySettingsStore.hpp"
#include "../UI/SettingsShell/SettingsWindowWinUI.hpp"

#include <fstream>
#include <shlobj.h>
#include "tf/WinHttpTransport.h"

namespace {

// ---------------------------------------------------------------------------
// App-level constants
// ---------------------------------------------------------------------------
constexpr wchar_t kHiddenWndClass[] = L"TransFrameMainWnd";
constexpr UINT kAppQuit = WM_APP + 0x100;
constexpr UINT kRefreshTrayRecents = WM_APP + 0x103;  // worker recorded a shot
const UINT kTaskbarCreated = ::RegisterWindowMessageW(L"TaskbarCreated");

// Default target language (PRD default: auto → zh). Settings UI lands later;
// env override: TRANSFRAME_TARGET_LANG=ja etc.
std::wstring g_targetLang = L"zh";

struct AppState {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;

    // Shell services (main thread only).
    std::unique_ptr<tf::CaptureService> capture;
    std::unique_ptr<tf::DisplayMonitorService> displays;
    std::unique_ptr<tf::WindowDetectorService> windowDetector;
    std::unique_ptr<tf::HotkeyManager> hotkeys;
    std::unique_ptr<tf::SelectionOverlayController> overlay;
    std::unique_ptr<tf::ResultWindowController> results;
    std::unique_ptr<tf::TrayIconService> tray;

    // Translation stack (FR-16..24 engine wiring). Declaration order here is
    // load-bearing: the scheduler's closures capture g.engines, and members
    // are destroyed in reverse declaration order — the scheduler (and its
    // worker threads) must die before the bootstrap they read from.
    std::shared_ptr<tf::WinHttpTransport> transport;
    std::shared_ptr<tf::ICredentialStore> credentials;
    std::unique_ptr<tf::app::EngineBootstrap> engines;
    std::unique_ptr<tf::TranslationScheduler> scheduler;

    // OCR engine + Win32 text-selection host.
    std::unique_ptr<tf::IOcrEngine> ocr;
    std::unique_ptr<tf::Win32TextExtractorHost> textHost;

    std::atomic<bool> pipelineBusy{false};
    std::atomic<std::uint64_t> pipelineGeneration{0};
    // Re-arm anchor: a worker wedged inside an unbounded system call (e.g.
    // a hung UIA/HTTP call) must not lock hotkeys out forever; after
    // kPipelineStuckSeconds a rejected dispatch re-arms.
    std::atomic<std::int64_t> pipelineBusySinceMs{0};
    tf::CancellationToken shutdownToken;

    // Pipeline workers, kept joinable: teardown waits for in-flight
    // OCR/HTTP instead of the CRT reaping detached threads mid-call.
    std::mutex workersMutex;
    std::vector<std::thread> workers;

    // Settings + history (FR-35/FR-30). history is thread-safe (internal
    // mutex); the windows are main-thread-only like every other HWND.
    std::unique_ptr<tf::core::HistoryStore> history;
    std::unique_ptr<tf::HistoryWindowController> historyWindow;

    // FR-14: last capture handed to the preview window (贴图 pins it in place).
    std::shared_ptr<tf::PixelBuffer> lastCapturePixels;
    tf::Rect lastCaptureAnchor{};
    std::vector<tf::HotkeySpec> hotkeySpecs;
    tf::HotkeyRegistrationReport hotkeyReport;
    tf::core::EngineConfig engineConfig;
    std::wstring roamingDir;  // %APPDATA%\TransFrame (engines.json / settings.json)
    std::wstring localDir;    // %LOCALAPPDATA%\TransFrame (history.db)
};

AppState g;

// Busy longer than this counts as a dead worker; dispatch re-arms.
constexpr int kPipelineStuckSeconds = 60;

// Defined with the dispatch helpers below; forward-declared for the early
// flow functions that finish a pipeline run.
void finishPipeline(std::uint64_t gen);

// ---------------------------------------------------------------------------
// UTF-8 <-> UTF-16 helpers
// ---------------------------------------------------------------------------
std::string toUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0,
                                          nullptr, nullptr);
    if (len <= 0) return {};  // conversion failure (invalid input) — no buffer
    std::string out(static_cast<std::size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), len, nullptr,
                          nullptr);
    out.resize(out.size() - 1);
    return out;
}

std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), len);
    out.resize(out.size() - 1);
    return out;
}

// ---------------------------------------------------------------------------
// engines.json loading (FR-16..24 bootstrap)
// ---------------------------------------------------------------------------

/// %APPDATA%\TransFrame\engines.json (UTF-8 JSON). Absent %APPDATA% → empty
/// path → the loader skips the file and the built-in defaults apply.
std::wstring enginesJsonPath() {
    const DWORD cap = ::GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
    if (cap == 0) return {};
    std::wstring appdata(cap, L'\0');
    const DWORD n = ::GetEnvironmentVariableW(L"APPDATA", appdata.data(), cap);
    if (n == 0 || n >= cap) return {};
    appdata.resize(n);
    return appdata + L"\\TransFrame\\engines.json";
}

/// Reads a whole file as UTF-8 bytes (BOM stripped). Nullopt when missing or
/// unreadable; the caller falls back to the built-in engine defaults.
std::optional<std::string> readTextFileUtf8(const std::wstring& path) {
    if (path.empty()) return std::nullopt;
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) return std::nullopt;
    std::string out = ss.str();
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);  // UTF-8 BOM
    }
    return out;
}

// ---------------------------------------------------------------------------
// Settings/history storage (%APPDATA%\TransFrame, %LOCALAPPDATA%\TransFrame)
// ---------------------------------------------------------------------------
std::wstring knownTransFrameDir(int csidl) {
    wchar_t path[MAX_PATH] = {};
    if (::SHGetFolderPathW(nullptr, csidl, nullptr, 0, path) != S_OK) return {};
    std::wstring dir(path);
    dir += L"\\TransFrame";
    ::CreateDirectoryW(dir.c_str(), nullptr);  // exists_ok; created on first save
    return dir;
}

std::optional<std::string> readTextFile(const std::wstring& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return std::nullopt;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return data;
}

/// Contract rule 3 (engines-config-shared.md): tmp + rename, never truncate
/// the live file on failure.
bool writeTextFileAtomic(const std::wstring& path, const std::string& content) {
    const std::wstring tmp = path + L".tmp";
    {
        std::ofstream f(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!f) return false;
    }
    return ::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

// ---------------------------------------------------------------------------
// Translation pipeline (worker thread; UI updates post back to the main thread)
// ---------------------------------------------------------------------------

constexpr UINT kShowResult = WM_APP + 0x101;

/// Defined below (ResultBox plumbing); the tray helpers forward to it.
void showResultOnMain(const tf::ResultWindowContent& c);

// ---------------------------------------------------------------------------
// Tray refresh helpers (main thread; the worker posts kRefreshTrayRecents)
// ---------------------------------------------------------------------------
std::vector<tf::PinnedSnapshot> pinnedSnapshotsForTray() {
    std::vector<tf::PinnedSnapshot> out;
    for (const tf::PinnedWindowInfo& p : tf::PinnedWindowManager::instance().snapshot()) {
        out.push_back({p.id, p.widthPx, p.heightPx, p.clickThrough});
    }
    return out;
}

void refreshTrayPinned() {
    if (g.tray) g.tray->setPinnedWindows(pinnedSnapshotsForTray());
}

void refreshTrayRecents() {
    if (g.tray && g.history) g.tray->setRecentShots(tf::shots::trayRecentShots(*g.history));
}

/// FR-34: tray 最近截图 #index -> preview window (full PNG, thumb fallback).
void openRecentShot(int index) {
    if (!g.history) return;
    const auto recents = g.history->recentScreenshots(5);
    if (index < 0 || index >= static_cast<int>(recents.size())) return;
    tf::PixelBuffer px = tf::shots::loadRecentShotPixels(recents[static_cast<std::size_t>(index)]);
    if (!px.valid()) {
        tf::ResultWindowContent c;
        c.translatedText = L"该截图文件已被清理（历史行仍保留缩略图）";
        showResultOnMain(c);
        return;
    }
    tf::PreviewWindowSpec spec;
    spec.image = std::make_shared<tf::PixelBuffer>(std::move(px));
    spec.suggestedFileName = L"TransFrame_recent";
    spec.titleSuffix = L" · " + std::to_wstring(spec.image->width) + L"×" +
                       std::to_wstring(spec.image->height);
    tf::PreviewWindowController::instance().show(spec);
}

/// Posts content to the main thread; ownership via a small heap box.
struct ResultBox {
    tf::ResultWindowContent content;
};

/// Show the result popup from any thread (main-thread hop for HWND safety).
void showResultOnMain(const tf::ResultWindowContent& c) {
    auto* box = new ResultBox{c};
    if (!::PostMessageW(g.hwnd, kShowResult, 0, reinterpret_cast<LPARAM>(box))) {
        delete box;  // window gone (shutdown)
    }
}

/// Joins the winning engine's segments (compare mode surfaces the first
/// engine's text in the v1 popup; the compare side-by-side UI lands with the
/// settings page). Empty when the whole chain/set failed.
std::string joinedFirstResult(const tf::SchedulerOutcome& out) {
    if (!out.isSuccess() || !out.bundle || out.bundle->results.empty()) return {};
    std::string joined;
    for (const auto& seg : out.bundle->results.front().segments) {
        if (!joined.empty()) joined += "\n";
        joined += seg.translated;
    }
    return joined;
}

/// Aggregates scheduler failures the way the v1 flow expects: the popup flow
/// checks for the "所有引擎失败" prefix to decide between translation and
/// error display.
std::string failureSummary(const tf::SchedulerOutcome& out) {
    std::string err = "所有引擎失败";
    for (const auto& f : out.failures) {
        err += "\n" + f.displayName + ": " + f.errorText;
    }
    // The default engine is a local service; the common cause is simply that
    // it is not running on this machine yet.
    if (err.find("MTranServer") != std::string::npos) {
        err += "\n提示：默认引擎 MTranServer 是本地服务（localhost:8989），"
               "请先启动它，或在 设置→通用 里改用其他引擎端点。";
    }
    return err;
}

/// Display label for the scene's primary engine (popup status line). Falls
/// back to MTranServer when no bootstrap is present.
std::wstring primaryEngineLabel(tf::TranslationScene scene) {
    if (g.engines) {
        const tf::app::SceneRoute route = g.engines->routeForScene(scene);
        if (!route.items.empty()) {
            const auto& item = route.items.front();
            if (item.kind == tf::EngineChainItem::Kind::Compatible) {
                if (const auto c = g.engines->compatibleById(item.compatibleId)) {
                    return toWide(c->displayName);
                }
            }
            return toWide(std::string(tf::translationEngineDisplayName(item.builtin)));
        }
    }
    return L"MTranServer";
}

std::string translateTexts(const std::vector<std::string>& texts, tf::TranslationScene scene) {
    if (!g.scheduler) return std::string("所有引擎失败\n翻译服务未初始化");
    if (!g.engines) return std::string("所有引擎失败\n翻译服务未初始化");

    // FR-23 routing: scene binding → global chain; compare-bound scenes run
    // translateCompare and surface the first engine's text in the v1 popup.
    const tf::app::SceneRoute route = g.engines->routeForScene(scene);
    tf::SchedulerOutcome out;
    if (route.useCompare) {
        tf::CompareRequest creq;
        creq.engines = route.items;
        creq.texts = texts;
        creq.sourceLanguage = "";  // auto-detect (v1 default)
        creq.targetLanguage = toUtf8(g_targetLang);
        creq.scene = scene;
        creq.cancelToken = g.shutdownToken;
        out = g.scheduler->translateCompare(creq);
    } else {
        tf::ChainRequest req;
        req.chain = route.items;
        req.texts = texts;
        req.sourceLanguage = "";  // auto-detect (v1 default)
        req.targetLanguage = toUtf8(g_targetLang);
        req.scene = scene;
        req.cancelToken = g.shutdownToken;
        out = g.scheduler->translateOrdered(req);
    }
    std::string joined = joinedFirstResult(out);
    if (!joined.empty() && g.history) {
        // History insert (FR-30): best-effort; a store failure never
        // blocks the translation flow.
        tf::core::TranslationHistory entry;
        entry.id = tf::core::Uuid::randomUuid();
        std::string source;
        for (const std::string& t : texts) {
            if (!source.empty()) source += "\n";
            source += t;
        }
        entry.sourceText = source;
        entry.translatedText = joined;
        entry.sourceLanguage = "auto";
        entry.targetLanguage = toUtf8(g_targetLang);
        entry.timestamp = tf::core::now();
        entry.category = tf::core::HistoryCategory::Translation;
        std::string storeError;
        g.history->insert(entry, &storeError);
        if (!storeError.empty()) {
            ::OutputDebugStringA("TransFrame: history insert failed: ");
            ::OutputDebugStringA(storeError.c_str());
            ::OutputDebugStringA("\n");
        }
    }
    if (!joined.empty()) return joined;
    return failureSummary(out);
}

/// Screenshot flow: pixels -> OCR -> line merge -> translate -> result popup.
/// Runs on a worker thread (OCR + HTTP block); the popup shows via PostMessage.
void runScreenshotPipeline(const tf::CaptureResult& cap) {
    // Worker threads have no COM apartment by default; Windows OCR (WinRT)
    // activation fails without one. Idempotent-safe: a mismatched apartment
    // is ignored (the call site owns the threading model).
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
    }
    if (!cap.ok) {
        tf::ResultWindowContent c;
        c.translatedText = L"截图失败: " + cap.error;
        c.statusLine = L"Capture: failed";
        c.anchorPhysical = cap.screenshot.sourceDisplay.framePhysical;
        showResultOnMain(c);
        return;
    }

    const auto t0 = std::chrono::steady_clock::now();

    // PixelBuffer (BGRA8; row stride may include alignment padding, D3D
    // staging aligns to 256 and GDI to 4) -> compact OcrImage rows. A flat
    // byteSize() copy would shift every row once stride != width*4.
    tf::OcrImage img;
    img.width = cap.screenshot.pixels.width;
    img.height = cap.screenshot.pixels.height;
    const auto& px = cap.screenshot.pixels;
    const std::size_t rowBytes = static_cast<std::size_t>(px.width) * 4;
    const std::size_t srcStride = static_cast<std::size_t>(px.stride);
    if (px.valid()) {
        img.bgra.resize(rowBytes * static_cast<std::size_t>(px.height));
        for (std::int32_t row = 0; row < px.height; ++row) {
            std::memcpy(img.bgra.data() + row * rowBytes,
                        px.data.get() + row * srcStride, rowBytes);
        }
    }

    auto ocrOutcome = g.ocr->recognize(img);
    const auto tOcr = std::chrono::steady_clock::now();

    if (!ocrOutcome.isOk()) {
        tf::ResultWindowContent c;
        c.translatedText = L"OCR 失败: " + toWide(ocrOutcome.error.message);
        c.statusLine = L"OCR failed · engine=" + toWide(g.ocr->name());
        c.anchorPhysical = cap.screenshot.sourceDisplay.framePhysical;
        showResultOnMain(c);
        return;
    }

    // Merge visual lines before translation (Swift parity).
    std::vector<tf::TextSegment> merged =
        tf::VisualLineGrouping::mergeLines(ocrOutcome.value.observations);
    std::vector<std::string> texts;
    texts.reserve(merged.size());
    for (const auto& seg : merged) texts.push_back(seg.text);

    std::string translated;
    std::string errorNote;
    if (!texts.empty()) {
        translated = translateTexts(texts, tf::TranslationScene::Screenshot);
        if (translated.rfind("所有引擎失败", 0) == 0) {
            errorNote = translated;
            translated.clear();
        }
    } else {
        translated = "(未识别到文字)";
    }
    const auto tEnd = std::chrono::steady_clock::now();

    tf::ResultWindowContent c;
    // FR-07 preview entry: share the captured pixels with the result popup
    // (shared_ptr; the box hands it to the main thread).
    c.previewImage = std::make_shared<tf::PixelBuffer>(cap.screenshot.pixels);
    std::string full;
    for (const auto& seg : ocrOutcome.value.observations) {
        if (!full.empty()) full += "\n";
        full += seg.text;
    }
    c.sourceText = toWide(full);
    c.translatedText = errorNote.empty() ? toWide(translated) : toWide(errorNote);
    c.engineLabel = primaryEngineLabel(tf::TranslationScene::Screenshot);
    const auto ms = [](auto a, auto b) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
    };
    c.statusLine = L"OCR " + std::to_wstring(ms(t0, tOcr)) + L"ms · 翻译 " +
                   std::to_wstring(ms(tOcr, tEnd)) + L"ms · " + c.engineLabel;
    c.anchorPhysical = cap.screenshot.sourceDisplay.framePhysical;

    // FR-14/FR-30/FR-34: remember the pixels for 贴图置顶, record the
    // screenshot history entry (PNG + thumbnail, best-effort) and ping the
    // main thread to rebuild the tray recent-5 strip.
    g.lastCapturePixels = c.previewImage;
    g.lastCaptureAnchor = c.anchorPhysical;
    if (g.history && cap.screenshot.pixels.valid()) {
        tf::shots::recordScreenshot(*g.history, cap.screenshot.pixels,
                                    g.localDir + L"\\recent");
        ::PostMessageW(g.hwnd, kRefreshTrayRecents, 0, 0);
    }
    showResultOnMain(c);
}

/// Region flow entry (overlay confirmed): capture region -> pipeline.
/// Called on the main thread; capture is fast (GDI/WGC sync); the heavy
/// OCR + translate stages run on a worker thread.
void onRegionConfirmed(const tf::SelectionOverlayResult& result, std::uint64_t gen) {
    if (!result.confirmed) {
        finishPipeline(gen);  // Esc / focus loss
        return;
    }

    const std::optional<tf::DisplayInfo> display =
        g.displays->displayAt(tf::Point{result.regionPhysical.x, result.regionPhysical.y});
    if (!display) {
        tf::ResultWindowContent c;
        c.translatedText = L"无法定位该区域所在的显示器";
        showResultOnMain(c);
        finishPipeline(gen);
        return;
    }

    tf::CaptureResult cap = g.capture->captureRegion(
        result.regionPhysical, *display,
        result.wholeWindow ? tf::CaptureTrigger::WindowClick
                           : tf::CaptureTrigger::ManualRegion);

    if (cap.ok) {
        // Anchor the popup to the captured region (not the whole display).
        cap.screenshot.sourceDisplay.framePhysical = result.regionPhysical;
    }
    {
        std::lock_guard<std::mutex> lock(g.workersMutex);
        g.workers.emplace_back([cap = std::move(cap), gen]() mutable {
            runScreenshotPipeline(cap);
            finishPipeline(gen);
        });
    }
}

/// Full-screen flow entry: primary display capture -> pipeline.
void onFullScreen(std::uint64_t gen) {
    const auto displays = g.displays->displays();
    const auto primary = std::find_if(displays.begin(), displays.end(),
                                      [](const tf::DisplayInfo& d) { return d.isPrimary; });
    if (primary == displays.end()) {
        tf::ResultWindowContent c;
        c.translatedText = L"未找到主显示器";
        showResultOnMain(c);
        finishPipeline(gen);
        return;
    }
    tf::CaptureResult cap =
        g.capture->captureDisplay(*primary, tf::CaptureTrigger::HotkeyFullScreen);
    {
        std::lock_guard<std::mutex> lock(g.workersMutex);
        g.workers.emplace_back([cap = std::move(cap), gen]() mutable {
            runScreenshotPipeline(cap);
            finishPipeline(gen);
        });
    }
}

// ---------------------------------------------------------------------------
// 划词 / 翻译并插入 flows (UIA + clipboard fallback)
// ---------------------------------------------------------------------------

void runTextSelectionFlow(bool insertMode) {
    // Detached worker thread: no COM apartment yet, and later stages (WinRT
    // OCR on the insert path) need one.
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
    }
    // Shell-foreground guard: on the desktop / shell UI, Ctrl+C copies FILES
    // (not text) and the shell's clipboard-owner path is the exact
    // synchronous surface that can freeze the taskbar when disturbed while
    // the shell is busy. Refuse early with a clear message instead.
    static const char* const kShellForeground[] = {
        "explorer.exe",
        "startmenuexperiencehost.exe",
        "searchhost.exe",
        "shellexperiencehost.exe",
        "textinputhost.exe",
        "dwm.exe",
    };
    const std::string fgProcess = g.textHost->foregroundProcessName();
    for (const char* shell : kShellForeground) {
        if (_stricmp(fgProcess.c_str(), shell) == 0) {
            tf::ResultWindowContent c;
            c.translatedText = L"前台是桌面/系统界面：请在可选择文本的应用中使用划词翻译";
            c.statusLine = L"Text selection: shell foreground refused";
            c.anchorPhysical = tf::Rect{200, 200, 400, 100};
            showResultOnMain(c);
            return;
        }
    }

    // The current Windows host cannot preserve non-text clipboard formats.
    // Disable the fallback until a lossless snapshot and restore is available.
    tf::TextExtractor extractor(*g.textHost, /*allowClipboardFallback=*/false);

    const tf::ExtractionOutcome extraction = extractor.captureSelectedText(insertMode);
    if (!extraction.selection) {
        tf::ResultWindowContent c;
        c.translatedText = L"当前应用不支持划词";
        c.statusLine = L"划词失败";
        c.anchorPhysical = tf::Rect{200, 200, 400, 100};
        showResultOnMain(c);
        return;
    }

    const std::string src = extraction.selection->text;
    if (src.empty()) {
        tf::ResultWindowContent c;
        c.translatedText = L"选中文本为空";
        showResultOnMain(c);
        return;
    }

    if (insertMode) {
        // Translate then re-validate the target before pasting (FR-29).
        const std::string translated =
            translateTexts({src}, tf::TranslationScene::TranslateAndInsert);
        if (translated.rfind("所有引擎失败", 0) == 0) {
            tf::ResultWindowContent c;
            c.translatedText = toWide(translated);
            showResultOnMain(c);
            return;
        }
        tf::InsertionService inserter(*g.textHost);
        tf::InsertRequest req;
        req.translatedText = translated;
        req.capturedSelection = *extraction.selection;
        std::string err;
        const tf::InsertionOutcome oc = inserter.insert(req, err);
        if (oc != tf::InsertionOutcome::Completed) {
            tf::ResultWindowContent c;
            c.translatedText = L"插入中止: " + toWide(err);
            c.statusLine = L"目标已变化，未粘贴";
            showResultOnMain(c);
        }
        return;
    }

    const std::string translated = translateTexts({src}, tf::TranslationScene::TextSelection);
    tf::ResultWindowContent c;
    c.sourceText = toWide(src);
    c.translatedText = toWide(translated);
    c.statusLine = L"划词翻译";  // insert-mode returned above
    c.anchorPhysical = tf::Rect{200, 200, 400, 100};
    showResultOnMain(c);
}

// ---------------------------------------------------------------------------
// Action dispatch (hotkey + tray)
// ---------------------------------------------------------------------------

/// Returns a ticket only when no pipeline is active.
std::optional<std::uint64_t> tryBeginPipeline() {
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    const bool wasBusy = g.pipelineBusy.exchange(true);
    if (!wasBusy) {
        g.pipelineBusySinceMs.store(nowMs);
        return g.pipelineGeneration.fetch_add(1) + 1;
    }
    // Self-heal: a worker stuck longer than the threshold (normal flows are
    // single-digit seconds; UIA/clipboard calls are timeout-capped) is dead
    // — re-arm over it rather than rejecting forever.
    const std::int64_t since = g.pipelineBusySinceMs.load();
    if (since != 0 && nowMs - since > kPipelineStuckSeconds * 1000) {
        g.pipelineBusySinceMs.store(nowMs);
        return g.pipelineGeneration.fetch_add(1) + 1;
    }
    // A running operation retains ownership until it finishes. Elapsed time
    // cannot distinguish a stuck operation from a slow valid one — but the
    // rejection must be observable, not a dead menu click.
    char path[MAX_PATH];
    if (::GetTempPathA(MAX_PATH, path) > 0) {
        lstrcatA(path, "TransFramePipeline.log");
        HANDLE f = ::CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ,
                                 nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            char line[96];
            const int len = _snprintf_s(line, sizeof(line), _TRUNCATE,
                                        "rejected-busy\n");
            DWORD written = 0;
            if (len > 0) ::WriteFile(f, line, static_cast<DWORD>(len), &written, nullptr);
            ::CloseHandle(f);
        }
    }
    tf::ResultWindowContent c;
    c.translatedText = L"上一次翻译还在进行中，请稍候再试";
    c.statusLine = L"Pipeline busy";
    c.anchorPhysical = tf::Rect{200, 200, 400, 100};
    showResultOnMain(c);
    return std::nullopt;
}

/// Only the current generation's worker may clear the busy flag.
void finishPipeline(std::uint64_t gen) {
    if (g.pipelineGeneration.load() == gen) g.pipelineBusy = false;
}

void startRegionCapture() {
    const auto gen = tryBeginPipeline();
    if (!gen) return;
    const std::uint64_t myGen = *gen;
    const bool began = g.overlay->beginSession(
        [myGen](tf::SelectionOverlayResult result) {
            // Runs on the main thread (overlay pumps our message loop).
            onRegionConfirmed(result, myGen);
        });
    if (!began) {
        finishPipeline(myGen);
        tf::ResultWindowContent c;
        c.translatedText = L"区域截图启动失败（会话被占用或图形设备不可用）";
        c.statusLine = L"Overlay beginSession failed";
        c.anchorPhysical = tf::Rect{200, 200, 400, 100};
        showResultOnMain(c);
    }
}

void dispatchAction(tf::HotkeyAction action) {
    switch (action) {
        case tf::HotkeyAction::FullScreen:
            if (const auto gen = tryBeginPipeline()) {
                onFullScreen(*gen);
                // worker thread finishes the pipeline when done
            }
            break;
        case tf::HotkeyAction::Region:
        case tf::HotkeyAction::Translate:
            startRegionCapture();
            break;
        case tf::HotkeyAction::TextSelection:
        case tf::HotkeyAction::TranslateInsert:
            if (const auto gen = tryBeginPipeline()) {
                const bool insert = action == tf::HotkeyAction::TranslateInsert;
                const std::uint64_t myGen = *gen;
                std::lock_guard<std::mutex> lock(g.workersMutex);
                g.workers.emplace_back([insert, myGen] {
                    runTextSelectionFlow(insert);
                    finishPipeline(myGen);
                });
            }
            break;
        default:
            break;
    }
}

void onTrayCommand(tf::TrayCommand command, int payloadIndex) {
    auto& pins = tf::PinnedWindowManager::instance();
    switch (command) {
        case tf::TrayCommand::FullScreen: dispatchAction(tf::HotkeyAction::FullScreen); break;
        case tf::TrayCommand::Region:     dispatchAction(tf::HotkeyAction::Region); break;
        case tf::TrayCommand::Translate:  dispatchAction(tf::HotkeyAction::Translate); break;
        case tf::TrayCommand::TextSelection: dispatchAction(tf::HotkeyAction::TextSelection); break;
        case tf::TrayCommand::TranslateInsert: dispatchAction(tf::HotkeyAction::TranslateInsert); break;
        case tf::TrayCommand::CheckUpdates:
            tf::checkForAppUpdate(g.hwnd);
            break;
        case tf::TrayCommand::History:
            if (g.historyWindow) g.historyWindow->show();
            break;
        case tf::TrayCommand::Settings:
            tf::ui::OpenSettingsWindow(g.hwnd);
            break;
        case tf::TrayCommand::Exit:
            ::PostMessageW(g.hwnd, kAppQuit, 0, 0);
            break;
        case tf::TrayCommand::RecentShot:
            openRecentShot(payloadIndex);
            break;
        // FR-14 置顶管理 submenu (payloadIndex = pinned index where noted).
        case tf::TrayCommand::PinnedSelect: {
            const auto snap = pins.snapshot();
            if (payloadIndex >= 0 && payloadIndex < static_cast<int>(snap.size())) {
                pins.bringToFront(snap[static_cast<std::size_t>(payloadIndex)].id);
            }
            break;
        }
        case tf::TrayCommand::PinnedToggleThrough: pins.toggleClickThroughFleet(); break;
        case tf::TrayCommand::PinnedGather:        pins.gatherAll(); break;
        case tf::TrayCommand::PinnedCloseAll:      pins.unpinAll(); break;
        default:
            break;  // PinnedManager itself only opens the submenu (never dispatches)
    }
}

// ---------------------------------------------------------------------------
// Hidden main window: tray messages + hotkey pump target
// ---------------------------------------------------------------------------

LRESULT CALLBACK mainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (kTaskbarCreated != 0 && msg == kTaskbarCreated && g.tray) {
        g.tray->remove();
        if (!g.tray->install()) {
            ::OutputDebugStringW(L"TransFrame: failed to restore tray icon after Explorer restart\n");
        }
        return 0;
    }
    switch (msg) {
        case kShowResult: {
            auto* box = reinterpret_cast<ResultBox*>(lp);
            if (box && g.results) g.results->show(box->content);
            delete box;
            return 0;
        }
        case kRefreshTrayRecents:
            refreshTrayRecents();
            return 0;
        case WM_APP + 0x5446:  // tray callback ('TF' — matches TrayIcon.cpp)
            if (lp == WM_LBUTTONUP || lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
                g.tray->showMenuAtCursor();
            }
            return 0;
        case WM_DISPLAYCHANGE:
            if (g.displays) g.displays->invalidate();
            return 0;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        default:
            return ::DefWindowProcW(hwnd, msg, wp, lp);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Entry
// ---------------------------------------------------------------------------


namespace {
void mainLog(const char* step, long v) {
    char path[MAX_PATH];
    if (::GetTempPathA(MAX_PATH, path) == 0) return;
    lstrcatA(path, "TransFrameMain.log");
    HANDLE f = ::CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    char line[128];
    const int len = _snprintf_s(line, sizeof(line), _TRUNCATE, "%s v=%ld\n", step, v);
    DWORD written = 0;
    if (len > 0) ::WriteFile(f, line, static_cast<DWORD>(len), &written, nullptr);
    ::CloseHandle(f);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g.instance = instance;

    // Main thread COM apartment for the WinRT pieces (tray update check and
    // any main-thread WinRT calls).
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
    }

    // Hidden top-level window (popup owner + Explorer restart broadcasts). RegisterHotKey
    // with nullptr hwnd posts WM_HOTKEY to the registering thread's queue —
    // pumping here covers both.
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = mainWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kHiddenWndClass;
    wc.hIcon = ::LoadIconW(instance, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    if (!::RegisterClassExW(&wc)) return 1;

    g.hwnd = ::CreateWindowExW(0, kHiddenWndClass, L"TransFrame", WS_OVERLAPPEDWINDOW,
                               0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!g.hwnd) return 1;

    // Composition root (NativeShellInit.cpp factories).
    g.capture = tf::makeCaptureService();
    g.displays = tf::makeDisplayMonitorService();
    g.windowDetector = tf::makeWindowDetectorService(GetCurrentProcessId());
    g.hotkeys = tf::makeHotkeyManager();
    g.overlay = std::make_unique<tf::SelectionOverlayController>();
    g.overlay->setWindowDetector(g.windowDetector.get());
    g.results = std::make_unique<tf::ResultWindowController>();
    g.ocr = tf::makeWindowsOcrEngine();
    g.textHost = std::make_unique<tf::Win32TextExtractorHost>();
    // FR-28 UIA route: focused element → TextPattern.GetSelection.
    // Clipboard fallback stays disabled (runTextSelectionFlow) to protect
    // non-text clipboard formats, so UIA is the only extraction route.
    g.textHost->focusedElement = tf::MakeWin32UiaFocusedElement();

    // Translation stack (FR-16..24 engine wiring): WinHTTP transport +
    // Credential Manager store + engine bootstrap. engines.json
    // (%APPDATA%\TransFrame\engines.json) drives the 9-engine registry,
    // compatible multi-instance, scene bindings and prompt overrides;
    // missing or malformed file → built-in defaults (MTranServer on
    // localhost:8989, the v1 wiring). Env override stays highest
    // (engines-config-shared.md).
    g.transport = std::make_shared<tf::WinHttpTransport>();
    g.credentials = std::make_shared<tf::CredManKeyStore>();
    g.roamingDir = knownTransFrameDir(CSIDL_APPDATA);
    g.localDir = knownTransFrameDir(CSIDL_LOCAL_APPDATA);
    // Settings-page view of the same engines.json (contract: a corrupt file
    // falls back to defaults, never blocks startup).
    g.engineConfig = tf::core::EngineConfig::makeDefault();
    if (const auto content = readTextFile(g.roamingDir + L"\\engines.json")) {
        g.engineConfig = tf::core::parseEngineConfig(*content).config;
    }
    std::string mtranUrlOverride;
    if (const DWORD cap = ::GetEnvironmentVariableA("TRANSFRAME_MTRAN_URL", nullptr, 0);
        cap > 0) {
        std::string buf(cap, '\0');
        const DWORD n = ::GetEnvironmentVariableA("TRANSFRAME_MTRAN_URL", buf.data(), cap);
        if (n > 0 && n < cap) mtranUrlOverride = buf.substr(0, n);
    }

    tf::app::EngineBootstrapConfig enginesConfig = tf::app::defaultEngineConfig();
    enginesConfig.mtranUrlOverride = mtranUrlOverride;
    if (std::optional<std::string> jsonText = readTextFileUtf8(enginesJsonPath())) {
        try {
            enginesConfig = tf::app::parseEngineConfig(*jsonText);
            enginesConfig.mtranUrlOverride = mtranUrlOverride;
        } catch (const std::exception& e) {
            // Keep the defaults; a broken config must not kill startup.
            std::string note = "TransFrame: engines.json rejected, using defaults: ";
            note += e.what();
            ::OutputDebugStringA(note.c_str());
        }
    }
    g.engines = std::make_unique<tf::app::EngineBootstrap>(std::move(enginesConfig),
                                                           g.credentials, g.transport);
    tf::SchedulerDependencies deps;
    g.engines->populate(deps);
    g.scheduler = std::make_unique<tf::TranslationScheduler>(std::move(deps));

    // Target language: engines.json value first (loaded above), env override
    // kept for the old no-settings path.
    if (!g.engineConfig.targetLang.empty()) {
        g_targetLang = toWide(g.engineConfig.targetLang);
    }
    if (wchar_t buf[16]{}; ::GetEnvironmentVariableW(L"TRANSFRAME_TARGET_LANG", buf, 16)) {
        g_targetLang = buf;
    }

    // History store (FR-30): SQLite/WAL under %LOCALAPPDATA%\TransFrame.
    // Failure degrades to "recording off + history window empty", never to an
    // app abort.
    std::string historyError;
    g.history = tf::core::HistoryStore::open(toUtf8(g.localDir + L"\\history.db"),
                                             &historyError);
    if (!g.history) {
        ::OutputDebugStringA("TransFrame: history store unavailable: ");
        ::OutputDebugStringA(historyError.c_str());
        ::OutputDebugStringA("\n");
    }

    // Tray icon (FR-34): callbacks route to the hidden message window.
    g.tray = std::make_unique<tf::TrayIconService>(onTrayCommand);
    g.tray->setCallbackWindow(g.hwnd);
    if (!g.tray->install()) {
        // Tray is best-effort in v1: hotkeys keep working without it.
    }

    // FR-14 置顶贴图: preview 贴图 button pins the capture in place (1:1 px);
    // fleet changes refresh the tray 置顶管理 submenu.
    tf::PreviewWindowController::instance().setPinHandler([] {
        if (!g.lastCapturePixels || !g.lastCapturePixels->valid()) return;
        const std::optional<std::pair<int, int>> origin{
            std::pair<int, int>{static_cast<int>(g.lastCaptureAnchor.x),
                                static_cast<int>(g.lastCaptureAnchor.y)}};
        tf::PinnedWindowManager::instance().pin(0, g.lastCapturePixels, origin);
    });
    tf::PinnedWindowManager::instance().setOnChanged([] { refreshTrayPinned(); });
    refreshTrayPinned();
    refreshTrayRecents();

    // Hotkeys (FR-33): persisted specs (settings.json) on top of the PRD
    // defaults; conflicts surface in the settings hotkey page.
    g.hotkeySpecs = tf::defaultHotkeySpecs();
    if (const auto content = readTextFile(g.roamingDir + L"\\settings.json")) {
        const auto bindings = tf::core::parseHotkeyBindings(*content);
        for (const tf::core::HotkeyBinding& b : bindings) {
            for (int i = 0; i < 5; ++i) {
                if (b.actionName == tf::core::kHotkeyActionNames[i]) {
                    g.hotkeySpecs[static_cast<std::size_t>(i)].vk = b.vk;
                    g.hotkeySpecs[static_cast<std::size_t>(i)].modifiers = b.modifiers;
                    g.hotkeySpecs[static_cast<std::size_t>(i)].enabled = b.enabled;
                }
            }
        }
    }
    g.hotkeys->setHandler([](tf::HotkeyAction action) { dispatchAction(action); });
    g.hotkeyReport = g.hotkeys->applySpecs(g.hotkeySpecs);

    // Settings persistence (shared by the WinUI 3 page ViewModels once
    // they are wired in): engines.json + hotkey settings, atomic writes.
    // The legacy Win32 settings controller is gone; these free functions
    // are the bridge seam.
    static const auto saveEngineConfig = [](const tf::core::EngineConfig& next) -> std::wstring {
        auto parsed = tf::MTranServerProvider::Configuration::parse(next.mtranBaseUrl);
        if (!parsed) return L"端点无效，需要 http://host[:port]";
        if (next.targetLang.empty()) return L"目标语言不能为空";
        const std::wstring file = g.roamingDir + L"\\engines.json";
        std::string existing;
        if (const auto content = readTextFile(file)) existing = *content;
        const std::string merged = tf::core::serializeEngineConfigPatch(existing, next);
        if (!writeTextFileAtomic(file, merged)) return L"写入 engines.json 失败";
        g.engineConfig = next;
        g_targetLang = toWide(next.targetLang);
        return {};
    };
    static const auto applyHotkeySpecs = [](std::vector<tf::HotkeySpec> specs) -> tf::HotkeyRegistrationReport {
        auto report = g.hotkeys->applySpecs(specs);
        std::vector<tf::core::HotkeyBinding> bindings;
        bindings.reserve(specs.size());
        for (const auto& spec : specs) {
            bindings.push_back({tf::core::kHotkeyActionNames[static_cast<int>(spec.action)],
                                spec.vk, spec.modifiers, spec.enabled});
        }
        const std::wstring file = g.roamingDir + L"\\settings.json";
        std::string existing;
        if (const auto content = readTextFile(file)) existing = *content;
        if (!writeTextFileAtomic(file, tf::core::serializeHotkeyBindings(existing, bindings))) {
            ::OutputDebugStringW(L"TransFrame: hotkey settings save failed\n");
        }
        g.hotkeySpecs = std::move(specs);
        g.hotkeyReport = report;
        return report;
    };

    (void)saveEngineConfig;
    (void)applyHotkeySpecs;

    // Pre-create the overlay window + D2D/DComp stack (<= 60ms hotkey path).
    if (!g.overlay->precreate()) {
        // Region capture needs the overlay; full-screen/划词 still work.
        // Surface the failure — a silent dead menu item looks like a hang.
        tf::ResultWindowContent c;
        c.translatedText = L"区域截图初始化失败（图形设备不可用）";
        c.statusLine = L"Overlay precreate failed";
        c.anchorPhysical = tf::Rect{200, 200, 400, 100};
        showResultOnMain(c);
    }

    // Main loop. WM_HOTKEY arrives as a THREAD message (RegisterHotKey with
    // hwnd=nullptr), so it is dispatched here rather than in a wndProc.
    MSG msg{};
    mainLog("loop-enter", 0);
    for (;;) {
        const BOOL got = ::GetMessageW(&msg, nullptr, 0, 0);
        if (got > 0) {
            if (msg.hwnd == nullptr && msg.message == WM_HOTKEY) {
                if (g.hotkeys) g.hotkeys->dispatchWmHotkey(static_cast<int>(msg.wParam));
                continue;
            }
            if (msg.message == kAppQuit) {
                mainLog("exit-kAppQuit", 0);
                break;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            continue;
        }
        if (got == 0) { mainLog("exit-WM_QUIT", 0); break; }
        mainLog("exit-GetMessage-error", ::GetLastError());
        break;
    }

    // Teardown.
    g.hotkeys->unregisterAll();
    g.tray->remove();
    g.shutdownToken.cancel();
    // FR-14: pinned windows outlive the message loop only until here.
    tf::PinnedWindowManager::instance().unpinAll();
    tf::PinnedWindowManager::destroyInstance();
    // Wait for in-flight pipeline workers (OCR/HTTP) before process exit —
    // the CRT would otherwise kill detached threads mid-system-call.
    {
        std::lock_guard<std::mutex> lock(g.workersMutex);
        for (auto& worker : g.workers) {
            if (worker.joinable()) worker.join();
        }
    }
    MSG pending{};
    while (::PeekMessageW(&pending, g.hwnd, kShowResult, kShowResult, PM_REMOVE)) {
        delete reinterpret_cast<ResultBox*>(pending.lParam);
    }
    ::DestroyWindow(g.hwnd);
    return static_cast<int>(msg.wParam);
}
