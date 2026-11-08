// ImageExporter.cpp — WIC encoding, HEIC probe, composited rendering and
// CF_DIB clipboard copy (see tf/ImageExporter.hpp). Win32-only.
//
// Encoding notes:
//   * PNG and JPEG are native WIC containers; JPEG quality rides the frame
//     property bag ("ImageQuality", VT_R4 0..1) written BEFORE the frame
//     initializes with that bag.
//   * HEIF/HEIC needs the OS HEIF image extension + HEVC codec; probing
//     enumerates the WIC encoder catalog for the HEIF container GUID. When
//     absent, callers fall back to PNG/JPEG with a visible notice (FR-12).
//   * Everything goes through 32bppBGRA; screenshots carry opaque alpha, so
//     the straight/premultiplied distinction is moot.
#include "pch.h"

#include "tf/ImageExporter.hpp"

#include "Annotation.h"
#include "Geometry.h"
#include "Styles.h"

#include <wincodec.h>
#include <d2d1helper.h>
#include <dwrite.h>

#include <algorithm>
#include <cwchar>
#include <memory>
#include <optional>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace tf {

namespace {

using Microsoft::WRL::ComPtr;

/// Defined in wincodec.h only on newer SDKs; keep our own constant so the
/// build does not depend on SDK vintage (values are immutable by contract).
const GUID kHeifContainerFormat = {
    0xe1e62521, 0x6787, 0x405b, {0xa3, 0x39, 0x50, 0x07, 0x15, 0xb5, 0x76, 0x3f}};

ComPtr<IWICImagingFactory> wicFactory() {
    ComPtr<IWICImagingFactory> f;
    ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                       IID_PPV_ARGS(&f));
    return f;
}

ComPtr<ID2D1Factory> d2dFactory() {
    ComPtr<ID2D1Factory> f;
    ::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&f));
    return f;
}

D2D1_COLOR_F toColorF(const tf::core::CodableColor& c) {
    return D2D1_COLOR_F{static_cast<FLOAT>(c.red), static_cast<FLOAT>(c.green),
                        static_cast<FLOAT>(c.blue), static_cast<FLOAT>(c.opacity)};
}

D2D1_POINT_2F toPoint2F(const tf::core::Point& p) {
    return D2D1_POINT_2F{static_cast<FLOAT>(p.x), static_cast<FLOAT>(p.y)};
}

D2D1_RECT_F toRectF(const tf::core::Rect& r) {
    return D2D1_RECT_F{static_cast<FLOAT>(r.minX()), static_cast<FLOAT>(r.minY()),
                       static_cast<FLOAT>(r.maxX()), static_cast<FLOAT>(r.maxY())};
}

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                          static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                          wide.data(), len);
    return wide;
}

/// Solid arrowhead wings at the end point, sized by line width.
void drawArrowHead(ID2D1RenderTarget& rt, const D2D1_POINT_2F& tail,
                   const D2D1_POINT_2F& head, FLOAT lineWidth,
                   const D2D1_COLOR_F& color) {
    const double angle = std::atan2(head.y - tail.y, head.x - tail.x);
    const double wing = std::max<FLOAT>(lineWidth * 4.0f, 10.0f);
    const double spread = 0.45;  // ~26° each side
    const D2D1_POINT_2F left{static_cast<FLOAT>(head.x - wing * std::cos(angle - spread)),
                             static_cast<FLOAT>(head.y - wing * std::sin(angle - spread))};
    const D2D1_POINT_2F right{static_cast<FLOAT>(head.x - wing * std::cos(angle + spread)),
                              static_cast<FLOAT>(head.y - wing * std::sin(angle + spread))};
    ComPtr<ID2D1Factory> factory;
    rt.GetFactory(&factory);
    if (!factory) return;
    ComPtr<ID2D1PathGeometry> path;
    if (FAILED(factory->CreatePathGeometry(&path))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(&sink))) return;
    sink->BeginFigure(head, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(left);
    sink->AddLine(right);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt.CreateSolidColorBrush(color, &brush))) return;
    rt.FillGeometry(path.Get(), brush.Get());
}

