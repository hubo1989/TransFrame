// WGCCaptureEngine.cpp — Windows.Graphics.Capture engine (preferred).
//
// Strategy: one GraphicsCaptureItem per monitor via
// Direct3D11CaptureFramePool (buffered, 2 buffers). Items are created lazily
// per display and cached; a capture is: activate the item, grab one frame,
// copy GPU -> staging texture once (PRD: <= 50ms including that copy), map,
// swizzle into the Core PixelBuffer (BGRA stays BGRA; WGC premultiplies
// alpha, screens are opaque so alpha=255 after premultiply of 255).
//
// IsBorderRequired (contract 12) is optional; product policy enables it on Win11.
// Below that the property is absent -> keep default (yellow border shows,
// documented degradation, PRD §6.13).
#include "pch.h"

#include "tf/CaptureService.hpp"

#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Foundation.Metadata.h>

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
    // The free-threaded pool exists on Win10 1809 (contract 7). Borderless
    // capture is optional and must not disable WGC on Win10 (PRD §6.13).
    try {
        return winrt::Windows::Foundation::Metadata::ApiInformation::IsMethodPresent(
                   L"Windows.Graphics.Capture.Direct3D11CaptureFramePool", L"CreateFreeThreaded") &&
               Capture::GraphicsCaptureSession::IsSupported();
    } catch (...) {
        return false;
    }
}

} // namespace tf
