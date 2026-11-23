// Drives the running TransFrame.App through its real overlay HWND, OS mouse
// routing and Copy toolbar action. All screenshots contain only this tool's
// generated fixture. Requires an unlocked input desktop; build with the .cmd.
// Exit != 0 on solid-color dim/selection, click-through, or wrong copied pixels.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <wtsapi32.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
struct Image {
    int w = 0, h = 0;
    std::vector<unsigned char> bgra;
};
int failures = 0;
int phase = 0;
bool blocked = false;
HWND overlay = nullptr;
bool inputDesktopReady() {
    DWORD sessionId = 0;
    ::ProcessIdToSessionId(::GetCurrentProcessId(), &sessionId);
    LPWSTR info = nullptr;
    DWORD bytes = 0;
    if (::WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessionId,
                                      WTSSessionInfoEx, &info, &bytes)) {
        const auto* extended = reinterpret_cast<WTSINFOEXW*>(info);
        const bool locked = bytes >= sizeof(WTSINFOEXW) && extended->Level == 1 &&
            extended->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_LOCK;
        ::WTSFreeMemory(info);
        if (locked) {
            std::puts("[BLOCKED] Windows session is locked");
            blocked = true;
            return false;
        }
    }
    HDESK desk = ::OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP);
    wchar_t name[128]{};
    DWORD needed = 0;
    const bool ready = desk && ::GetUserObjectInformationW(
        desk, UOI_NAME, name, sizeof(name), &needed) && std::wstring(name) == L"Default";
    if (desk) ::CloseDesktop(desk);
    if (!ready) {
        std::puts("[BLOCKED] Interactive desktop changed during smoke");
        blocked = true;
    }
    return ready;
}
void check(bool ok, const char* label) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
void pump(int ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do {
        MSG m{};
        while (::PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&m); ::DispatchMessageW(&m);
        }
        ::Sleep(8);
    } while (std::chrono::steady_clock::now() < until);
}
LRESULT CALLBACK fixtureProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps{}; HDC dc = ::BeginPaint(h, &ps);
        for (int y = 0; y < 640; y += 40) {
            for (int x = 0; x < 960; x += 40) {
                const int cell = x / 40 + y / 40;
                const COLORREF c = RGB(45 + (cell * 37 + phase * 23) % 190,
                                       35 + (x / 40 * 19 + phase * 61) % 200,
                                       40 + (y / 40 * 29 + phase * 41) % 195);
                HBRUSH b = ::CreateSolidBrush(c);
                RECT r{x,y,x+40,y+40}; ::FillRect(dc, &r, b); ::DeleteObject(b);
            }
        }
        ::SetBkMode(dc, TRANSPARENT); ::SetTextColor(dc, RGB(255,255,255));
        RECT title{24,18,900,50};
        ::DrawTextW(dc, L"TransFrame selection regression / generated color grid", -1,
                    &title, DT_LEFT | DT_SINGLELINE);
        ::EndPaint(h, &ps); return 0;
    }
    return ::DefWindowProcW(h,m,w,l);
}
Image screen(RECT r) {
    Image out; out.w=r.right-r.left; out.h=r.bottom-r.top;
    BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=out.w; bi.bmiHeader.biHeight=-out.h;
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    HDC src=::GetDC(nullptr), dst=::CreateCompatibleDC(src);
    void* data=nullptr;
    HBITMAP bmp=::CreateDIBSection(src,&bi,DIB_RGB_COLORS,&data,nullptr,0);
    HGDIOBJ old=::SelectObject(dst,bmp);
    if (bmp && ::BitBlt(dst,0,0,out.w,out.h,src,r.left,r.top,SRCCOPY|CAPTUREBLT)) {
        auto* p=static_cast<unsigned char*>(data);
        out.bgra.assign(p,p+static_cast<size_t>(out.w)*out.h*4);
        for(size_t i=3;i<out.bgra.size();i+=4) out.bgra[i]=255;
    }
    ::SelectObject(dst,old); ::DeleteObject(bmp); ::DeleteDC(dst); ::ReleaseDC(nullptr,src);
    return out;
}
Image crop(const Image& src, RECT r) {
    Image out; out.w=r.right-r.left; out.h=r.bottom-r.top;
    if(src.bgra.empty()) return out;
    out.bgra.resize(static_cast<size_t>(out.w)*out.h*4);
    for(int y=0;y<out.h;++y) std::copy_n(
        src.bgra.data()+(static_cast<size_t>(r.top+y)*src.w+r.left)*4,
        static_cast<size_t>(out.w)*4, out.bgra.data()+static_cast<size_t>(y)*out.w*4);
    return out;
}
bool png(const Image& image,const std::filesystem::path& path) {
    if(image.bgra.empty()) return false;
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr=::CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    ComPtr<IWICStream> stream; ComPtr<IWICBitmapEncoder> enc; ComPtr<IWICBitmapFrameEncode> frame;
    if(SUCCEEDED(hr)) hr=factory->CreateStream(&stream);
    if(SUCCEEDED(hr)) hr=stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE);
    if(SUCCEEDED(hr)) hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&enc);
    if(SUCCEEDED(hr)) hr=enc->Initialize(stream.Get(),WICBitmapEncoderNoCache);
    if(SUCCEEDED(hr)) hr=enc->CreateNewFrame(&frame,nullptr);
    if(SUCCEEDED(hr)) hr=frame->Initialize(nullptr);
    if(SUCCEEDED(hr)) hr=frame->SetSize(image.w,image.h);
    WICPixelFormatGUID fmt=GUID_WICPixelFormat32bppBGRA;
    if(SUCCEEDED(hr)) hr=frame->SetPixelFormat(&fmt);
    if(SUCCEEDED(hr)) hr=frame->WritePixels(image.h,image.w*4,
        static_cast<UINT>(image.bgra.size()),const_cast<BYTE*>(image.bgra.data()));
    if(SUCCEEDED(hr)) hr=frame->Commit();
    if(SUCCEEDED(hr)) hr=enc->Commit();
    return SUCCEEDED(hr);
}
double error(const Image& a,const Image& b,double factor=1.0,int margin=12) {
    if(a.w!=b.w||a.h!=b.h||a.bgra.empty()||b.bgra.empty()) return 1000;
    double total=0; size_t n=0;
    for(int y=margin;y<a.h-margin;y+=5) for(int x=margin;x<a.w-margin;x+=5) {
        const size_t off=(static_cast<size_t>(y)*a.w+x)*4;
        for(int c=0;c<3;++c) {total+=std::abs(a.bgra[off+c]*factor-b.bgra[off+c]);++n;}
    }
    return n ? total/n : 1000;
}
void moveMouse(int x,int y) {
    const int dx=::GetSystemMetrics(SM_XVIRTUALSCREEN),dy=::GetSystemMetrics(SM_YVIRTUALSCREEN);
    INPUT in{}; in.type=INPUT_MOUSE;
    in.mi.dx=::MulDiv(x-dx,65535,::GetSystemMetrics(SM_CXVIRTUALSCREEN)-1);
    in.mi.dy=::MulDiv(y-dy,65535,::GetSystemMetrics(SM_CYVIRTUALSCREEN)-1);
    in.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
    if(::SendInput(1,&in,sizeof(in))!=1) check(false,"OS mouse movement rejected");
}
void button(bool down) {
    INPUT in{}; in.type=INPUT_MOUSE; in.mi.dwFlags=down?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_LEFTUP;
    if(::SendInput(1,&in,sizeof(in))!=1) check(false,"OS mouse button input rejected");
}
Image clipboardImage() {
    Image out;
    if(!::OpenClipboard(nullptr)) return out;
    HANDLE mem=::GetClipboardData(CF_DIB);
    if(mem) {
        const size_t size=::GlobalSize(mem);
        auto* raw=static_cast<unsigned char*>(::GlobalLock(mem));
        if(raw && size>=sizeof(BITMAPINFOHEADER)) {
            const auto* h=reinterpret_cast<const BITMAPINFOHEADER*>(raw);
            size_t offset=h->biSize;
            if(h->biSize==sizeof(BITMAPINFOHEADER) && h->biCompression==BI_BITFIELDS) offset+=12;
            offset+=static_cast<size_t>(h->biClrUsed)*sizeof(RGBQUAD);
            if(h->biWidth>0&&h->biHeight!=0&&h->biBitCount==32&&
               (h->biCompression==BI_RGB||h->biCompression==BI_BITFIELDS)&&h->biHeight!=LONG_MIN) {
                out.w=h->biWidth; out.h=std::abs(h->biHeight);
                const size_t row=static_cast<size_t>(out.w)*4;
                if(out.h<=10000 && out.w<=20000 && offset<=size && row*out.h<=size-offset) {
                    out.bgra.resize(row*out.h);
                    for(int y=0;y<out.h;++y) {
                        const int sy=h->biHeight>0?out.h-1-y:y;
                        std::copy_n(raw+offset+static_cast<size_t>(sy)*row,row,
                                    out.bgra.data()+static_cast<size_t>(y)*row);
                    }
                }
            }
        }
        if(raw) ::GlobalUnlock(mem);
    }
    ::CloseClipboard(); return out;
}
}
int wmain(int argc,wchar_t** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    ::SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ::CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if (!inputDesktopReady()) return 2;
    HWND main=::FindWindowW(L"TransFrameMainWnd",nullptr);
    if(!main) {std::puts("[BLOCKED] Start TransFrame.App before this smoke");return 2;}
    const DWORD tid=::GetWindowThreadProcessId(main,nullptr);
    overlay=::FindWindowW(L"TransFrameOverlayWnd",nullptr);
    if(overlay && ::IsWindowVisible(overlay)) {
        ::PostMessageW(overlay,WM_KEYDOWN,VK_ESCAPE,0);pump(300);
    }
    POINT original{}; ::GetCursorPos(&original);
    WNDCLASSW wc{};wc.lpfnWndProc=fixtureProc;wc.hInstance=::GetModuleHandleW(nullptr);
    wc.lpszClassName=L"TFSelectionRegressionFixture";::RegisterClassW(&wc);
    const int fx=::GetSystemMetrics(SM_XVIRTUALSCREEN)+80;
    const int fy=::GetSystemMetrics(SM_YVIRTUALSCREEN)+80;
    HWND fixture=::CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,wc.lpszClassName,L"Selection fixture",
        WS_POPUP|WS_VISIBLE,fx,fy,960,640,nullptr,nullptr,wc.hInstance,nullptr);
    if(!fixture) return 2;
    const std::filesystem::path out=argc>1?argv[1]:L"build\\selection-smoke";
    std::filesystem::create_directories(out);
    const RECT fixtureRect{fx,fy,fx+960,fy+640};
    const RECT localRegion{120,110,760,470};
    const RECT region{fx+120,fy+110,fx+760,fy+470};
    for(phase=0;phase<2;++phase) {
        if (!inputDesktopReady()) break;
        ::InvalidateRect(fixture,nullptr,FALSE);::UpdateWindow(fixture);pump(300);::DwmFlush();
        if (!inputDesktopReady()) break;
        if (::WindowFromPoint(POINT{fx+480,fy+320}) != fixture) {
            const HWND actual = ::WindowFromPoint(POINT{fx+480,fy+320});
            wchar_t cls[128]{};
            ::GetClassNameW(actual, cls, 128);
            std::printf("[BLOCKED] Generated fixture is not on the input desktop (hit class=%ls)\n", cls);
            png(screen(fixtureRect), out / L"blocked-desktop.png");
            blocked = true;
            break;
        }
        moveMouse(fx+15,fy+15);pump(80);
        const Image before=screen(fixtureRect), expected=crop(before,localRegion);
        const auto prefix=std::wstring(L"round-")+std::to_wstring(phase+1)+L"-";
        check(png(before,out/(prefix+L"desktop.png")),"fixture baseline PNG saved");
        check(::PostThreadMessageW(tid,WM_HOTKEY,1,0)!=FALSE,"region action delivered to app");
        pump(600);::DwmFlush();
        if (!inputDesktopReady()) break;
        overlay=::FindWindowW(L"TransFrameOverlayWnd",nullptr);
        check(overlay&&::IsWindowVisible(overlay),"selection overlay visible");
        if(!overlay||!::IsWindowVisible(overlay)) break;
        check(::WindowFromPoint(POINT{fx+480,fy+320})==overlay,"OS routes pointer to overlay");
        const Image dim=screen(fixtureRect);
        const double dimError=error(before,dim,0.6);
        std::printf("dim mean channel error vs 40%% black: %.3f\n",dimError);
        check(dimError<12,"dim keeps desktop colors visible");
        png(dim,out/(prefix+L"dim.png"));
        moveMouse(region.left,region.top);pump(60);button(true);pump(70);
        for(int i=1;i<=12;++i) {
            moveMouse(region.left+(region.right-region.left)*i/12,
                      region.top+(region.bottom-region.top)*i/12);pump(25);
        }
        button(false);pump(500);::DwmFlush();
        if (!inputDesktopReady()) break;
        check(::IsWindowVisible(overlay)!=FALSE,"drag release keeps selection open");
        check(::WindowFromPoint(POINT{fx+480,fy+320})==overlay,"transparent selection still receives mouse");
        const Image selected=screen(region);
        const double selectedError=error(expected,selected);
        std::printf("selected mean channel error vs original: %.3f\n",selectedError);
        check(selectedError<3,"selected rectangle shows original desktop pixels");
        png(screen(fixtureRect),out/(prefix+L"selection.png"));
        const DWORD sequence=::GetClipboardSequenceNumber();
        const int toolbarLeft=region.left+((region.right-region.left)-316)/2;
        moveMouse(toolbarLeft+18,region.bottom+8+18);pump(100);button(true);pump(40);button(false);
        for(int n=0;n<100;++n) {
            pump(50);
            if (!inputDesktopReady()) break;
            if(::GetClipboardSequenceNumber()!=sequence&&!::IsWindowVisible(overlay))break;
        }
        if (blocked) break;
        check(!::IsWindowVisible(overlay),"Copy action closes selection session");
        check(::GetClipboardSequenceNumber()!=sequence,"Copy writes clipboard image");
        const Image copied=clipboardImage();
        std::printf("clipboard dimensions=%dx%d\n",copied.w,copied.h);
        check(copied.w==expected.w&&copied.h==expected.h&&!copied.bgra.empty(),"clipboard dimensions match physical selection");
        const double copiedError=error(expected,copied);
        std::printf("copied mean channel error vs original: %.3f\n",copiedError);
        check(copiedError<3,"copied pixels match original without dim or stale frame");
        png(copied,out/(prefix+L"copied.png"));
        ::PostMessageW(overlay,WM_KEYDOWN,VK_ESCAPE,0);pump(200);
    }
    // Dismiss even on a failed assertion and restore pointer; never leave a
    // fullscreen overlay or pressed button behind on the person's desktop.
    if(overlay) ::PostMessageW(overlay,WM_KEYDOWN,VK_ESCAPE,0);
    pump(200);::DestroyWindow(fixture);::SetCursorPos(original.x,original.y);
    ::CoUninitialize();
    std::printf("selection smoke: %d failure(s)\n",failures);
    return blocked ? 2 : (failures ? 1 : 0);
}
