// tf/NativeShellInit.hpp — composition-root factories (App-facing surface of
// NativeShell). Implemented in src/NativeShellInit.cpp (Windows only).
#ifndef TF_NATIVE_SHELL_INIT_HPP
#define TF_NATIVE_SHELL_INIT_HPP

#ifdef _WIN32

#include "tf/CaptureService.hpp"
#include "tf/DisplayMonitor.hpp"
#include "tf/HotkeyManager.hpp"
#include "tf/OcrEngines.hpp"
#include "tf/WindowDetector.hpp"

#include <memory>

namespace tf {

/// WGC -> DXGI Desktop Duplication -> GDI capture chain (PRD §3.2). Engines
/// share one process-wide D3D11 device cache (src/CaptureD3dDevice.h).
std::unique_ptr<CaptureService> makeCaptureService();
std::unique_ptr<DisplayMonitorService> makeDisplayMonitorService();
std::unique_ptr<WindowDetectorService> makeWindowDetectorService(unsigned long excludePid);
std::unique_ptr<HotkeyManager> makeHotkeyManager();

/// Windows.Media.Ocr engine with the WinRT glue bound (Win32OcrUiaHosts.cpp).
std::unique_ptr<IOcrEngine> makeWindowsOcrEngine();

}  // namespace tf

#endif  // _WIN32
#endif  // TF_NATIVE_SHELL_INIT_HPP
