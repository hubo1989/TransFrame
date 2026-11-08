// PreviewWindow.cpp — screenshot preview + annotation window (FR-07/08/09/11).
//
// Layout: top toolbar (Win32 child buttons + a status line), below it a D2D
// canvas showing the base pixels with the annotation overlay. The document
// (annotations + 50-step undo) lives in tf::core::AnnotationDocument; this
// controller maps mouse input into image coordinates and paints.
//
// Canvas transform: the image fits the canvas at scale = min(cw/iw, ch/ih, 1),
// centered. Image px -> client px:  client = origin + imagePt * scale.
#include "pch.h"

#include <objbase.h>  // RPC_E_ALREADY_INITIALIZED

#include "tf/PreviewWindow.hpp"
#include "tf/ImageExporter.hpp"

#include "AnnotationDocument.h"
#include "Geometry.h"
#include "Styles.h"

#include <shobjidl.h>   // IFileSaveDialog
#include <wincodec.h>   // WIC upload of the PixelBuffer (pch.h omits it)
#include <cderr.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace tf {

namespace {

using Microsoft::WRL::ComPtr;
using tf::core::Annotation;
using tf::core::AnnotationDocument;
using tf::core::AnnotationTool;
using tf::core::Rect;

constexpr wchar_t kPreviewWndClass[] = L"TransFramePreviewWnd";
constexpr wchar_t kTextInputClass[] = L"TransFramePreviewTextInput";
constexpr int kToolbarHeight = 44;
constexpr int kMinWindowW = 520;
constexpr int kMinWindowH = 400;

// Small helpers shared with the annotation drawing path (static linkage;
// ImageExporter.cpp keeps its own copies in its anonymous namespace).
D2D1_POINT_2F toPoint2F(const core::Point& p) {
    return D2D1_POINT_2F{static_cast<FLOAT>(p.x), static_cast<FLOAT>(p.y)};
}

D2D1_RECT_F toRectF(const core::Rect& r) {
    return D2D1_RECT_F{static_cast<FLOAT>(r.minX()), static_cast<FLOAT>(r.minY()),
                       static_cast<FLOAT>(r.maxX()), static_cast<FLOAT>(r.maxY())};
}

std::string utf8Of(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                          static_cast<int>(wide.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                          utf8.data(), len, nullptr, nullptr);
    return utf8;
}

// Toolbar buttons, left to right.
enum class ToolButton : int {
    Rectangle = 101,
    Text = 102,
    Arrow = 103,
    Undo = 104,
    Redo = 105,
    Save = 106,
    Copy = 107,
    Close = 108,
    Status = 109,  // static text
};

// ------------------------------------------------- text input mini dialog —

struct TextInputState {
    WNDPROC originalEditProc = nullptr;
    HWND edit = nullptr;
    bool confirmed = false;
};

/// Result of the modal text-input popup (Enter confirmed / Esc cancelled).
struct TextInputResult {
    bool confirmed = false;
    std::wstring text;
};

/// One live modal input at a time (single UI thread).
TextInputState* g_textInput = nullptr;

LRESULT CALLBACK textInputEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    TextInputState* state = g_textInput;
    if (msg == WM_CHAR && state) {
        if (wp == VK_RETURN) {
            state->confirmed = true;
            ::PostMessageW(::GetParent(h), WM_CLOSE, 0, 0);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            ::PostMessageW(::GetParent(h), WM_CLOSE, 0, 0);
            return 0;
        }
    }
    return ::CallWindowProcW(state ? state->originalEditProc : ::DefWindowProcW,
                             h, msg, wp, lp);
}

