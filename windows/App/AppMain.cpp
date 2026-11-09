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
#include "tf/ImageExporter.hpp"
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
#include "PerformanceRecorder.h"
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

// FR-31/32/41 (wt-uploads-picker): image upload + eyedropper + color history.
#include "tf/ColorPicker.hpp"
#include "tf/ColorHistoryWindow.hpp"
#include "tf/ImageUploadProvider.hpp"
#include "tf/ThumbnailCodec.hpp"

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

// FR-26 overlay render mode: replace (default) / append / off. Env override
// TRANSFRAME_OVERLAY_MODE until the settings page wires a control.
enum class OverlayRenderMode { Replace, Append, Off };
OverlayRenderMode g_overlayRenderMode = OverlayRenderMode::Replace;

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
    std::unique_ptr<tf::ColorHistoryWindowController> colorHistoryWindow;

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
constexpr UINT kUploadDone = WM_APP + 0x104;  // FR-31 worker finished an upload

/// Worker -> main hop for a finished upload (clipboard + history + notice
/// all run on the UI thread; the worker only talks HTTP).
struct UploadDoneBox {
    bool ok = false;
    std::wstring message;  // user-facing notice
    std::wstring clipboard;  // URL or markdown, written on success
    tf::core::TranslationHistory entry{};  // upload history record (ok only)
};

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

