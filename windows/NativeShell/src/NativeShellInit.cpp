// NativeShellInit.cpp — composition root wiring the fallback chain.
#include "pch.h"

#include "tf/NativeShellInit.hpp"

#include "CaptureD3dDevice.h"
#include "tf/WindowDetector.hpp"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace tf {

// Forward declarations from engine TUs.
std::unique_ptr<ICaptureEngine> makeWgcCaptureEngine();
std::unique_ptr<ICaptureEngine> makeDxgiDuplicationEngine();
std::unique_ptr<ICaptureEngine> makeGdiCaptureEngine();

std::unique_ptr<IDisplayEnumerator> makeWin32DisplayEnumerator();
std::unique_ptr<IClock> makeSteadyClock();
std::unique_ptr<IWindowLister> makeWin32WindowLister(DWORD excludePid);
std::unique_ptr<IHotkeyRegistrar> makeWin32HotkeyRegistrar();

/// Build the capture service with the PRD §3.2 fallback chain:
/// WGC -> DXGI Desktop Duplication -> GDI BitBlt.
std::unique_ptr<CaptureService> makeCaptureService() {
    std::vector<std::unique_ptr<ICaptureEngine>> chain;
    chain.push_back(makeWgcCaptureEngine());
    chain.push_back(makeDxgiDuplicationEngine());
    chain.push_back(makeGdiCaptureEngine());
    return std::make_unique<CaptureService>(std::move(chain));
}

// ---------------------------------------------------------------------------
// Shared D3D11 device cache (CaptureD3dDevice.h). Engines call these from
// background threads; creation is serialized and devices live for the
// process (WGC frame pools and duplications hold their own references).
// ---------------------------------------------------------------------------
namespace {

using Microsoft::WRL::ComPtr;

std::mutex g_deviceMutex;
// Adapter LUID as a hashable scalar key (LUID itself has no operator==/hash).
std::unordered_map<std::uint64_t, ComPtr<ID3D11Device>> g_adapterDevices;
ComPtr<ID3D11Device> g_defaultDevice;  // default adapter

std::uint64_t luidKey(const LUID& luid) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(luid.HighPart)) << 32) |
           static_cast<std::uint32_t>(luid.LowPart);
}

ComPtr<ID3D11Device> createDeviceLocked(IDXGIAdapter1* adapter) {
    // BGRA support is required (both engines consume B8G8R8A8). An explicit
    // adapter pins D3D_DRIVER_TYPE_UNKNOWN (DXGI rule); the default path
    // prefers hardware and falls back to WARP so basic-render sessions still
    // capture through the duplication engine.
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    constexpr UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    ComPtr<ID3D11Device> device;
    if (adapter) {
        ::D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels,
                            ARRAYSIZE(levels), D3D11_SDK_VERSION, device.GetAddressOf(),
                            nullptr, nullptr);
    } else if (FAILED(::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                          flags, levels, ARRAYSIZE(levels),
                                          D3D11_SDK_VERSION, device.GetAddressOf(),
                                          nullptr, nullptr))) {
        ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
                            ARRAYSIZE(levels), D3D11_SDK_VERSION, device.GetAddressOf(),
                            nullptr, nullptr);
    }
    return device;
}

}  // namespace

ComPtr<ID3D11Device> sharedD3d11Device() {
    std::lock_guard<std::mutex> lock(g_deviceMutex);
    if (!g_defaultDevice) g_defaultDevice = createDeviceLocked(nullptr);
    return g_defaultDevice;
}

ComPtr<ID3D11Device> d3d11DeviceForAdapter(IDXGIAdapter1* adapter) {
    if (!adapter) return sharedD3d11Device();
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc))) return nullptr;

    std::lock_guard<std::mutex> lock(g_deviceMutex);
    auto it = g_adapterDevices.find(luidKey(desc.AdapterLuid));
    if (it != g_adapterDevices.end()) return it->second;

    // A hybrid system may drive the primary output from the default adapter;
    // reuse the default device when the LUID matches instead of creating a
    // second device on the same adapter.
    if (g_defaultDevice) {
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> defaultAdapter;
        DXGI_ADAPTER_DESC defaultDesc{};
        if (SUCCEEDED(g_defaultDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) &&
            SUCCEEDED(dxgiDevice->GetAdapter(defaultAdapter.GetAddressOf())) &&
            SUCCEEDED(defaultAdapter->GetDesc(&defaultDesc)) &&
            luidKey(defaultDesc.AdapterLuid) == luidKey(desc.AdapterLuid)) {
            return g_defaultDevice;
        }
    }

    ComPtr<ID3D11Device> device = createDeviceLocked(adapter);
    if (!device) return nullptr;
    g_adapterDevices.emplace(luidKey(desc.AdapterLuid), device);
    return device;
}

std::unique_ptr<DisplayMonitorService> makeDisplayMonitorService() {
    static auto enumerator = makeWin32DisplayEnumerator();
    static auto clock = makeSteadyClock();
    return std::make_unique<DisplayMonitorService>(*enumerator, *clock);
}

std::unique_ptr<WindowDetectorService> makeWindowDetectorService(DWORD excludePid) {
    static auto lister = makeWin32WindowLister(excludePid);
    return std::make_unique<WindowDetectorService>(*lister);
}

std::unique_ptr<HotkeyManager> makeHotkeyManager() {
    static auto registrar = makeWin32HotkeyRegistrar();
    return std::make_unique<HotkeyManager>(*registrar);
}

// ---------------------------------------------------------------------------
// Windows OCR seams: implemented in Win32OcrUiaHosts.cpp (WinRT glue).
// ---------------------------------------------------------------------------
WindowsOcrEngine::LanguageProvider MakeWinRtLanguageProvider();
WindowsOcrEngine::Recognizer MakeWinRtRecognizer();

std::unique_ptr<IOcrEngine> makeWindowsOcrEngine() {
    return std::make_unique<WindowsOcrEngine>(MakeWinRtLanguageProvider(),
                                              MakeWinRtRecognizer());
}

}  // namespace tf