/// Small modal popup: multiline edit + 确定/取消. Enter confirms, Esc cancels.
TextInputResult showTextInput(HWND owner, const std::wstring& title,
                              const POINT& screenPos) {
    TextInputResult result;
    TextInputState state;
    g_textInput = &state;

    const int w = 320, h = 140;
    HWND popup = ::CreateWindowExW(WS_EX_TOOLWINDOW, L"EDIT", title.c_str(),
                                   WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                   screenPos.x, screenPos.y, w, h,
                                   owner, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    if (!popup) {
        g_textInput = nullptr;
        return result;
    }
    // Reuse the EDIT class for the popup frame is clumsy for buttons; build a
    // plain frame + children instead.
    ::DestroyWindow(popup);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = kTextInputClass;
    wc.hbrBackground = ::GetSysColorBrush(COLOR_WINDOW);
    ::RegisterClassExW(&wc);  // ERROR_CLASS_ALREADY_EXISTS tolerated

    popup = ::CreateWindowExW(WS_EX_TOOLWINDOW, kTextInputClass, title.c_str(),
                              WS_POPUP | WS_CAPTION | WS_SYSMENU,
                              screenPos.x, screenPos.y, w, h,
                              owner, nullptr, wc.hInstance, nullptr);
    HWND edit = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                  WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                      ES_AUTOHSCROLL | WS_TABSTOP,
                                  8, 8, w - 16, h - 56, popup,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(2001)),
                                  wc.hInstance, nullptr);
    HWND ok = ::CreateWindowExW(0, L"BUTTON", L"确定",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                                w - 176, h - 40, 80, 28, popup,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(2002)),
                                wc.hInstance, nullptr);
    HWND cancel = ::CreateWindowExW(0, L"BUTTON", L"取消",
                                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                                    w - 88, h - 40, 80, 28, popup,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(2003)),
                                    wc.hInstance, nullptr);
    (void)ok;
    (void)cancel;

    state.edit = edit;
    state.originalEditProc = reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(
        edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&textInputEditProc)));
    ::SetFocus(edit);
    ::ShowWindow(popup, SW_SHOW);

    // Local modal loop; re-post WM_QUIT like DialogBox would.
    MSG msg;
    bool done = false;
    while (!done && ::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_QUIT) {
            ::PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        // WM_COMMAND from the popup's children, or WM_CLOSE on the popup.
        if (msg.hwnd == popup && msg.message == WM_CLOSE) {
            done = true;
        } else if (msg.message == WM_COMMAND) {
            const int code = HIWORD(msg.wParam);
            if (msg.hwnd == ok && code == BN_CLICKED) {
                state.confirmed = true;
                done = true;
            } else if (msg.hwnd == cancel && code == BN_CLICKED) {
                done = true;
            }
        }
        if (!done) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }
    if (state.confirmed) {
        const int len = ::GetWindowTextLengthW(edit) + 1;
        std::wstring text(static_cast<std::size_t>(len), L'\0');
        ::GetWindowTextW(edit, text.data(), len);
        text.resize(std::wcslen(text.c_str()));
        result.confirmed = !text.empty();
        result.text = text;
    }
    ::DestroyWindow(popup);
    ::UnregisterClassW(kTextInputClass, wc.hInstance);
    g_textInput = nullptr;
    return result;
}

// --------------------------------------------------------------- the Impl —

constexpr int kBtnW = 64;
constexpr int kBtnH = 28;
constexpr int kBtnGap = 8;

}  // namespace

struct PreviewWindowController::Impl {
    HWND hwnd = nullptr;
    HINSTANCE instance = nullptr;
    PreviewWindowSpec spec;
    AnnotationDocument document;
    AnnotationTool activeTool = AnnotationTool::None;
    std::optional<tf::core::Uuid> selectedId;

    // Drag state (image pixel coordinates).
    bool dragging = false;
    bool moving = false;
    core::Point dragStart{};
    core::Point dragCurrent{};
    core::Point moveLast{};

    // Toolbar child HWNDs keyed by ToolButton.
    HWND buttons[9] = {};

    // D2D device stack (created lazily per window; no DComp needed here).
    ComPtr<ID2D1Factory> d2d;
    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1Bitmap> baseBitmap;
    ComPtr<ID2D1StrokeStyle> dashStyle;

