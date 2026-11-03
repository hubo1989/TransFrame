// WindowDetectorWin32.cpp — EnumWindows + DwmGetWindowAttribute lister.
//
// Geometry notes (FR-02 acceptance):
//  - DWMWA_EXTENDED_FRAME_BOUNDS returns the *visible* window rect excluding
//    the invisible resize borders/shadows — exactly what the user sees, and
//    what a capture should frame. GetWindowRect would over-include by up to
//    7px per side on Win10/11.
//  - DWMWA_CLOAKED filters suspended UWP windows and cloaked (alt-tab hidden)
//    windows that would otherwise hit-test as invisible ghosts.
//  - EnumWindows yields top-level windows in Z order (topmost first) — the
//    native equivalent of CGWindowListCopyWindowInfo(optionOnScreenOnly).
#include "pch.h"

#include "tf/WindowDetector.hpp"

#include <string>

namespace tf {
namespace {

struct EnumCtx {
    std::vector<WindowInfo>* out;
    DWORD selfPid = 0;
};

bool isAltTabCandidate(HWND hwnd) {
    LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
    LONG_PTR exStyle = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (style & WS_CHILD) {
        // Child windows (browser render hosts, editor panes) stay selectable:
        // FR-02 requires sub-window targeting. EnumWindows only walks
        // top-level HWNDs, so reaching here means WS_CHILD was set on a
        // top-level orphan — skip those.
        return false;
    }
    if (!(style & WS_VISIBLE)) return false;
    if (exStyle & WS_EX_TOOLWINDOW) return false;      // tray/tools: not content windows
    if (exStyle & WS_EX_NOACTIVATE) {
        // Our own overlay windows are NOACTIVATE; never self-highlight.
        return false;
    }
    if (::GetWindow(hwnd, GW_OWNER) != nullptr) {
        // Owned popups (dialogs) are alt-tab candidates; keep them.
    }
    return true;
}

BOOL CALLBACK enumProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lParam);

    if (!isAltTabCandidate(hwnd)) return TRUE;

    // Cloaked check first: cheap and filters ghosts before DWM bounds calls.
    DWORD cloaked = 0;
    if (SUCCEEDED(::DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
        cloaked != 0) {
        return TRUE;  // suspended UWP / hidden virtual desktop window
    }

    RECT frame{};
    if (FAILED(::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                       &frame, sizeof(frame)))) {
        // DWM unavailable (session without DWM): GetWindowRect fallback.
        if (!::GetWindowRect(hwnd, &frame)) return TRUE;
    }

    WindowInfo info;
    info.hwnd = reinterpret_cast<std::uint64_t>(hwnd);
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    info.pid = pid;
    info.frame = Rect{frame.left, frame.top,
                      frame.right - frame.left, frame.bottom - frame.top};
    info.isCloaked = cloaked != 0;
    info.isOnScreen = true;

    // Layered-window transparency (0 => opaque per API contract).
    BYTE alpha = 255;
    DWORD flags = 0;
    if (::GetLayeredWindowAttributes(hwnd, nullptr, &alpha, &flags) &&
        (flags & LWA_ALPHA)) {
        info.alpha = alpha / 255.0;
    } else {
        info.alpha = 1.0;
    }

    wchar_t title[256] = {};
    const int titleLen = ::GetWindowTextW(hwnd, title, 256);
    info.windowName = std::wstring(title, std::max(0, titleLen));

    // Owner name from the process image name; window title is the fallback
    // (mirrors CGWindowListOwnerName semantics loosely).
    HANDLE proc = nullptr;
    if (pid != 0 && pid != ctx->selfPid) {
        proc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    }
    if (proc) {
        wchar_t image[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        if (::QueryFullProcessImageNameW(proc, 0, image, &size) && size > 0) {
            const std::wstring path(image, size);
            const auto slash = path.find_last_of(L"\\/");
            info.ownerName = slash == std::wstring::npos
                                 ? path
                                 : path.substr(slash + 1);
        }
        ::CloseHandle(proc);
    }
    if (info.ownerName.empty()) {
        info.ownerName = info.windowName;
    }

    ctx->out->push_back(std::move(info));
    return TRUE;
}

class Win32WindowLister final : public IWindowLister {
public:
    explicit Win32WindowLister(DWORD excludePid) : selfPid_(excludePid) {}

    std::vector<WindowInfo> listTopmostFirst() override {
        std::vector<WindowInfo> out;
        EnumCtx ctx{&out, selfPid_};
        ::EnumWindows(enumProc, reinterpret_cast<LPARAM>(&ctx));
        // EnumWindows already returns topmost-first; attach z-order indices.
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i].zOrder = static_cast<std::int32_t>(i);
        }
        return out;
    }

private:
    DWORD selfPid_;
};

} // namespace

std::unique_ptr<IWindowLister> makeWin32WindowLister(DWORD excludePid) {
    return std::make_unique<Win32WindowLister>(excludePid);
}

} // namespace tf