std::string translateTexts(const std::vector<std::string>& texts, tf::TranslationScene scene,
                           std::vector<std::string>* perSegmentOut = nullptr) {
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
    // FR-26/FR-06: per-segment translations pair 1:1 with the input texts
    // (the scheduler's bundle preserves request order).
    if (perSegmentOut && out.isSuccess() && out.bundle && !out.bundle->results.empty()) {
        *perSegmentOut = std::vector<std::string>();
        for (const auto& seg : out.bundle->results.front().segments) {
            perSegmentOut->push_back(seg.translated);
        }
        if (perSegmentOut->size() != texts.size()) perSegmentOut->clear();  // defensive
    }
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

/// FR-40: drop the recorder's report as %LOCALAPPDATA%\TransFrame\perf\
/// latest.json (atomic). The settings-page export entry (FR-35 高级) reads
/// this later; failures are silent — instrumentation must never break the
/// pipeline.
void writePerfReport() {
    if (g.localDir.empty()) return;
    const std::wstring dir = g.localDir + L"\\perf";
    ::CreateDirectoryW(dir.c_str(), nullptr);  // exists_ok
    writeTextFileAtomic(dir + L"\\latest.json",
                        tf::core::PerformanceRecorder::shared().reportJsonString());
}

/// Screenshot flow: pixels -> OCR -> line merge -> translate -> overlay
/// composite -> result surface (popup or FR-06 in-place panel).
/// Runs on a worker thread (OCR + HTTP block); `opId` links this run's FR-40
/// stage metrics; `inPlace` routes results to the overlay panel instead of
/// the popup (FR-05 toolbar Translate action). Pipeline ticket handling
/// stays with the caller (finishPipeline on its own generation).
void runScreenshotPipeline(const tf::CaptureResult& cap, const std::string& opId,
                           bool inPlace) {
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

    // OCR with the FR-40 stage scope wrapped tightly around the call; the
    // pair carries the end timestamp out for the popup status line.
    const auto ocr = [&] {
        tf::core::PerfScope scope(tf::core::PerfStage::Ocr, opId);
        auto out = g.ocr->recognize(img);
        if (!out.isOk()) scope.fail("ocr");
        return std::make_pair(std::move(out), std::chrono::steady_clock::now());
    }();
    const auto& ocrOutcome = ocr.first;
    const auto tOcr = ocr.second;

    if (!ocrOutcome.isOk()) {
        tf::ResultWindowContent c;
        c.translatedText = L"OCR 失败: " + toWide(ocrOutcome.error.message);
        c.statusLine = L"OCR failed · engine=" + toWide(g.ocr->name());
        c.anchorPhysical = cap.screenshot.sourceDisplay.framePhysical;
        if (inPlace && g.overlay->isActive()) {
            g.overlay->presentNotice(toUtf8(c.translatedText));
        } else {
            showResultOnMain(c);
        }
        return;
    }

    // Merge visual lines before translation (Swift parity).
    std::vector<tf::TextSegment> merged;
    {
        tf::core::PerfScope groupingScope(tf::core::PerfStage::Grouping, opId);
        merged = tf::VisualLineGrouping::mergeLines(ocrOutcome.value.observations);
    }
    std::vector<std::string> texts;
    texts.reserve(merged.size());
    for (const auto& seg : merged) texts.push_back(seg.text);

    std::string translated;
    std::string errorNote;
    std::vector<std::string> perSegment;
    if (!texts.empty()) {
        tf::core::PerfScope translationScope(tf::core::PerfStage::Translation, opId);
        translated = translateTexts(texts, tf::TranslationScene::Screenshot, &perSegment);
        if (translated.rfind("所有引擎失败", 0) == 0) {
            translationScope.fail("allEnginesFailed");
            errorNote = translated;
            translated.clear();
            perSegment.clear();
        }
    } else {
        translated = "(未识别到文字)";
    }
    const auto tEnd = std::chrono::steady_clock::now();

    // FR-26: composite the translations over the captured pixels (offscreen
    // D2D pass). On failure the plain capture keeps flowing downstream.
    tf::PixelBuffer composited;
    if (g_overlayRenderMode != OverlayRenderMode::Off && perSegment.size() == merged.size() &&
        !perSegment.empty()) {
        tf::core::PerfScope renderScope(tf::core::PerfStage::Render, opId);
        std::vector<tf::OverlaySegment> overlaySegments;
        overlaySegments.reserve(merged.size());
        for (std::size_t i = 0; i < merged.size(); ++i) {
            const auto& bb = merged[i].boundingBox;  // normalized 0..1
            overlaySegments.push_back(tf::OverlaySegment{
                tf::Rect{static_cast<std::int64_t>(bb.x * img.width),
                         static_cast<std::int64_t>(bb.y * img.height),
                         static_cast<std::int64_t>(bb.width * img.width),
                         static_cast<std::int64_t>(bb.height * img.height)},
                perSegment[i]});
        }
        composited = tf::renderTranslationOverlay(
            px, overlaySegments,
            g_overlayRenderMode == OverlayRenderMode::Append
                ? tf::OverlayTextMode::AppendBelow
                : tf::OverlayTextMode::ReplaceOriginal);
    }

    tf::ResultWindowContent c;
    // FR-07 preview entry: share the captured pixels with the result popup
    // (shared_ptr; the box hands it to the main thread). With FR-26 the
    // composited buffer replaces the base, so preview save/copy exports
    // carry the overlay.
    c.previewImage = std::make_shared<tf::PixelBuffer>(
        composited.valid() ? std::move(composited) : px);
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
    const std::wstring timing = L"OCR " + std::to_wstring(ms(t0, tOcr)) + L"ms · 翻译 " +
                                std::to_wstring(ms(tOcr, tEnd)) + L"ms";
    c.statusLine = timing + L" · " + c.engineLabel;
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

    if (inPlace) {
        // FR-06: source/translation rows into the overlay panel. The session
        // may have been dismissed while this worker ran — fall back to the
        // popup so the result is never lost.
        if (g.overlay->isActive()) {
            std::vector<tf::OverlayTextRow> rows;
            const std::size_t n =
                std::min(perSegment.size(), merged.size());
            rows.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                rows.push_back(tf::OverlayTextRow{merged[i].text, perSegment[i]});
            }
            if (rows.empty() && errorNote.empty()) {
                g.overlay->presentNotice("(未识别到文字)");
            } else if (!errorNote.empty()) {
                g.overlay->presentNotice(errorNote);
            } else {
                g.overlay->presentResults(std::move(rows), toUtf8(timing + L" · " + c.engineLabel));
            }
        } else {
            showResultOnMain(c);
        }
    } else {
        showResultOnMain(c);
    }
    writePerfReport();  // FR-40: keep perf\latest.json fresh after each run
}

