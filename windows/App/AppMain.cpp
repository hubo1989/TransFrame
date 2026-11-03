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
#include <memory>
#include <mutex>
#include <optional>
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
#include "tf/ResultWindow.hpp"
#include "tf/SelectionOverlay.hpp"
#include "tf/TextSelectionInsertion.hpp"
#include "tf/TrayIcon.hpp"
#include "tf/Win32OcrUiaHosts.h"

// Core (translation services)
#include "tf/CoreModels.hpp"
#include "tf/MTranServerProvider.hpp"
#include "tf/TranslationProvider.hpp"
#include "tf/TranslationScheduler.hpp"
#include "tf/VisualLineGrouping.hpp"
#include "tf/WinHttpTransport.h"

namespace {

// ---------------------------------------------------------------------------
// App-level constants
// ---------------------------------------------------------------------------
constexpr wchar_t kHiddenWndClass[] = L"TransFrameMainWnd";
constexpr UINT kAppQuit = WM_APP + 0x100;
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

    // Translation stack.
    std::shared_ptr<tf::WinHttpTransport> transport;
    std::unique_ptr<tf::MTranServerProvider> mtran;
    std::unique_ptr<tf::TranslationScheduler> scheduler;

    // OCR engine + Win32 text-selection host.
    std::unique_ptr<tf::IOcrEngine> ocr;
    std::unique_ptr<tf::Win32TextExtractorHost> textHost;

    std::atomic<bool> pipelineBusy{false};
    std::atomic<std::uint64_t> pipelineGeneration{0};
    tf::CancellationToken shutdownToken;

    // Pipeline workers, kept joinable: teardown waits for in-flight
    // OCR/HTTP instead of the CRT reaping detached threads mid-call.
    std::mutex workersMutex;
    std::vector<std::thread> workers;
};

AppState g;

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
// Translation pipeline (worker thread; UI updates post back to the main thread)
// ---------------------------------------------------------------------------

constexpr UINT kShowResult = WM_APP + 0x101;

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

std::string translateTexts(const std::vector<std::string>& texts) {
    tf::ChainRequest req;
    req.chain.push_back(tf::EngineChainItem::makeBuiltin(tf::TranslationEngineType::MtranServer));
    req.texts = texts;
    req.sourceLanguage = "";  // auto-detect (MTranServer default)
    req.targetLanguage = toUtf8(g_targetLang);
    req.scene = tf::TranslationScene::Screenshot;
    req.cancelToken = g.shutdownToken;

    const tf::SchedulerOutcome out = g.scheduler->translateOrdered(req);
    if (out.isSuccess() && out.bundle && !out.bundle->results.empty()) {
        const auto& results = out.bundle->results;
        // First engine's joined translation.
        std::string joined;
        for (const auto& seg : results.front().segments) {
            if (!joined.empty()) joined += "\n";
            joined += seg.translated;
        }
        return joined;
    }
    std::string err = "所有引擎失败";
    for (const auto& f : out.failures) {
        err += "\n" + f.displayName + ": " + f.errorText;
    }
    return err;
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
        translated = translateTexts(texts);
        if (translated.rfind("所有引擎失败", 0) == 0) {
            errorNote = translated;
            translated.clear();
        }
    } else {
        translated = "(未识别到文字)";
    }
    const auto tEnd = std::chrono::steady_clock::now();

    tf::ResultWindowContent c;
    std::string full;
    for (const auto& seg : ocrOutcome.value.observations) {
        if (!full.empty()) full += "\n";
        full += seg.text;
    }
    c.sourceText = toWide(full);
    c.translatedText = errorNote.empty() ? toWide(translated) : toWide(errorNote);
    c.engineLabel = L"MTransServer";
    const auto ms = [](auto a, auto b) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
    };
    c.statusLine = L"OCR " + std::to_wstring(ms(t0, tOcr)) + L"ms · 翻译 " +
                   std::to_wstring(ms(tOcr, tEnd)) + L"ms · " + c.engineLabel;
    c.anchorPhysical = cap.screenshot.sourceDisplay.framePhysical;
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
        const std::string translated = translateTexts({src});
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

    const std::string translated = translateTexts({src});
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
    if (!g.pipelineBusy.exchange(true)) {
        // The previous worker has released the busy flag and can be reaped
        // before starting the next one; retain no completed thread handles.
        {
            std::lock_guard<std::mutex> lock(g.workersMutex);
            for (auto& worker : g.workers) {
                if (worker.joinable()) worker.join();
            }
            g.workers.clear();
        }
        return g.pipelineGeneration.fetch_add(1) + 1;
    }
    // A running operation retains ownership until it finishes. Elapsed time
    // cannot distinguish a stuck operation from a slow valid one.
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

