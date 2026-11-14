// src/CaptureD3dDevice.h — shared D3D11 plumbing for the capture engines.
//
// One process-wide device cache (implemented in NativeShellInit.cpp): WGC
// frame pools ride the default-adapter device, while Desktop Duplication
// must use a device created on the adapter that owns the duplicated output.
// The cache is keyed by adapter LUID so both engines share devices instead
// of each spinning up (and leaking) their own.
//
// The readback helpers implement the PRD §8 "GPU->CPU one copy" model: each
// engine keeps a per-monitor STAGING texture as its current-desktop cache.
// A content refresh is one CopyResource into that staging texture; a capture
// Maps it and row-crops into a tight BGRA PixelBuffer (staging RowPitch is
// 256-aligned; the Core buffer is packed). Static-desktop captures skip the
// GPU copy entirely — the cache already holds the current desktop.
//
// Internal to NativeShell's Windows TU set; not part of include/tf.
#ifndef TF_CAPTURE_D3D_DEVICE_H
#define TF_CAPTURE_D3D_DEVICE_H

#include "tf/CaptureService.hpp"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace tf {

/// Default-adapter device (WGC path). Empty ComPtr on failure — callers fail
/// and the capture chain demotes to DXGI/GDI.
Microsoft::WRL::ComPtr<ID3D11Device> sharedD3d11Device();

/// Device bound to `adapter` (Desktop Duplication path); nullptr selects the
/// default adapter. Empty ComPtr on failure.
Microsoft::WRL::ComPtr<ID3D11Device> d3d11DeviceForAdapter(IDXGIAdapter1* adapter);

/// Copy `source` into `staging`, (re)creating the staging texture when it is
/// empty or its size no longer matches (mode change). One GPU->CPU-path
/// copy; the result is consumed by cropFromStaging. False on failure.
inline bool copyToStagingCache(ID3D11Device* device, ID3D11DeviceContext* context,
                               ID3D11Texture2D* source,
                               Microsoft::WRL::ComPtr<ID3D11Texture2D>& staging) {
    if (!device || !context || !source) return false;
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    if (staging) {
        D3D11_TEXTURE2D_DESC stagingDesc{};
        staging->GetDesc(&stagingDesc);
        if (stagingDesc.Width != desc.Width || stagingDesc.Height != desc.Height ||
            stagingDesc.Format != desc.Format) {
            staging.Reset();
        }
    }
    if (!staging) {
        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &staging))) return false;
    }
    context->CopyResource(staging.Get(), source);
    return true;
}

/// Map a staging cache and copy `crop` (texture-local px, clamped to the
/// texture bounds) into a tightly packed BGRA8 PixelBuffer. Invalid buffer
/// on Map failure or empty intersection.
inline PixelBuffer cropFromStaging(ID3D11DeviceContext* context,
                                   ID3D11Texture2D* staging, const Rect& crop) {
    PixelBuffer out;
    if (!context || !staging) return out;
    D3D11_TEXTURE2D_DESC desc{};
    staging->GetDesc(&desc);
    if (desc.Width == 0 || desc.Height == 0) return out;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) return out;
    std::int32_t stride = 0;
    out.data = copyBgraCrop(static_cast<const std::uint8_t*>(mapped.pData),
                            static_cast<std::int32_t>(mapped.RowPitch),
                            static_cast<std::int32_t>(desc.Width),
                            static_cast<std::int32_t>(desc.Height),
                            crop, stride);
    context->Unmap(staging, 0);
    if (!out.data) return out;

    // Width/height come from the clamped rect actually copied.
    const Rect clipped = crop.intersection(Rect{0, 0, desc.Width, desc.Height});
    out.width = static_cast<std::int32_t>(clipped.width);
    out.height = static_cast<std::int32_t>(clipped.height);
    out.stride = stride;
    return out;
}

}  // namespace tf

#endif  // TF_CAPTURE_D3D_DEVICE_H