/// Region flow entry (overlay confirmed): capture region -> pipeline.
/// Called on the main thread; capture is fast (GDI/WGC sync); the heavy
/// OCR + translate stages run on a worker thread. Drag selections now park
/// on the FR-05 in-place toolbar; this path handles the remaining completion
/// cases — window-click capture (runs the popup pipeline) and dismissal
/// (releases the pipeline ticket).
void onRegionConfirmed(const tf::SelectionOverlayResult& result, std::uint64_t gen) {
    if (!result.confirmed) {
        finishPipeline(gen);  // Esc / focus loss / toolbar dismiss
        return;
    }

    const std::string opId = tf::core::Uuid::randomUuid().toString();
    std::optional<tf::DisplayInfo> display;
    {
        tf::core::PerfScope displayScope(tf::core::PerfStage::DisplayLookup, opId);
        display = g.displays->displayAt(
            tf::Point{result.regionPhysical.x, result.regionPhysical.y});
        if (!display) displayScope.fail("noDisplay");
    }
    if (!display) {
        tf::ResultWindowContent c;
        c.translatedText = L"无法定位该区域所在的显示器";
        showResultOnMain(c);
        finishPipeline(gen);
        return;
    }

    tf::CaptureResult cap;
    {
        tf::core::PerfScope captureScope(tf::core::PerfStage::Capture, opId);
        cap = g.capture->captureRegion(
            result.regionPhysical, *display,
            result.wholeWindow ? tf::CaptureTrigger::WindowClick
                               : tf::CaptureTrigger::ManualRegion);
        if (!cap.ok) captureScope.fail(toUtf8(cap.error));
    }

    if (cap.ok) {
        // Anchor the popup to the captured region (not the whole display).
        cap.screenshot.sourceDisplay.framePhysical = result.regionPhysical;
    }
    {
        std::lock_guard<std::mutex> lock(g.workersMutex);
        g.workers.emplace_back([cap = std::move(cap), gen, opId]() mutable {
            runScreenshotPipeline(cap, opId, /*inPlace=*/false);
            finishPipeline(gen);
        });
    }
}

/// FR-32 eyedropper entry: one session shared by tray menu, overlay toolbar
/// and (later) hotkey. Completion writes the HEX to the clipboard, records
/// the color history entry (ColorPickFlow.swift parity), live-updates an open
/// color history window, and toasts via the tray balloon — clicking the
/// balloon opens the color history window (spec §4.1 entry #2).
void startColorPickSession() {
    if (!tf::ColorPickerController::instance().beginSession(
            [](const tf::ColorPickResult& result) {
                if (!result.picked) return;
                // Clipboard: CF_UNICODETEXT "#RRGGBB" (main thread already).
                const std::wstring hexW(result.hex.begin(), result.hex.end());
                if (::OpenClipboard(g.hwnd)) {
                    if (::EmptyClipboard()) {
                        const std::size_t bytes = (hexW.size() + 1) * sizeof(wchar_t);
                        if (HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
                            if (void* p = ::GlobalLock(mem)) {
                                std::memcpy(p, hexW.c_str(), bytes);
                                ::GlobalUnlock(mem);
                                if (!::SetClipboardData(CF_UNICODETEXT, mem)) {
                                    ::GlobalFree(mem);
                                }
                            } else {
                                ::GlobalFree(mem);
                            }
                        }
                    }
                    ::CloseClipboard();
                }
                if (g.history) {
                    auto entry = tf::core::TranslationHistory::colorPicked(result.hex);
                    entry.timestamp = tf::core::now();
                    std::string err;
                    g.history->insert(entry, &err);
                    if (!err.empty()) {
                        ::OutputDebugStringA("TransFrame: color history insert failed: ");
                        ::OutputDebugStringA(err.c_str());
                        ::OutputDebugStringA("\n");
                    }
                }
                if (g.colorHistoryWindow) g.colorHistoryWindow->notifyColorPicked();
                if (g.tray) {
                    g.tray->showBalloon(L"已复制颜色",
                                        std::wstring(L"已复制 ") + hexW + L"（点击查看取色历史）");
                }
            })) {
        // Session already active or the lens window is unavailable.
        ::OutputDebugStringW(L"TransFrame: color picker unavailable or already active\n");
    }
}