void onTrayCommand(tf::TrayCommand command, int /*recentIndex*/) {
    switch (command) {
        case tf::TrayCommand::FullScreen: dispatchAction(tf::HotkeyAction::FullScreen); break;
        case tf::TrayCommand::Region:     dispatchAction(tf::HotkeyAction::Region); break;
        case tf::TrayCommand::Translate:  dispatchAction(tf::HotkeyAction::Translate); break;
        case tf::TrayCommand::TextSelection: dispatchAction(tf::HotkeyAction::TextSelection); break;
        case tf::TrayCommand::TranslateInsert: dispatchAction(tf::HotkeyAction::TranslateInsert); break;
        case tf::TrayCommand::CheckUpdates:
            tf::checkForAppUpdate(g.hwnd);
            break;
        case tf::TrayCommand::Exit:
            ::PostMessageW(g.hwnd, kAppQuit, 0, 0);
            break;
        case tf::TrayCommand::RecentShot:
            // Recent-shots history view lands with task D (history store).
            break;
        default:
            break;  // placeholders (settings etc.) — later milestones
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

    // Translation stack: WinHTTP transport + MTranServer (default engine).
    // Endpoint override: TRANSFRAME_MTRAN_URL=http://host:port.
    g.transport = std::make_shared<tf::WinHttpTransport>();
    std::string mtranUrl = "http://localhost:8989";
    if (const DWORD cap = ::GetEnvironmentVariableA("TRANSFRAME_MTRAN_URL", nullptr, 0);
        cap > 0) {
        std::string buf(cap, '\0');
        const DWORD n = ::GetEnvironmentVariableA("TRANSFRAME_MTRAN_URL", buf.data(), cap);
        if (n > 0 && n < cap) mtranUrl = buf.substr(0, n);
    }
    auto mtranConfig = tf::MTranServerProvider::Configuration::parse(mtranUrl);
    if (!mtranConfig) mtranConfig = tf::MTranServerProvider::Configuration::makeDefault();
    g.mtran = std::make_unique<tf::MTranServerProvider>(*mtranConfig, g.transport);

    tf::SchedulerDependencies deps;
    deps.builtinProvider = [](tf::TranslationEngineType t)
        -> tf::TranslationProviderPtr {
        if (t == tf::TranslationEngineType::MtranServer && g.mtran) {
            // Fresh provider instance per run sharing the WinHTTP session.
            return std::make_unique<tf::MTranServerProvider>(g.mtran->configuration(),
                                                             g.transport);
        }
        return nullptr;
    };
    deps.compatibleProvider = [](const std::string&) -> tf::TranslationProviderPtr {
        return nullptr;
    };
    deps.compatibleConfigs = [] { return std::vector<tf::CompatibleConfig>{}; };
    deps.promptResolver = [](tf::TranslationEngineType, tf::TranslationScene,
                             const std::string&) { return std::string(); };
    g.scheduler = std::make_unique<tf::TranslationScheduler>(std::move(deps));

    if (wchar_t buf[16]{}; ::GetEnvironmentVariableW(L"TRANSFRAME_TARGET_LANG", buf, 16)) {
        g_targetLang = buf;
    }

    // Tray icon (FR-34): callbacks route to the hidden message window.
    g.tray = std::make_unique<tf::TrayIconService>(onTrayCommand);
    g.tray->setCallbackWindow(g.hwnd);
    if (!g.tray->install()) {
        // Tray is best-effort in v1: hotkeys keep working without it.
    }

    // Hotkeys (FR-33): Ctrl+Alt+F/A/T/D/I defaults. Registration conflicts
    // (another app owns the combo) surface in the settings UI later.
    g.hotkeys->setHandler([](tf::HotkeyAction action) { dispatchAction(action); });
    (void)g.hotkeys->applySpecs(tf::defaultHotkeySpecs());

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
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.hwnd == nullptr && msg.message == WM_HOTKEY) {
            if (g.hotkeys) g.hotkeys->dispatchWmHotkey(static_cast<int>(msg.wParam));
            continue;
        }
        if (msg.message == kAppQuit) {
            break;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    // Teardown.
    g.hotkeys->unregisterAll();
    g.tray->remove();
    g.shutdownToken.cancel();
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