/// Text rendering shared by TextAnnotation and number labels. Anchors at the
/// payload position (top-left), wraps at wrapWidth when set.
void drawUtf16Text(ID2D1RenderTarget& rt, IDWriteFactory& dwrite,
                   const std::wstring& text, const tf::core::Point& position,
                   double fontSize, const D2D1_COLOR_F& color,
                   std::optional<double> wrapWidth, bool centeredAroundAnchor) {
    if (text.empty()) return;
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(dwrite.CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       static_cast<FLOAT>(fontSize), L"", &format))) {
        return;
    }
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt.CreateSolidColorBrush(color, &brush))) return;
    const FLOAT boxWidth = centeredAroundAnchor
        ? static_cast<FLOAT>(fontSize * 0.62 * text.size())
        : (wrapWidth && *wrapWidth > 0 ? static_cast<FLOAT>(*wrapWidth) : 100000.0f);
    if (centeredAroundAnchor) {
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    ComPtr<IDWriteTextLayout> layout;
    const FLOAT originX = centeredAroundAnchor
        ? static_cast<FLOAT>(position.x - (fontSize * 0.31 * text.size()))
        : static_cast<FLOAT>(position.x);
    const FLOAT originY = centeredAroundAnchor
        ? static_cast<FLOAT>(position.y - fontSize * 0.7)
        : static_cast<FLOAT>(position.y);
    if (FAILED(dwrite.CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                       format.Get(), boxWidth, 100000.0f, &layout))) {
        return;
    }
    rt.DrawTextLayout(D2D1_POINT_2F{originX, originY}, layout.Get(), brush.Get());
}

void drawTextAnnotation(ID2D1RenderTarget& rt, IDWriteFactory& dwrite,
                        const tf::core::TextAnnotation& t) {
    drawUtf16Text(rt, dwrite, utf8ToWide(t.content), t.position, t.style.fontSize,
                  toColorF(t.style.color), t.wrapWidth, /*centered*/ false);
}