// ---------------------------------------------------------------------------
// FR-31 image upload (worker thread; result hops back via kUploadDone)
// ---------------------------------------------------------------------------

/// Secrets: CredMan under "TransFrame/uploads/<channel>"; a legacy plaintext
/// value in settings.json migrates into CredMan on first read (then stays
/// out of the JSON, matching the sanitized save path).
tf::ImageUploadSecrets loadUploadSecrets(const tf::ImageUploadConfig& config,
                                         const tf::ImageUploadSecrets& legacy) {
    tf::ImageUploadSecrets out;
    tf::CredManKeyStore cred;
    for (auto channel : {tf::ImageUploadChannel::AliyunOSS, tf::ImageUploadChannel::TencentCOS,
                         tf::ImageUploadChannel::QiniuKodo}) {
        const std::string account = tf::uploadCredentialAccount(channel);
        std::string secret;
        if (const auto stored = cred.load(account); stored && !stored->apiKey.empty()) {
            secret = stored->apiKey;
        } else if (!legacy.secretFor(channel).empty()) {
            secret = legacy.secretFor(channel);
            cred.save(account, tf::StoredCredentials{secret, "", {}});
        }
        (void)config;
        switch (channel) {
            case tf::ImageUploadChannel::AliyunOSS: out.ossAccessKeySecret = secret; break;
            case tf::ImageUploadChannel::TencentCOS: out.cosSecretKey = secret; break;
            case tf::ImageUploadChannel::QiniuKodo: out.kodoSecretKey = secret; break;
            default: break;
        }
    }
    return out;
}

void postUploadDone(bool ok, const std::wstring& message, const std::wstring& clipboard,
                    tf::core::TranslationHistory entry = {}) {
    auto* box = new UploadDoneBox{ok, message, clipboard, std::move(entry)};
    ::PostMessageW(g.hwnd, kUploadDone, 0, reinterpret_cast<LPARAM>(box));
}

/// ProviderError -> readable zh text (category label + provider message).
std::wstring describeUploadError(const tf::ProviderError& e) {
    const wchar_t* cat = L"错误";
    switch (e.category) {
        case tf::ErrorCategory::Network: cat = L"网络错误"; break;
        case tf::ErrorCategory::Quota: cat = L"配额/限流"; break;
        case tf::ErrorCategory::Auth: cat = L"认证失败"; break;
        case tf::ErrorCategory::Content: cat = L"响应异常"; break;
        case tf::ErrorCategory::Configuration: cat = L"配置缺失"; break;
    }
    return std::wstring(cat) + L"：" + toWide(e.message);
}

