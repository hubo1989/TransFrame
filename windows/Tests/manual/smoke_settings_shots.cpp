// smoke_settings_shots.cpp — UI 视觉检查：拉起设置窗口，逐页签切换并
// GDI 抓屏存 PNG（tab0_general.png ... tab5_advanced.png），供人工/模型
// 审查布局问题（截断、重叠、对比度、漏翻译等）。
// 切页经 SwitchSettingsTab（与托盘同宿主 STA，DispatcherQueue FIFO），
// 抓屏走屏幕 DC BitBlt：窗口需前台无遮挡（harness 每次前置）。
//
// Build: windows\Tests\manual\build_smoke_settings_shots.cmd
#include "SettingsWindowWinUI.hpp"
#include "tf/Localization.hpp"
#include "tf/Win32Locale.hpp"

#include <windows.h>
#include <wincodec.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace {

struct FindCtx {
    HWND found = nullptr;
};

BOOL CALLBACK enumProc(HWND hwnd, LPARAM lp) {
    wchar_t title[128]{};
    if (::GetWindowTextW(hwnd, title, 128) > 0 &&
        wcscmp(title, L"TransFrame 设置") == 0 && ::IsWindowVisible(hwnd)) {
        reinterpret_cast<FindCtx*>(lp)->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

bool SavePngWic(const wchar_t* path, int w, int h, const std::uint8_t* topDownBgra) {
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return false;

    IWICBitmapEncoder* encoder = nullptr;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) {
        IWICStream* stream = nullptr;
        hr = factory->CreateStream(&stream);
        if (SUCCEEDED(hr)) {
            hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
            if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
            stream->Release();
        }
    }
    IWICBitmapFrameEncode* frame = nullptr;
    if (SUCCEEDED(hr)) {
        hr = encoder->CreateNewFrame(&frame, nullptr);
        if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
        if (SUCCEEDED(hr)) hr = frame->SetSize(w, h);
        if (SUCCEEDED(hr)) {
            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
            hr = frame->SetPixelFormat(&fmt);
        }
        if (SUCCEEDED(hr)) {
            const UINT stride = static_cast<UINT>(w) * 4;
            hr = frame->WritePixels(static_cast<UINT>(h), stride,
                                    stride * static_cast<UINT>(h),
                                    const_cast<BYTE*>(topDownBgra));
        }
        if (SUCCEEDED(hr)) hr = frame->Commit();
        if (SUCCEEDED(hr)) hr = encoder->Commit();
        frame->Release();
    }
    encoder->Release();
    factory->Release();
    return SUCCEEDED(hr);
}

/// 采样判定位图是否全黑（PrintWindow 对不支持的窗口可能给出空内容）。
bool HasContent(const std::uint8_t* pixels, int w, int h) {
    for (int y = 0; y < h; y += 7) {
        const std::uint8_t* row = pixels + static_cast<std::size_t>(y) * w * 4;
        for (int x = 0; x < w; x += 11) {
            const std::uint32_t px = *reinterpret_cast<const std::uint32_t*>(row + x * 4);
            if ((px & 0x00FFFFFF) != 0) return true;  // 非纯黑像素存在
        }
    }
    return false;
}

bool CaptureWindow(HWND hwnd, const wchar_t* path) {
    RECT rc{};
    if (!::GetWindowRect(hwnd, &rc)) return false;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return false;

    // PrintWindow 离屏渲染：窗口无需前台/无遮挡（DComp 内容用
    // PW_RENDERFULLCONTENT）。失败或全黑时退回屏幕 BitBlt（需无遮挡）。
    HDC screen = ::GetDC(nullptr);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = ::SelectObject(mem, bmp);

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    bool ok = ::PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT) != 0;
    if (!ok || !::GetDIBits(mem, bmp, 0, static_cast<UINT>(h), pixels.data(), &info,
                            DIB_RGB_COLORS) ||
        !HasContent(pixels.data(), w, h)) {
        ok = ::BitBlt(mem, 0, 0, w, h, screen, rc.left, rc.top, SRCCOPY | CAPTUREBLT) != 0 &&
             ::GetDIBits(mem, bmp, 0, static_cast<UINT>(h), pixels.data(), &info,
                         DIB_RGB_COLORS) != 0;
    }
    ::SelectObject(mem, old);
    if (ok) ok = SavePngWic(path, w, h, pixels.data());

    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, screen);
    return ok;
}

}  // namespace

