// DxgiDuplicationEngine.cpp — DXGI Desktop Duplication fallback (engine 2).
//
// Used when WGC is unavailable (API contract missing, group policy, capture
// consent denied). One output duplication per monitor output; capture =
// AcquireNextFrame with a short timeout, copy the GPU texture into a staging
// texture, Map once, crop region into the Core PixelBuffer.
#include "pch.h"

#include "tf/CaptureService.hpp"

namespace tf {

bool probeDxgiDuplicationAvailability() {
    // Desktop Duplication requires D3D11 + a duplicatable output; the cheap
    // probe is DXGI factory creation. Full output enumeration happens in the
    // engine itself; failure there demotes to GDI.
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(::CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                    reinterpret_cast<void**>(factory.GetAddressOf())))) {
        return false;
    }
    return true;
}

} // namespace tf