void runImageUpload(std::shared_ptr<tf::PixelBuffer> pixels) {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
    }
    if (!pixels || !pixels->valid()) {
        postUploadDone(false, L"没有可上传的图像数据", L"");
        return;
    }
    const std::vector<std::uint8_t> png = tf::encodePngToMemory(*pixels);
    if (png.empty()) {
        postUploadDone(false, L"PNG 编码失败（WIC）", L"");
        return;
    }
    const std::string pngBytes(png.begin(), png.end());

    // Config: settings.json section + CredMan secrets.
    tf::ImageUploadConfig config;
    tf::ImageUploadSecrets legacy;
    if (const auto settings = readTextFile(g.roamingDir + L"\\settings.json")) {
        config = tf::parseImageUploadConfig(*settings, &legacy);
    }
    const tf::ImageUploadSecrets secrets = loadUploadSecrets(config, legacy);

    // Filename: TF_<epoch>_<6-char tag>.png (Swift uploadFilename parity).
    const std::string uuid = tf::core::Uuid::randomUuid().toString();
    std::string tag;
    for (const char c : uuid) {
        if (c != '-') tag += c;
        if (tag.size() == 6) break;
    }
    const std::int64_t nowEpoch = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string filename = tf::uploadFilename(nowEpoch, tag);

    // PicGo needs a temp file; the writer/remover keep it out of the
    // protocol layer. %TEMP%\TransFrameUpload\<filename>.
    wchar_t tempRoot[MAX_PATH]{};
    ::GetTempPathW(MAX_PATH, tempRoot);
    std::filesystem::path tempDir = std::filesystem::path(tempRoot) / L"TransFrameUpload";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    const std::filesystem::path tempFile = tempDir / std::filesystem::path(filename);

    tf::WinHttpTransport transport;
    tf::ImageUploadService service(
        std::shared_ptr<tf::IHttpTransport>(&transport, [](tf::IHttpTransport*) {}),
        [&](const std::string& name, std::string_view bytes) {
            const auto path = tempDir / std::filesystem::path(name);
            if (bytes.empty()) return std::string();  // not a write request
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!out) return std::string();
            const std::u8string u8path = path.u8string();
            return std::string(u8path.begin(), u8path.end());
        },
        [](const std::string& pathUtf8) {
            std::error_code del;
            std::filesystem::remove(
                std::filesystem::path(reinterpret_cast<const char8_t*>(pathUtf8.c_str())), del);
        });

    const auto outcome = service.upload(config, secrets, pngBytes, filename, nowEpoch);
    if (outcome.error.has_value()) {
        postUploadDone(false, L"上传失败：" + describeUploadError(*outcome.error), L"");
        return;
    }
    const tf::ImageUploadResult& result = *outcome.result;
    auto entry = tf::core::TranslationHistory::uploaded(
        result.markdown, result.url, tf::imageUploadChannelDisplayName(result.provider));
    entry.timestamp = tf::core::now();
    const std::string clip = tf::ImageUploadService::clipboardText(config, result);
    postUploadDone(true, L"上传成功，已复制到剪贴板：\n" + toWide(result.url),
                   toWide(clip), entry);
}