void drawOneAnnotation(ID2D1RenderTarget& rt, IDWriteFactory& dwrite,
                       const tf::core::Annotation& a) {
    const D2D1_ANTIALIAS_MODE savedAA = rt.GetAntialiasMode();
    rt.SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ComPtr<ID2D1SolidColorBrush> brush;

    std::visit(
        [&](const auto& payload) {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, tf::core::RectangleAnnotation> ||
                          std::is_same_v<T, tf::core::EllipseAnnotation>) {
                const FLOAT lineWidth = static_cast<FLOAT>(payload.style.lineWidth);
                const bool fill = payload.isFilled;
                rt.CreateSolidColorBrush(toColorF(payload.style.color), &brush);
                if (!brush) return;
                if (fill) {
                    rt.CreateSolidColorBrush(toColorF(payload.effectiveFillColor()), &brush);
                    if (!brush) return;
                }
                if constexpr (std::is_same_v<T, tf::core::EllipseAnnotation>) {
                    const D2D1_RECT_F r = toRectF(payload.rect);
                    const D2D1_ELLIPSE ellipse{
                        D2D1_POINT_2F{(r.left + r.right) / 2, (r.top + r.bottom) / 2},
                        (r.right - r.left) / 2, (r.bottom - r.top) / 2};
                    if (fill) { rt.FillEllipse(ellipse, brush.Get()); }
                    rt.DrawEllipse(ellipse, brush.Get(), lineWidth);
                } else {
                    if (fill) { rt.FillRectangle(toRectF(payload.rect), brush.Get()); }
                    rt.DrawRectangle(toRectF(payload.rect), brush.Get(), lineWidth);
                }
            } else if constexpr (std::is_same_v<T, tf::core::HighlightAnnotation>) {
                D2D1_COLOR_F c = toColorF(payload.color);
                c.a = static_cast<FLOAT>(payload.opacity);
                rt.CreateSolidColorBrush(c, &brush);
                if (!brush) return;
                rt.FillRectangle(toRectF(payload.rect), brush.Get());
            } else if constexpr (std::is_same_v<T, tf::core::MosaicAnnotation> ||
                                 std::is_same_v<T, tf::core::PrivacyRedactionAnnotation>) {
                // Pixelated mosaic lands with the full annotation pass; the
                // minimal editor cannot create either type — render a solid
                // block so exports stay meaningful.
                D2D1_COLOR_F c;
                if constexpr (std::is_same_v<T, tf::core::PrivacyRedactionAnnotation>) {
                    c = toColorF(payload.fillColor);
                } else {
                    c = D2D1_COLOR_F{0.3f, 0.3f, 0.3f, 0.85f};
                }
                rt.CreateSolidColorBrush(c, &brush);
                if (!brush) return;
                rt.FillRectangle(toRectF(payload.rect), brush.Get());
            } else if constexpr (std::is_same_v<T, tf::core::CalloutBubbleAnnotation>) {
                rt.CreateSolidColorBrush(toColorF(payload.style.color), &brush);
                if (!brush) return;
                // TextStyle carries no line width; bubble outlines are fixed.
                rt.DrawRectangle(toRectF(payload.rect), brush.Get(), 2.0f);
                rt.DrawLine(toPoint2F(payload.targetPoint),
                            D2D1_POINT_2F{static_cast<FLOAT>(payload.rect.midX()),
                                          static_cast<FLOAT>(payload.rect.midY())},
                            brush.Get(), 2.0f);
                drawUtf16Text(rt, dwrite, utf8ToWide(payload.text),
                              tf::core::Point{payload.rect.minX() + 4, payload.rect.minY() + 4},
                              payload.style.fontSize, toColorF(payload.style.color),
                              std::optional<double>(payload.rect.width - 8), false);
            } else if constexpr (std::is_same_v<T, tf::core::LineAnnotation> ||
                                 std::is_same_v<T, tf::core::ArrowAnnotation>) {
                const FLOAT w = static_cast<FLOAT>(payload.style.lineWidth);
                rt.CreateSolidColorBrush(toColorF(payload.style.color), &brush);
                if (!brush) return;
                const D2D1_POINT_2F start = toPoint2F(payload.startPoint);
                const D2D1_POINT_2F end = toPoint2F(payload.endPoint);
                if constexpr (std::is_same_v<T, tf::core::ArrowAnnotation>) {
                    if (payload.controlPoint) {
                        // Quadratic Bezier as a 16-segment polyline.
                        const D2D1_POINT_2F c = toPoint2F(*payload.controlPoint);
                        D2D1_POINT_2F prev = start;
                        for (int i = 1; i <= 16; ++i) {
                            const double t = i / 16.0;
                            const double mt = 1.0 - t;
                            const D2D1_POINT_2F next{
                                static_cast<FLOAT>(mt * mt * start.x + 2 * mt * t * c.x + t * t * end.x),
                                static_cast<FLOAT>(mt * mt * start.y + 2 * mt * t * c.y + t * t * end.y)};
                            rt.DrawLine(prev, next, brush.Get(), w);
                            prev = next;
                        }
                    } else {
                        rt.DrawLine(start, end, brush.Get(), w);
                    }
                    drawArrowHead(rt, start, end, w, toColorF(payload.style.color));
                } else if constexpr (std::is_same_v<T, tf::core::LineAnnotation>) {
                    rt.DrawLine(start, end, brush.Get(), w);
                }
            } else if constexpr (std::is_same_v<T, tf::core::FreehandAnnotation>) {
                const FLOAT w = static_cast<FLOAT>(payload.style.lineWidth);
                rt.CreateSolidColorBrush(toColorF(payload.style.color), &brush);
                if (!brush || payload.points.size() < 2) return;
                for (std::size_t i = 1; i < payload.points.size(); ++i) {
                    rt.DrawLine(toPoint2F(payload.points[i - 1]), toPoint2F(payload.points[i]),
                                brush.Get(), w);
                }
            } else if constexpr (std::is_same_v<T, tf::core::TextAnnotation>) {
                drawTextAnnotation(rt, dwrite, payload);
            } else if constexpr (std::is_same_v<T, tf::core::NumberLabelAnnotation>) {
                rt.CreateSolidColorBrush(toColorF(payload.color), &brush);
                if (!brush) return;
                rt.FillEllipse(D2D1_ELLIPSE{toPoint2F(payload.position),
                                            static_cast<FLOAT>(payload.size / 2),
                                            static_cast<FLOAT>(payload.size / 2)},
                               brush.Get());
                drawUtf16Text(rt, dwrite, std::to_wstring(payload.number),
                              payload.position, payload.size * 0.55,
                              D2D1_COLOR_F{1.f, 1.f, 1.f, 1.f}, std::nullopt,
                              /*centered*/ true);
            }
        },
        a.payload);
    rt.SetAntialiasMode(savedAA);
}

