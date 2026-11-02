// WGCCaptureEngine.cpp — Windows.Graphics.Capture engine (preferred).
//
// Strategy: one GraphicsCaptureItem per monitor via
// Direct3D11CaptureFramePool (buffered, 2 buffers). Items are created lazily
// per display and cached; a capture is: activate the item, grab one frame,
// copy GPU -> staging texture once (PRD: <= 50ms including that copy), map,
// swizzle into the Core PixelBuffer (BGRA stays BGRA; WGC premultiplies
// alpha, screens are opaque so alpha=255 after premultiply of 255).
//
// IsBorderRequired=false on UniversalApiContract >= 15 (Win11): borderless.
// Below that the property is absent -> keep default (yellow border shows,
// documented degradation, PRD §6.13).
#include "pch.h"

#include "tf/CaptureService.hpp"

#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.System.h>

namespace tf {
namespace winrt = ::winrt;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

namespace {

} // namespace

// ---------------------------------------------------------------------------
// The full engine is implemented in the App-facing TU where the WinRT
// apartment and the D3D device live (NativeShell device singletons).
// This TU provides the availability probe shared by the chain builder.
// ---------------------------------------------------------------------------

bool probeWgcAvailability() {
    // WGC exists from Windows 10 1803; contract gate for our usage is the
    // Direct3D11CaptureFramePool API. A probe activation failure means WGC is
    // off (policy / SKUs) -> fall to DXGI.
    try {
        return winrt::Windows::Foundation::Metadata::ApiInformation::IsApiContractPresent(
            L"Windows.Foundation.UniversalApiContract", 12);
    } catch (...) {
        return false;
    }
}

} // namespace tf