/// FR-05 toolbar action handler (main thread; overlay window already hidden
/// by the controller so captures see the clean desktop).
void onOverlayToolbarAction(tf::OverlayToolbarAction action, tf::Rect region,
                            std::uint64_t gen) {
    switch (action) {
        case tf::OverlayToolbarAction::Cancel:
            return;  // handled inside the controller
        case tf::OverlayToolbarAction::Pin:
            // 置顶管理 lands with the pin-manager worktree; placeholder.
            g.overlay->presentNotice("置顶管理将在后续版本提供");
            return;
        case tf::OverlayToolbarAction::ColorPick:
            startColorPickSession();  // FR-32
            return;
        default:
            break;
    }

    // Copy / Save / Translate all need the region captured now.
    const std::string opId = tf::core::Uuid::randomUuid().toString();
    const auto display = g.displays->displayAt(tf::Point{region.x, region.y});
    if (!display) {
        tf::ResultWindowContent c;
        c.translatedText = L"无法定位该区域所在的显示器";
        showResultOnMain(c);
        g.overlay->endSession();
        finishPipeline(gen);
        return;
    }
    tf::CaptureResult cap;
    {
        tf::core::PerfScope captureScope(tf::core::PerfStage::Capture, opId);
        cap = g.capture->captureRegion(region, *display,
                                       tf::CaptureTrigger::ManualRegion);
        if (!cap.ok) captureScope.fail(toUtf8(cap.error));
    }
    if (!cap.ok) {
        tf::ResultWindowContent c;
        c.translatedText = L"截图失败: " + cap.error;
        c.statusLine = L"Capture: failed";
        showResultOnMain(c);
        g.overlay->endSession();
        finishPipeline(gen);
        return;
    }

    switch (action) {
        case tf::OverlayToolbarAction::Copy: {
            // FR-05 复制: region pixels to the clipboard as CF_DIB.
            tf::copyBitmapToClipboard(g.hwnd, cap.screenshot.pixels);
            g.overlay->endSession();
            finishPipeline(gen);
            return;
        }
        case tf::OverlayToolbarAction::Save: {
            // FR-05 保存: PNG into the user's Pictures\TransFrame (PRD FR-12
            // default location; the save dialog flow lives in the preview
            // window).
            PWSTR pictures = nullptr;
            std::wstring dir = L"C:\\Users\\Public\\Pictures";
            if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr,
                                                 &pictures))) {
                dir = std::wstring(pictures) + L"\\TransFrame";
                ::CoTaskMemFree(pictures);
            }
            ::CreateDirectoryW(dir.c_str(), nullptr);
            SYSTEMTIME st{};
            ::GetLocalTime(&st);
            wchar_t name[64]{};
            swprintf_s(name, L"TransFrame_%04u%02u%02u_%02u%02u%02u.png",
                       static_cast<unsigned>(st.wYear), static_cast<unsigned>(st.wMonth),
                       static_cast<unsigned>(st.wDay), static_cast<unsigned>(st.wHour),
                       static_cast<unsigned>(st.wMinute),
                       static_cast<unsigned>(st.wSecond));
            const std::wstring path = dir + L"\\" + name;
            std::wstring error;
            if (!tf::encodeToFile(cap.screenshot.pixels, path, tf::ExportFormat::Png,
                                  100, error)) {
                tf::ResultWindowContent c;
                c.translatedText = L"保存失败: " + error;
                showResultOnMain(c);
            }
            g.overlay->endSession();
            finishPipeline(gen);
            return;
        }
        case tf::OverlayToolbarAction::Translate: {
            // FR-04/FR-06: full pipeline on a worker; results land in the
            // overlay's docked panel (the session stays open until the user
            // dismisses it, which fires the completion → finishPipeline).
            cap.screenshot.sourceDisplay.framePhysical = region;
            {
                std::lock_guard<std::mutex> lock(g.workersMutex);
                g.workers.emplace_back([cap = std::move(cap), opId]() mutable {
                    runScreenshotPipeline(cap, opId, /*inPlace=*/true);
                });
            }
            return;
        }
        default:
            g.overlay->endSession();
            finishPipeline(gen);
            return;
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
    const std::string opId = tf::core::Uuid::randomUuid().toString();
    tf::CaptureResult cap;
    {
        tf::core::PerfScope captureScope(tf::core::PerfStage::Capture, opId);
        cap = g.capture->captureDisplay(*primary, tf::CaptureTrigger::HotkeyFullScreen);
        if (!cap.ok) captureScope.fail(toUtf8(cap.error));
    }
    {
        std::lock_guard<std::mutex> lock(g.workersMutex);
        g.workers.emplace_back([cap = std::move(cap), gen, opId]() mutable {
            runScreenshotPipeline(cap, opId, /*inPlace=*/false);
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

void startRegionCapture(bool autoTranslate) {
    const auto gen = tryBeginPipeline();
    if (!gen) return;
    const std::uint64_t myGen = *gen;
    // FR-05: toolbar actions run the region pipeline while the session stays
    // open; the completion fires only when the user dismisses the UI.
    g.overlay->setToolbarActionHandler([myGen](tf::OverlayToolbarAction action,
                                               tf::Rect region) {
        // Main thread (overlay pumps our message loop).
        onOverlayToolbarAction(action, region, myGen);
    });
    // FR-04: Ctrl+Alt+T reaches results with no extra click; Ctrl+Alt+A
    // parks on the in-place toolbar instead.
    g.overlay->setAutoConfirmAction(
        autoTranslate ? std::optional(tf::OverlayToolbarAction::Translate)
                      : std::nullopt);
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
            startRegionCapture(action == tf::HotkeyAction::Translate);
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
        case tf::TrayCommand::Translate:  dispatchAction(tf::HotkeyAction::Translate); break;        case tf::TrayCommand::TextSelection: dispatchAction(tf::HotkeyAction::TextSelection); break;
        case tf::TrayCommand::TranslateInsert: dispatchAction(tf::HotkeyAction::TranslateInsert); break;
        case tf::TrayCommand::CheckUpdates:
            tf::checkForAppUpdate(g.hwnd);
            break;
        case tf::TrayCommand::History:
            if (g.historyWindow) g.historyWindow->show();
            break;
        case tf::TrayCommand::ColorHistory:  // FR-41
            if (g.colorHistoryWindow) g.colorHistoryWindow->show();
            else if (g.historyWindow) g.historyWindow->show();
            break;
        case tf::TrayCommand::PickColor:  // FR-32
            startColorPickSession();
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
        case kUploadDone: {
            // FR-31: clipboard + history + notice, all on the UI thread.
            auto* box = reinterpret_cast<UploadDoneBox*>(lp);
            if (box) {
                if (box->ok) {
                    if (::OpenClipboard(g.hwnd)) {
                        if (::EmptyClipboard()) {
                            const std::size_t bytes =
                                (box->clipboard.size() + 1) * sizeof(wchar_t);
                            if (HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
                                if (void* p = ::GlobalLock(mem)) {
                                    std::memcpy(p, box->clipboard.c_str(), bytes);
                                    ::GlobalUnlock(mem);
                                    if (!::SetClipboardData(CF_UNICODETEXT, mem)) {
                                        ::GlobalFree(mem);
                                    }
                                } else {
                                    ::GlobalFree(mem);
                                }
                            }
                        }
                        ::CloseClipboard();
                    }
                    if (g.history) {
                        std::string err;
                        g.history->insert(box->entry, &err);
                    }
                }
                ::MessageBoxW(g.hwnd, box->message.c_str(),
                              box->ok ? L"图床上传" : L"图床上传失败",
                              box->ok ? MB_ICONINFORMATION : MB_ICONERROR);
                delete box;
            }
            return 0;
        }
        case WM_APP + 0x5446:  // tray callback ('TF' — matches TrayIcon.cpp)
            if (lp == NIN_BALLOONUSERCLICK) {
                // Picker-completion toast jump entry (spec §4.1 entry #2).
                if (g.colorHistoryWindow) g.colorHistoryWindow->show();
                return 0;
            }
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
    // FR-26 overlay mode (replace default) until the settings page lands.
    if (wchar_t mode[16]{}; ::GetEnvironmentVariableW(L"TRANSFRAME_OVERLAY_MODE", mode, 16)) {
        if (_wcsicmp(mode, L"append") == 0) g_overlayRenderMode = OverlayRenderMode::Append;
        else if (_wcsicmp(mode, L"off") == 0) g_overlayRenderMode = OverlayRenderMode::Off;
        else g_overlayRenderMode = OverlayRenderMode::Replace;
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

    // FR-41 color history window: same store as the history window (color
    // entries only, spec §3). FR-32 eyedropper lens: precreate so the tray
    // entry opens in one DWM frame (same contract as the selection overlay).
    if (g.history) {
        g.colorHistoryWindow =
            std::make_unique<tf::ColorHistoryWindowController>(g.history.get());
    }
    tf::ColorPickerController::instance().precreate();

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

    // FR-31 图床上传: preview 上传 button encodes + ships the composited
    // pixels through the active provider (worker thread; result via kUploadDone).
    tf::PreviewWindowController::instance().setUploadHandler(
        [](std::shared_ptr<tf::PixelBuffer> pixels) {
            {
                std::lock_guard<std::mutex> lock(g.workersMutex);
                g.workers.emplace_back(
                    [px = std::move(pixels)]() mutable { runImageUpload(std::move(px)); });
            }
        });

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