/// One shared DWrite factory per process (UI thread only; cheap to create).
IDWriteFactory* sharedDWriteFactory() {
    static IDWriteFactory* factory = nullptr;
    if (!factory) {
        ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                              reinterpret_cast<IUnknown**>(&factory));
    }
    return factory;
}

}  // namespace

void drawAnnotations(ID2D1RenderTarget& rt,
                     const std::vector<tf::core::Annotation>& annotations) {
    IDWriteFactory* dwrite = sharedDWriteFactory();
    if (!dwrite) return;
    for (const tf::core::Annotation& a : annotations) {
        drawOneAnnotation(rt, *dwrite, a);
    }
}

PixelBuffer renderComposited(const PixelBuffer& base,
                             const tf::core::AnnotationDocument& document) {
    PixelBuffer out;
    if (!base.valid()) return out;
    ComPtr<IWICImagingFactory> wic = wicFactory();
    ComPtr<ID2D1Factory> d2d = d2dFactory();
    if (!wic || !d2d) return out;

    ComPtr<IWICBitmap> wicBitmap;
    // D2D WIC render targets only accept PREMULTIPLIED (PBGRA) source
    // bitmaps — straight-alpha 32bppBGRA returns
    // D2DERR_UNSUPPORTED_PIXEL_FORMAT (0x88982F80). Screenshots are opaque,
    // so premultiplied == straight for the pixels we care about.
    if (FAILED(wic->CreateBitmap(static_cast<UINT>(base.width),
                                 static_cast<UINT>(base.height),
                                 GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                 &wicBitmap))) {
        return out;
    }
    const D2D1_PIXEL_FORMAT pixelFormat = D2D1::PixelFormat(
        DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);  // D2D's required pairing for 32bppBGRA; opaque screenshots unaffected
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, pixelFormat);
    ComPtr<ID2D1RenderTarget> rt;
    if (FAILED(d2d->CreateWicBitmapRenderTarget(wicBitmap.Get(), props, &rt))) return out;

    ComPtr<ID2D1Bitmap> baseBitmap;
    if (FAILED(rt->CreateBitmapFromWicBitmap(wicBitmap.Get(), &baseBitmap))) return out;
    rt->BeginDraw();
    rt->Clear(D2D1_COLOR_F{1, 1, 1, 1});
    rt->DrawBitmap(baseBitmap.Get(),
                   D2D1_RECT_F{0.f, 0.f, static_cast<FLOAT>(base.width),
                               static_cast<FLOAT>(base.height)},
                   1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    drawAnnotations(*rt.Get(), document.annotations());
    if (FAILED(rt->EndDraw())) {
        return out;
    }

    const std::int32_t stride = base.width * 4;
    out.width = base.width;
    out.height = base.height;
    out.stride = stride;
    out.dpiX = base.dpiX;
    out.dpiY = base.dpiY;
    out.data = std::shared_ptr<std::uint8_t[]>(
        new std::uint8_t[static_cast<std::size_t>(stride) * base.height]);
    WICRect rect{0, 0, base.width, base.height};
    if (FAILED(wicBitmap->CopyPixels(&rect, static_cast<UINT>(stride),
                                     static_cast<UINT>(out.byteSize()), out.data.get()))) {
        return PixelBuffer{};
    }
    return out;
}

