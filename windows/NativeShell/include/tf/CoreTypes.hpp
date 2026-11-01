// tf/CoreTypes.hpp
// Minimal Core-model surface this task depends on (PRD §4 interface contract).
// Task A owns windows/Core; the real Screenshot/DisplayInfo land there.
// This header declares only what NativeShell needs, self-contained, so B does
// not block on A. Fields mirror macOS Models/Screenshot.swift + DisplayInfo.swift.
// On MSVC these resolve via transitive <windows.h> includes (see src/pch.h);
// the header itself stays platform-clean for Catch2 tests.
#ifndef TF_CORE_TYPES_HPP
#define TF_CORE_TYPES_HPP

#include "Geo.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tf {

/// Raw BGRA8 bitmap in physical pixels. Ownership shared so the same pixels can
/// flow into OCR, preview and history without extra copies.
struct PixelBuffer {
    std::int32_t width = 0;   // physical px
    std::int32_t height = 0;  // physical px
    std::int32_t stride = 0;  // bytes per row (>= width*4, D3D requires multiples of 256 on staging)
    std::uint32_t dpiX = 96;  // horizontal DPI of the source display
    std::uint32_t dpiY = 96;  // vertical DPI
    std::shared_ptr<std::uint8_t[]> data;  // BGRA byte order, row-major, top-down

    bool valid() const {
        return data && width > 0 && height > 0 && stride >= width * 4;
    }
    std::size_t byteSize() const { return static_cast<std::size_t>(stride) * height; }
};

enum class ExportFormat : std::uint8_t { Png, Jpeg, Heic };

enum class CaptureTrigger : std::uint8_t {
    HotkeyRegion,
    HotkeyFullScreen,
    HotkeyTranslate,
    TrayRegion,
    TrayFullScreen,
    WindowClick,    // FR-02: click on auto-detected window
    ManualRegion,   // FR-01: drag selection
};

/// Core DisplayInfo (subset used by NativeShell). Physical pixel frame;
/// scale = dpi / 96 per PRD §3.2 (Per-Monitor V2).
struct DisplayInfo {
    std::wstring id;          // device name from EnumDisplayMonitors (\\.\DISPLAY1)
    std::wstring name;        // user-visible name (GetMonitorInfo szDevice fallback)
    Rect framePhysical;      // virtual-desktop coordinates, physical pixels
    std::uint32_t dpiX = 96;
    std::uint32_t dpiY = 96;
    bool isPrimary = false;

    double scale() const { return dpiX / 96.0; }
};

/// Core Screenshot (subset). Physical pixels + DPI metadata per PRD §4.3.
struct Screenshot {
    std::uint64_t id = 0;                  // GUID bits, filled by Core
    PixelBuffer pixels;                    // BGRA8, physical px, top-down
    DisplayInfo sourceDisplay;             // display that contains the capture origin
    std::chrono::system_clock::time_point capturedAt{};
    CaptureTrigger trigger = CaptureTrigger::ManualRegion;

    std::int64_t width() const { return pixels.width; }
    std::int64_t height() const { return pixels.height; }
};

enum class HotkeyAction : std::uint8_t {
    FullScreen,      // Ctrl+Alt+F
    Region,          // Ctrl+Alt+A
    Translate,       // Ctrl+Alt+T
    TextSelection,   // Ctrl+Alt+D
    TranslateInsert, // Ctrl+Alt+I
};

inline constexpr int kHotkeyActionCount = 5;

enum class HotkeyEventKind : std::uint8_t { Pressed };

} // namespace tf

#endif // TF_CORE_TYPES_HPP