    // Canvas transform (recomputed on paint/resize).
    D2D1_POINT_2F origin{};  // client px of image (0,0)
    FLOAT scale = 1.0f;
    FLOAT imageW = 0.f, imageH = 0.f;
    std::wstring statusText = L"工具: 矩形（R/1）· 拖拽绘制 · Shift=正方形/45°";

    static Impl* selfFromHwnd(HWND h) {
        return reinterpret_cast<Impl*>(::GetWindowLongPtrW(h, GWLP_USERDATA));
    }

    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        Impl* self = selfFromHwnd(h);
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            ::SetWindowLongPtrW(h, GWLP_USERDATA,
                                reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            self = static_cast<Impl*>(cs->lpCreateParams);
        }
        if (self) return self->handle(h, msg, wp, lp);
        return ::DefWindowProcW(h, msg, wp, lp);
    }

    // ---- view transform ------------------------------------------------
    void computeTransform() {
        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        const FLOAT canvasW = static_cast<FLOAT>(rc.right - rc.left);
        const FLOAT canvasH = static_cast<FLOAT>(rc.bottom - rc.top - kToolbarHeight);
        if (canvasW <= 0 || canvasH <= 0 || imageW <= 0 || imageH <= 0) {
            scale = 1.0f;
            origin = D2D1_POINT_2F{0, static_cast<FLOAT>(kToolbarHeight)};
            return;
        }
        scale = std::min({canvasW / imageW, canvasH / imageH, 1.0f});
        origin = D2D1_POINT_2F{(canvasW - imageW * scale) / 2,
                               kToolbarHeight + (canvasH - imageH * scale) / 2};
    }

    core::Point clientToImage(POINT pt) const {
        if (scale <= 0) return core::Point{0, 0};
        return core::Point{(pt.x - origin.x) / scale, (pt.y - origin.y) / scale};
    }

    core::Rect selectedBounds() const {
        if (!selectedId) return core::Rect{};
        const Annotation* a = document.findById(*selectedId);
        if (!a) return core::Rect{};
        return std::visit(
            [](const auto& payload) -> core::Rect {
                using T = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<T, tf::core::TextAnnotation>) {
                    return tf::core::estimatedTextBounds(payload);
                } else {
                    return payload.bounds();
                }
            },
            a->payload);
    }

    // ---- D2D resources ---------------------------------------------------
    bool ensureRenderResources() {
        if (!d2d) {
            if (FAILED(::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                           IID_PPV_ARGS(&d2d)))) {
                return false;
            }
            D2D1_STROKE_STYLE_PROPERTIES dashProps{};
            dashProps.dashStyle = D2D1_DASH_STYLE_DASH;
            d2d->CreateStrokeStyle(dashProps, nullptr, 0, &dashStyle);
        }
        if (rt) return true;
        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        const D2D1_SIZE_U size{static_cast<UINT>(rc.right - rc.left),
                               static_cast<UINT>(rc.bottom - rc.top)};
        const D2D1_PIXEL_FORMAT fmt = D2D1::PixelFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        const D2D1_RENDER_TARGET_PROPERTIES props =
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, fmt);
        const D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps =
            D2D1::HwndRenderTargetProperties(hwnd, size);
        return SUCCEEDED(d2d->CreateHwndRenderTarget(props, hwndProps, &rt));
    }

    void releaseRenderResources() {
        dashStyle.Reset();
        baseBitmap.Reset();
        rt.Reset();
    }

    /// Upload the PixelBuffer into the D2D bitmap once per capture (WIC
    /// intermediate; the buffer is BGRA8 with opaque alpha).
    void ensureBaseBitmap() {
        if (baseBitmap || !spec.image || !spec.image->valid() || !rt) return;
        ComPtr<IWICImagingFactory> wic;
        if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
            return;
        }
        // CreateBitmapFromMemory wraps the capture buffer directly (no
        // upload copy, no mutation — CopyPixels on a fresh bitmap would read
        // zeros back over the caller's pixels). The shared_ptr in spec.image
        // keeps the buffer alive for the bitmap's lifetime.
        ComPtr<IWICBitmap> wicBitmap;
        if (FAILED(wic->CreateBitmapFromMemory(
                static_cast<UINT>(spec.image->width),
                static_cast<UINT>(spec.image->height), GUID_WICPixelFormat32bppBGRA,
                static_cast<UINT>(spec.image->stride),
                static_cast<UINT>(spec.image->byteSize()), spec.image->data.get(),
                &wicBitmap))) {
            return;
        }
        rt->CreateBitmapFromWicBitmap(wicBitmap.Get(), &baseBitmap);
    }

    void render() {
        if (!ensureRenderResources()) return;
        ensureBaseBitmap();
        computeTransform();
        rt->BeginDraw();
        rt->SetTransform(D2D1::Matrix3x2F::Identity());
        rt->Clear(D2D1_COLOR_F{0.08f, 0.08f, 0.08f, 1.f});
        if (baseBitmap) {
            // client = image * scale + origin  (row-vector order: scale then
            // translate; scaling around the canvas origin keeps 1:1 zooms exact)
            rt->SetTransform(D2D1::Matrix3x2F::Scale(D2D1_SIZE_F{scale, scale},
                                                     D2D1_POINT_2F{0.f, 0.f}) *
                             D2D1::Matrix3x2F::Translation(origin.x, origin.y));
            rt->DrawBitmap(baseBitmap.Get(),
                           D2D1_RECT_F{0.f, 0.f, imageW, imageH}, 1.0f,
                           D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            drawAnnotations(*rt.Get(), document.annotations());

            // In-flight drag preview.
            if (dragging) {
                drawDragPreview(*rt.Get());
            }
            // Selection box (dashed, drawn in image space; scale via transform).
            const core::Rect b = selectedBounds();
            if (b.width > 0 || b.height > 0) {
                ComPtr<ID2D1SolidColorBrush> sel;
                if (SUCCEEDED(rt->CreateSolidColorBrush(
                        D2D1_COLOR_F{0.1f, 0.6f, 1.f, 1.f}, &sel))) {
                    rt->DrawRectangle(toRectF(b), sel.Get(), 1.5f / scale,
                                      dashStyle.Get());
                }
            }
        }
        const HRESULT hr = rt->EndDraw();
        if (hr == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
            releaseRenderResources();
        }
        (void)hr;
    }

    void drawDragPreview(ID2D1RenderTarget& target) {
        ComPtr<ID2D1SolidColorBrush> brush;
        if (FAILED(target.CreateSolidColorBrush(
                D2D1_COLOR_F{0.1f, 0.6f, 1.f, 1.f}, &brush))) {
            return;
        }
        if (activeTool == AnnotationTool::Rectangle) {
            target.DrawRectangle(toRectF(normalizeRect(dragStart, dragCurrent)),
                                 brush.Get(), 1.5f / scale);
        } else if (activeTool == AnnotationTool::Arrow) {
            const core::Point end = ::GetKeyState(VK_SHIFT) < 0
                ? shiftConstrain(dragStart, dragCurrent, false)
                : dragCurrent;
            target.DrawLine(toPoint2F(dragStart), toPoint2F(end), brush.Get(),
                            2.0f / scale);
        }
    }

    // ---- toolbar ----------------------------------------------------------
    void buildToolbar() {
        instance = ::GetModuleHandleW(nullptr);
        struct Btn { ToolButton id; const wchar_t* label; };
        const Btn defs[] = {
            {ToolButton::Rectangle, L"矩形"},
            {ToolButton::Text, L"文字"},
            {ToolButton::Arrow, L"箭头"},
            {ToolButton::Undo, L"撤销"},
            {ToolButton::Redo, L"重做"},
            {ToolButton::Save, L"保存"},
            {ToolButton::Copy, L"复制"},
            {ToolButton::Close, L"关闭"},
        };
        int x = 8;
        for (const Btn& def : defs) {
            buttons[static_cast<int>(def.id) - 101] = ::CreateWindowExW(
                0, L"BUTTON", def.label,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                x, 8, kBtnW, kBtnH, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(def.id)),
                instance, nullptr);
            ::SendMessageW(buttons[static_cast<int>(def.id) - 101],
                           WM_SETFONT,
                           reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)),
                           TRUE);
            x += kBtnW + kBtnGap;
        }
        buttons[static_cast<int>(ToolButton::Status) - 101] = ::CreateWindowExW(
            0, L"STATIC", statusText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS,
            x + 8, 14, 620, 20, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ToolButton::Status)),
            instance, nullptr);
        ::SendMessageW(buttons[static_cast<int>(ToolButton::Status) - 101],
                       WM_SETFONT,
                       reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)),
                       TRUE);
    }

    void setStatus(const std::wstring& text) {
        statusText = text;
        if (buttons[static_cast<int>(ToolButton::Status) - 101]) {
            ::SetWindowTextW(buttons[static_cast<int>(ToolButton::Status) - 101],
                             text.c_str());
        }
    }

    void setTool(AnnotationTool tool) {
        activeTool = tool;
        selectedId.reset();  // tool switch drops the selection (drag vs. select)
        switch (tool) {
            case AnnotationTool::Rectangle: setStatus(L"工具: 矩形（R/1）· Shift=正方形"); break;
            case AnnotationTool::Text: setStatus(L"工具: 文字（T/4）· 点击画布输入"); break;
            case AnnotationTool::Arrow: setStatus(L"工具: 箭头（A/3）· Shift=45°"); break;
            case AnnotationTool::None: setStatus(L"工具: 选择/移动（点击标注，Del 删除）"); break;
        }
        ::InvalidateRect(hwnd, nullptr, FALSE);
    }

    void syncHistoryButtons() {
        if (buttons[static_cast<int>(ToolButton::Undo) - 101]) {
            ::EnableWindow(buttons[static_cast<int>(ToolButton::Undo) - 101],
                           document.canUndo() ? TRUE : FALSE);
        }
        if (buttons[static_cast<int>(ToolButton::Redo) - 101]) {
            ::EnableWindow(buttons[static_cast<int>(ToolButton::Redo) - 101],
                           document.canRedo() ? TRUE : FALSE);
        }
    }

    // ---- save / copy (FR-12) ----------------------------------------------
    PixelBuffer composited() {
        PixelBuffer out = renderComposited(spec.image ? *spec.image : PixelBuffer{},
                                           document);
        return out;
    }

    void notify(const std::wstring& message) {
        ::MessageBoxW(hwnd, message.c_str(), L"TransFrame 预览", MB_OK | MB_ICONINFORMATION);
    }

    void saveFlow() {
        if (!spec.image || !spec.image->valid()) {
            notify(L"没有可保存的图像");
            return;
        }
        // COM for the common dialog (safe to call when already initialized).
        const HRESULT com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        struct ComGuard {
            HRESULT hr;
            ~ComGuard() { if (SUCCEEDED(hr)) ::CoUninitialize(); }
        } comGuard{com};
        constexpr HRESULT kRpcEAlreadyInitialized = 0x80010106;  // RPC_E_ALREADY_INITIALIZED
        if (FAILED(com) && com != kRpcEAlreadyInitialized) {
            notify(L"COM 初始化失败，无法打开保存对话框");
            return;
        }

        const bool heicOk = heicEncoderAvailable();
        ComPtr<IFileSaveDialog> dialog;
        if (FAILED(::CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(&dialog)))) {
            notify(L"无法打开保存对话框");
            return;
        }
        // FR-12: HEIC only enters the filter list when the OS can encode it.
        std::vector<COMDLG_FILTERSPEC> filters{
            {L"PNG 图片", L"*.png"},
            {L"JPEG 图片", L"*.jpg"},
            {L"HEIC 图片", L"*.heic"},
            {L"所有文件", L"*.*"},
        };
        if (!heicOk) filters.erase(filters.begin() + 2);
        dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());
        dialog->SetFileTypeIndex(1);
        dialog->SetDefaultExtension(L"png");
        if (!spec.suggestedFileName.empty()) {
            dialog->SetFileName(spec.suggestedFileName.c_str());
        }
        dialog->SetTitle(L"保存截图");
        const HRESULT chosen = dialog->Show(hwnd);
        if (chosen == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
        if (FAILED(chosen)) {
            notify(L"保存对话框失败");
            return;
        }
        ComPtr<IShellItem> item;
        if (FAILED(dialog->GetResult(&item)) || !item) return;
        PWSTR pathRaw = nullptr;
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &pathRaw))) return;
        std::wstring path(pathRaw);
        ::CoTaskMemFree(pathRaw);

        // Selected format from the chosen extension (dialog filter is advisory).
        ExportFormat format = ExportFormat::Png;
        std::wstring lower = path;
        for (wchar_t& c : lower) c = towlower(c);
        if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, L".jpg") == 0) {
            format = ExportFormat::Jpeg;
        } else if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, L".jpeg") == 0) {
            format = ExportFormat::Jpeg;
        } else if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, L".heic") == 0) {
            format = ExportFormat::Heic;
        }

        PixelBuffer composed = composited();
        std::wstring error;
        if (encodeToFile(composed, path, format, 90, error)) {
            setStatus(L"已保存: " + path);
            return;
        }
        // FR-12: no HEIC encoder -> say so, fall back to PNG on confirmation.
        if (format == ExportFormat::Heic) {
            const int yes = ::MessageBoxW(hwnd,
                                          (error + L"\n\n是否改存为 PNG？").c_str(),
                                          L"TransFrame 预览", MB_YESNO | MB_ICONWARNING);
            if (yes == IDYES) {
                std::wstring pngPath = path;
                pngPath.replace(pngPath.size() - 5, 5, L".png");
                std::wstring pngError;
                if (encodeToFile(composed, pngPath, ExportFormat::Png, 0, pngError)) {
                    setStatus(L"已保存 PNG 回退: " + pngPath);
                    return;
                }
                notify(L"PNG 回退也失败: " + pngError);
                return;
            }
            return;
        }
        notify(L"保存失败: " + error);
    }

    void copyFlow(bool closeAfter) {
        PixelBuffer composed = composited();
        if (!composed.valid()) {
            notify(L"合成图像失败");
            return;
        }
        if (copyBitmapToClipboard(hwnd, composed)) {
            setStatus(closeAfter ? L"已复制并关闭" : L"已复制到剪贴板");
            if (closeAfter) ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
        } else {
            notify(L"复制到剪贴板失败");
        }
    }

    // ---- message handling -------------------------------------------------
    LRESULT handle(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_CREATE: {
                buildToolbar();
                syncHistoryButtons();
                ::SetFocus(h);
                return 0;
            }
            case WM_SIZE: {
                if (rt) {
                    const UINT w = LOWORD(lp), hgt = HIWORD(lp);
                    if (w > 0 && hgt > 0) {
                        rt->Resize(D2D1_SIZE_U{w, hgt});
                    }
                }
                ::InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC dc = ::BeginPaint(h, &ps);
                (void)dc;
                render();
                ::EndPaint(h, &ps);
                return 0;
            }
            case WM_PRINTCLIENT:
                render();
                return 0;
            case WM_COMMAND: {
                const int id = LOWORD(wp);
                const int code = HIWORD(wp);
                if (code != BN_CLICKED) break;
                if (id == static_cast<int>(ToolButton::Rectangle)) setTool(AnnotationTool::Rectangle);
                else if (id == static_cast<int>(ToolButton::Text)) setTool(AnnotationTool::Text);
                else if (id == static_cast<int>(ToolButton::Arrow)) setTool(AnnotationTool::Arrow);
                else if (id == static_cast<int>(ToolButton::Undo)) {
                    document.undo();
                    selectedId.reset();
                    syncHistoryButtons();
                    ::InvalidateRect(h, nullptr, FALSE);
                } else if (id == static_cast<int>(ToolButton::Redo)) {
                    document.redo();
                    selectedId.reset();
                    syncHistoryButtons();
                    ::InvalidateRect(h, nullptr, FALSE);
                } else if (id == static_cast<int>(ToolButton::Save)) saveFlow();
                else if (id == static_cast<int>(ToolButton::Copy)) copyFlow(false);
                else if (id == static_cast<int>(ToolButton::Close)) ::PostMessageW(h, WM_CLOSE, 0, 0);
                if (id <= static_cast<int>(ToolButton::Close)) {
                    ::SetFocus(h);  // keep FR-07 shortcuts on the preview window
                }
                return 0;
            }
            case WM_KEYDOWN: {
                const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
                const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (ctrl && wp == 'Z') {
                    if (shift) { document.redo(); } else { document.undo(); }
                    selectedId.reset();
                    syncHistoryButtons();
                    ::InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }
                if (ctrl && wp == 'Y') {
                    document.redo();
                    selectedId.reset();
                    syncHistoryButtons();
                    ::InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }
                if (ctrl && wp == 'S') { saveFlow(); return 0; }
                if (ctrl && wp == 'C') { copyFlow(true); return 0; }  // FR-07: copy + close
                if (wp == VK_RETURN) { saveFlow(); return 0; }
                if (wp == VK_ESCAPE) { ::PostMessageW(h, WM_CLOSE, 0, 0); return 0; }
                if (wp == VK_DELETE) {
                    if (selectedId && document.remove(*selectedId)) {
                        selectedId.reset();
                        syncHistoryButtons();
                        ::InvalidateRect(h, nullptr, FALSE);
                    }
                    return 0;
                }
                if (!ctrl) {
                    if (wp == 'R' || wp == '1') { setTool(AnnotationTool::Rectangle); return 0; }
                    if (wp == 'T' || wp == '4') { setTool(AnnotationTool::Text); return 0; }
                    if (wp == 'A' || wp == '3') { setTool(AnnotationTool::Arrow); return 0; }
                }
                break;
            }
            case WM_LBUTTONDOWN: {
                ::SetCapture(h);
                ::SetFocus(h);
                const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                const core::Point img = clientToImage(pt);
                if (activeTool == AnnotationTool::Rectangle ||
                    activeTool == AnnotationTool::Arrow) {
                    dragging = true;
                    dragStart = img;
                    dragCurrent = img;
                } else if (activeTool == AnnotationTool::Text) {
                    POINT screen = pt;
                    ::ClientToScreen(h, &screen);
                    TextInputResult input = showTextInput(
                        h, L"输入标注文字", POINT{screen.x + 16, screen.y + 16});
                    if (input.confirmed) {
                        tf::core::TextAnnotation t;
                        t.id = tf::core::Uuid::randomUuid();
                        t.position = img;
                        t.content = utf8Of(input.text);
                        t.style = tf::core::TextStyle{};  // 14px red default
                        document.add(Annotation::text(t));
                        syncHistoryButtons();
                    }
                } else {  // select / move
                    const Annotation* hit = document.hitTest(img);
                    if (hit) {
                        selectedId = hit->id();
                        moving = true;
                        moveLast = img;
                        document.beginBatch();  // one history entry per drag
                    } else {
                        selectedId.reset();
                    }
                }
                ::InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            case WM_MOUSEMOVE: {
                if (::GetCapture() != h) break;
                const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                const core::Point img = clientToImage(pt);
                if (dragging) {
                    dragCurrent = img;
                } else if (moving && selectedId) {
                    document.translateDirect(*selectedId,
                                             static_cast<double>(img.x - moveLast.x),
                                             static_cast<double>(img.y - moveLast.y));
                    moveLast = img;
                }
                ::InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            case WM_LBUTTONUP: {
                if (::GetCapture() == h) ::ReleaseCapture();
                const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                const core::Point img = clientToImage(pt);
                if (dragging) {
                    dragging = false;
                    if (activeTool == AnnotationTool::Rectangle) {
                        const core::Point end = (::GetKeyState(VK_SHIFT) < 0)
                                              ? shiftConstrain(dragStart, img, true)
                                              : img;
                        tf::core::RectangleAnnotation r;
                        r.id = tf::core::Uuid::randomUuid();
                        r.rect = normalizeRect(dragStart, end);
                        r.style = tf::core::StrokeStyle{};
                        if (r.isValid()) {
                            document.add(Annotation::rectangle(r));
                            syncHistoryButtons();
                        }
                    } else if (activeTool == AnnotationTool::Arrow) {
                        const core::Point end = (::GetKeyState(VK_SHIFT) < 0)
                                              ? shiftConstrain(dragStart, img, false)
                                              : img;
                        tf::core::ArrowAnnotation a;
                        a.id = tf::core::Uuid::randomUuid();
                        a.startPoint = dragStart;
                        a.endPoint = end;
                        a.style = tf::core::StrokeStyle{};
                        if (a.startPoint != a.endPoint) {
                            document.add(Annotation::arrow(a));
                            syncHistoryButtons();
                        }
                    }
                } else if (moving) {
                    moving = false;
                }
                ::InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            case WM_DESTROY: {
                releaseRenderResources();
                return 0;
            }
        }
        return ::DefWindowProcW(h, msg, wp, lp);
    }
};


// ------------------------------------------------- class external surface —

PreviewWindowController& PreviewWindowController::instance() {
    static PreviewWindowController controller;
    return controller;
}

bool PreviewWindowController::registerClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Impl::wndProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kPreviewWndClass;
    wc.hIcon = ::LoadIconW(instance, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    return ::RegisterClassExW(&wc) != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool PreviewWindowController::show(const PreviewWindowSpec& specIn) {
    if (!specIn.image || !specIn.image->valid()) return false;
    if (!impl_) impl_.reset(new Impl);
    Impl& impl = *impl_;

    impl.spec = specIn;
    impl.imageW = static_cast<FLOAT>(specIn.image->width);
    impl.imageH = static_cast<FLOAT>(specIn.image->height);
    impl.document = AnnotationDocument{};  // fresh edit session per capture
    impl.selectedId.reset();
    impl.baseBitmap.Reset();

    if (!impl.hwnd) {
        impl.instance = ::GetModuleHandleW(nullptr);
        if (!registerClass(impl.instance)) return false;
        // Initial size: fit the image into 75% of the work area (FR-07
        // remembers nothing yet — persistence lands with the settings pass).
        RECT work{};
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int availW = static_cast<int>(work.right - work.left);
        const int availH = static_cast<int>(work.bottom - work.top);
        const double fit = std::min({availW * 0.75 / impl.imageW,
                                     availH * 0.75 / impl.imageH, 1.0});
        int winW = std::max(static_cast<int>(impl.imageW * fit) + 16, kMinWindowW);
        int winH = std::max(static_cast<int>(impl.imageH * fit) + kToolbarHeight + 48,
                            kMinWindowH);
        impl.hwnd = ::CreateWindowExW(
            0, kPreviewWndClass,
            (L"预览" + specIn.titleSuffix).c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, winW, winH,
            nullptr, nullptr, impl.instance, &impl);
        if (!impl.hwnd) return false;
    } else {
        ::SetWindowTextW(impl.hwnd, (L"预览" + specIn.titleSuffix).c_str());
    }

    impl.setTool(AnnotationTool::Rectangle);
    impl.syncHistoryButtons();
    ::ShowWindow(impl.hwnd, SW_SHOW);
    ::SetForegroundWindow(impl.hwnd);
    ::InvalidateRect(impl.hwnd, nullptr, FALSE);
    return true;
}

void PreviewWindowController::close() {
    if (impl_ && impl_->hwnd) ::PostMessageW(impl_->hwnd, WM_CLOSE, 0, 0);
}

bool PreviewWindowController::isVisible() const {
    return impl_ && impl_->hwnd && ::IsWindowVisible(impl_->hwnd);
}

PreviewWindowController::~PreviewWindowController() {
    if (impl_ && impl_->hwnd) ::DestroyWindow(impl_->hwnd);
}

}  // namespace tf