PixelBuffer renderTranslationOverlay(const PixelBuffer& base,
                                     const std::vector<OverlaySegment>& segments,
                                     OverlayTextMode mode) {
    PixelBuffer out;
    if (!base.valid() || segments.empty()) return out;
    ComPtr<IWICImagingFactory> wic = wicFactory();
    ComPtr<ID2D1Factory> d2d = d2dFactory();
    IDWriteFactory* dwrite = sharedDWriteFactory();
    if (!wic || !d2d || !dwrite) return out;

    ComPtr<IWICBitmap> wicBitmap;
    // D2D WIC render targets only accept PREMULTIPLIED (PBGRA) source
    // bitmaps — straight-alpha 32bppBGRA returns
    // D2DERR_UNSUPPORTED_PIXEL_FORMAT (0x88982F80). Screenshots are opaque,
    // so premultiplied == straight for the pixels we care about.
    if (FAILED(wic->CreateBitmap(static_cast<UINT>(base.width),
                                 static_cast<UINT>(base.height),
                                 GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                 &wicBitmap))) {
        return out;
    }
    const D2D1_PIXEL_FORMAT pixelFormat = D2D1::PixelFormat(
        DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, pixelFormat);
    ComPtr<ID2D1RenderTarget> rt;
    if (FAILED(d2d->CreateWicBitmapRenderTarget(wicBitmap.Get(), props, &rt))) {
        return out;
    }

    // Base pixels uploaded directly as a D2D bitmap (no WIC round trip — the
    // render target already IS the WIC bitmap, so drawing a bitmap created
    // from that same surface would alias the target).
    const D2D1_BITMAP_PROPERTIES bmpProps = D2D1::BitmapProperties(pixelFormat);
    ComPtr<ID2D1Bitmap> baseBitmap;
    if (FAILED(rt->CreateBitmap(
            D2D1::SizeU(static_cast<UINT>(base.width), static_cast<UINT>(base.height)),
            base.data.get(), static_cast<UINT>(base.stride), bmpProps,
            &baseBitmap))) {
        return out;
    }

    // Shared trimming sign for ellipsized overflow (needs any text format).
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 1};
    ComPtr<IDWriteTextFormat> probeFormat;
    if (FAILED(dwrite->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 14.f, L"", probeFormat.GetAddressOf()))) {
        return out;
    }
    ComPtr<IDWriteInlineObject> ellipsis;
    dwrite->CreateEllipsisTrimmingSign(probeFormat.Get(), ellipsis.GetAddressOf());

    rt->BeginDraw();
    rt->Clear(D2D1_COLOR_F{0, 0, 0, 0});
    rt->DrawBitmap(baseBitmap.Get(),
                   D2D1::RectF(0.f, 0.f, static_cast<FLOAT>(base.width),
                               static_cast<FLOAT>(base.height)),
                   1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);

    for (const OverlaySegment& seg : segments) {
        // Portable decision pass (tested): colors, geometry, font fit.
        const OverlayDrawItem item =
            planOverlayItem(seg, mode, base.width, base.height, OverlayStyle{},
                            base.data.get(), base.stride);
        if (item.fillAlpha <= 0.f || item.fillRect.empty()) continue;

        const D2D1_RECT_F fill = D2D1::RectF(
            static_cast<FLOAT>(item.fillRect.x), static_cast<FLOAT>(item.fillRect.y),
            static_cast<FLOAT>(item.fillRect.right()),
            static_cast<FLOAT>(item.fillRect.bottom()));
        ComPtr<ID2D1SolidColorBrush> fillBrush;
        if (SUCCEEDED(rt->CreateSolidColorBrush(
                D2D1::ColorF(item.fillColor.r, item.fillColor.g, item.fillColor.b,
                             item.fillAlpha),
                fillBrush.GetAddressOf()))) {
            rt->FillRectangle(&fill, fillBrush.Get());
        }
        if (seg.text.empty()) continue;

        ComPtr<ID2D1SolidColorBrush> textBrush;
        if (FAILED(rt->CreateSolidColorBrush(
                D2D1::ColorF(item.textColor.r, item.textColor.g, item.textColor.b, 1.f),
                textBrush.GetAddressOf()))) {
            continue;
        }
        ComPtr<IDWriteTextFormat> format;
        if (FAILED(dwrite->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, item.fontPx, L"", format.GetAddressOf()))) {
            continue;
        }
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        format->SetReadingDirection(DWRITE_READING_DIRECTION_LEFT_TO_RIGHT);
        ComPtr<IDWriteTextLayout> layout;
        const std::wstring text = utf8ToWide(seg.text);
        if (FAILED(dwrite->CreateTextLayout(
                text.c_str(), static_cast<UINT32>(text.size()), format.Get(),
                static_cast<FLOAT>(item.fillRect.width) - 6.f,
                static_cast<FLOAT>(item.fillRect.height), layout.GetAddressOf()))) {
            continue;
        }
        if (item.ellipsize) layout->SetTrimming(&trimming, ellipsis.Get());
        // CLIP keeps glyph ascenders/descenders inside the band (the strategy
        // layer's geometry is the contract; nothing may spill over the base).
        rt->DrawTextLayout(
            D2D1_POINT_2F{static_cast<FLOAT>(item.fillRect.x) + 3.f,
                          static_cast<FLOAT>(item.fillRect.y)},
            layout.Get(), textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    if (FAILED(rt->EndDraw())) {
        return out;
    }

    const std::int32_t stride = base.width * 4;
    out.width = base.width;
    out.height = base.height;
    out.stride = stride;
    out.dpiX = base.dpiX;
    out.dpiY = base.dpiY;
    out.data = std::shared_ptr<std::uint8_t[]>(
        new std::uint8_t[static_cast<std::size_t>(stride) * base.height]);
    WICRect rect{0, 0, base.width, base.height};
    if (FAILED(wicBitmap->CopyPixels(&rect, static_cast<UINT>(stride),
                                     static_cast<UINT>(out.byteSize()), out.data.get()))) {
        return PixelBuffer{};
    }
    return out;
}