namespace {

/// 与 AppMain 相同的字符串表加载（exe 旁 Assets\Strings + engines.json 的
/// appLanguage）。没有它，页面只会显示硬编码 fallback 标签，截图不反映
/// 真实应用的本地化外观。
void LoadLocalizationLikeAppMain() {
    namespace loc = tf::core::loc;
    wchar_t exePath[MAX_PATH]{};
    if (::GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        wchar_t exeDir[MAX_PATH]{};
        if (::wcscpy_s(exeDir, exePath) == 0) {
            if (wchar_t* slash = wcsrchr(exeDir, L'\\')) *slash = L'\0';
            std::string err;
            loc::Localization::instance().loadDirectory(
                std::filesystem::path(std::wstring(exeDir) + L"\\Assets\\Strings"), &err);
        }
    }
    loc::Localization::instance().setSystemLocaleProvider(tf::win32SystemUiLocaleName);
    std::string appLanguage = "system";
    if (const char* appdata = ::getenv("APPDATA")) {
        std::ifstream f(std::string(appdata) + "\\TransFrame\\engines.json");
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            const std::string text = ss.str();
            const auto pos = text.find("\"appLanguage\"");
            if (pos != std::string::npos) {
                const auto q1 = text.find('"', text.find(':', pos) + 1);
                const auto q2 = q1 == std::string::npos ? q1 : text.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    appLanguage = text.substr(q1 + 1, q2 - q1 - 1);
                }
            }
        }
    }
    loc::Localization::instance().setLanguage(appLanguage);
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    LoadLocalizationLikeAppMain();
    if (FAILED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        std::printf("FAIL: CoInitializeEx\n");
        return 1;
    }

    std::printf("launching settings window...\n");
    tf::ui::OpenSettingsWindow(nullptr);

    HWND hwnd = nullptr;
    for (int i = 0; i < 60 && !hwnd; ++i) {
        ::Sleep(250);
        FindCtx ctx;
        if (!::EnumWindows(enumProc, reinterpret_cast<LPARAM>(&ctx))) {
            hwnd = ctx.found;
        }
    }
    if (!hwnd) {
        std::printf("FAIL: no visible settings window after 15s\n");
        return 1;
    }
    std::printf("settings window up (hwnd=%p)\n", static_cast<void*>(hwnd));

    // Optional size override: TF_SHOTS_W / TF_SHOTS_H (pixels; DPI 100% here).
    // Lets the audit compare clipping at the 800x600 default vs a larger pane.
    const char* envW = ::getenv("TF_SHOTS_W");
    const char* envH = ::getenv("TF_SHOTS_H");
    if (envW && envH) {
        const int w = atoi(envW);
        const int h = atoi(envH);
        if (w >= 400 && h >= 300) {
            ::SetWindowPos(hwnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
            ::Sleep(700);  // 等布局回流
        }
    }

    RECT client{};
    ::GetClientRect(hwnd, &client);
    std::printf("client: %ld x %ld\n", client.right, client.bottom);

    static const wchar_t* kNames[6] = {
        L"tab0_general", L"tab1_engines", L"tab2_prompts",
        L"tab3_language", L"tab4_shortcuts", L"tab5_advanced",
    };
    wchar_t dir[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, dir, MAX_PATH);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash) *slash = L'\0';

    for (int i = 0; i < 6; ++i) {
        if (!tf::ui::SwitchSettingsTab(i, 20000)) {
            std::printf("FAIL: SwitchSettingsTab(%d) timed out\n", i);
            return 1;
        }
        ::Sleep(900);  // 渲染稳定余量（页面 OnShown + XAML 布局；PrintWindow 无需前台）
        wchar_t path[MAX_PATH]{};
        _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\%s.png", dir, kNames[i]);
        if (!CaptureWindow(hwnd, path)) {
            std::printf("FAIL: capture %ls\n", kNames[i]);
            return 1;
        }
        std::printf("shot %d/6: %ls\n", i + 1, kNames[i]);
    }
    std::printf("PASS: 6 tab screenshots written next to the exe\n");
    ::CoUninitialize();
    return 0;
}