bool heicEncoderAvailable() {
    ComPtr<IWICImagingFactory> wic = wicFactory();
    if (!wic) return false;
    ComPtr<IEnumUnknown> enumerator;
    if (FAILED(wic->CreateComponentEnumerator(WICEncoder, WICComponentEnumerateDefault,
                                              &enumerator))) {
        return false;
    }
    ComPtr<IUnknown> unknown;
    while (enumerator->Next(1, &unknown, nullptr) == S_OK) {
        ComPtr<IWICBitmapEncoderInfo> info;
        if (SUCCEEDED(unknown.As(&info)) && info) {
            GUID container{};
            if (SUCCEEDED(info->GetContainerFormat(&container)) &&
                container == kHeifContainerFormat) {
                return true;
            }
        }
        unknown.Reset();
    }
    return false;
}

bool encodeToFile(const PixelBuffer& pixels, const std::wstring& path,
                  ExportFormat format, int qualityPercent, std::wstring& error) {
    error.clear();
    if (!pixels.valid()) {
        error = L"没有可保存的图像数据";
        return false;
    }
    const GUID* container = &GUID_ContainerFormatPng;
    if (format == ExportFormat::Jpeg) container = &GUID_ContainerFormatJpeg;
    else if (format == ExportFormat::Heic) container = &kHeifContainerFormat;

    ComPtr<IWICImagingFactory> wic = wicFactory();
    if (!wic) {
        error = L"WIC 初始化失败";
        return false;
    }
    ComPtr<IWICBitmapEncoder> encoder;
    HRESULT hr = wic->CreateEncoder(*container, nullptr, &encoder);
    if (FAILED(hr)) {
        if (format == ExportFormat::Heic) {
            error = L"系统缺少 HEIF 编码器（需安装 HEVC/HEIF 扩展），请改用 PNG 或 JPEG";
        } else {
            wchar_t buf[16] = {};
            swprintf(buf, 16, L"0x%08lX", static_cast<unsigned long>(hr));
            error = std::wstring(L"WIC 创建编码器失败 (") + buf + L")";
        }
        return false;
    }

    ComPtr<IWICStream> stream;
    if (FAILED(wic->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) {
        error = L"无法打开目标文件（路径无效或被占用）";
        return false;
    }
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
        error = L"WIC 编码器初始化失败";
        return false;
    }
    ComPtr<IPropertyBag2> bag;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(encoder->CreateNewFrame(&frame, &bag))) {
        error = L"WIC 创建编码帧失败";
        return false;
    }
    if (bag && (format == ExportFormat::Jpeg || format == ExportFormat::Heic)) {
        // "ImageQuality" must be in the bag BEFORE frame->Initialize(bag).
        PROPBAG2 option{};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        option.vt = VT_R4;
        VARIANT value{};
        value.vt = VT_R4;
        value.fltVal = std::clamp(qualityPercent, 0, 100) / 100.0f;
        ULONG errors = 0;
        bag->Write(1, &option, &value);  // best effort: default quality on failure
        (void)errors;
    }
    if (FAILED(frame->Initialize(bag.Get()))) {
        error = L"WIC 编码帧初始化失败";
        return false;
    }
    if (FAILED(frame->SetSize(static_cast<UINT>(pixels.width),
                              static_cast<UINT>(pixels.height)))) {
        error = L"WIC 设置尺寸失败";
        return false;
    }
    GUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&pixelFormat))) {
        error = L"WIC 设置像素格式失败";
        return false;
    }
    const UINT stride = static_cast<UINT>(pixels.stride);
    const UINT bufferBytes = static_cast<UINT>(pixels.byteSize());
    if (FAILED(frame->WritePixels(static_cast<UINT>(pixels.height), stride, bufferBytes,
                                  pixels.data.get()))) {
        error = L"WIC 写入像素失败";
        return false;
    }
    if (FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
        error = L"WIC 提交编码失败";
        return false;
    }
    return true;
}

bool copyBitmapToClipboard(HWND owner, const PixelBuffer& pixels) {
    if (!pixels.valid() || !::OpenClipboard(owner)) return false;
    bool ok = false;
    if (::EmptyClipboard()) {
        BITMAPINFOHEADER bi{};
        bi.biSize = sizeof(bi);
        bi.biWidth = pixels.width;
        bi.biHeight = -pixels.height;  // negative = top-down rows
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;
        bi.biSizeImage = static_cast<DWORD>(pixels.byteSize());
        const DWORD total = static_cast<DWORD>(sizeof(bi) + pixels.byteSize());
        if (HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, total)) {
            if (void* p = ::GlobalLock(mem)) {
                std::memcpy(p, &bi, sizeof(bi));
                std::memcpy(static_cast<std::uint8_t*>(p) + sizeof(bi), pixels.data.get(),
                            pixels.byteSize());
                ::GlobalUnlock(mem);
                if (::SetClipboardData(CF_DIB, mem)) {
                    ok = true;  // ownership passed to the clipboard
                } else {
                    ::GlobalFree(mem);
                }
            } else {
                ::GlobalFree(mem);
            }
        }
    }
    ::CloseClipboard();
    return ok;
}

}  // namespace tf
