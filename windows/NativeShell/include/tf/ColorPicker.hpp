// tf/ColorPicker.hpp — FR-32 screen eyedropper (magnifier lens picker).
//
// One pre-created WS_POPUP + WS_EX_LAYERED + WS_EX_TOPMOST window spanning
// the virtual desktop (same D2D1.1 + DComp stack as SelectionOverlay).
// beginSession snapshots the desktop into a BGRA cache BEFORE the overlay
// shows — a DComp layered window reads back black from the GDI screen DC on
// Win11 DWM, so live GetPixel/BitBlt sampling would only see the overlay
// itself. Mouse move reads a 25x25 cache region, renders it 8x
// nearest-neighbor with a crosshair and a live HEX/RGB badge. Click samples
// the cache pixel under the cursor; Esc/right-click cancels. All
// coordinates are physical virtual-desktop pixels (Per-Monitor V2 process),
// so multi-monitor and mixed-DPI setups work unchanged.
//
// Pure helpers (colorPickHex / colorPickParseHex / colorPickRgbText) are
// header-inline and portable — Catch2 drives them directly.
#ifndef TF_COLOR_PICKER_HPP
#define TF_COLOR_PICKER_HPP

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace tf {

struct ColorPickResult {
    bool picked = false;   // false = cancelled (Esc / right-click / lost session)
    std::string hex;       // "#1A2B3C" (uppercase, 6 digits) when picked
    std::uint8_t r = 0, g = 0, b = 0;
    long screenX = 0, screenY = 0;  // physical px, virtual-desktop coords
};

using ColorPickCompletion = std::function<void(const ColorPickResult&)>;

/// "#RRGGBB", uppercase, 6 hex digits (spec §3: translatedText form).
inline std::string colorPickHex(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    static const char* digits = "0123456789ABCDEF";
    std::string out = "#000000";
    out[1] = digits[(r >> 4) & 0xF];
    out[2] = digits[r & 0xF];
    out[3] = digits[(g >> 4) & 0xF];
    out[4] = digits[g & 0xF];
    out[5] = digits[(b >> 4) & 0xF];
    out[6] = digits[b & 0xF];
    return out;
}

/// Parses "#1A2B3C" / "1a2b3c" (leading # optional, case-insensitive, exactly
/// 6 hex digits) into {R,G,B}; nullopt otherwise.
inline std::optional<std::array<std::uint8_t, 3>> colorPickParseHex(const std::string& hex) {
    std::size_t i = 0;
    if (hex.size() == 7 && hex[0] == '#') i = 1;
    if (hex.size() - i != 6) return std::nullopt;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int vals[6]{};
    for (int k = 0; k < 6; ++k) {
        const int v = nib(hex[i + static_cast<std::size_t>(k)]);
        if (v < 0) return std::nullopt;
        vals[k] = v;
    }
    return std::array<std::uint8_t, 3>{
        static_cast<std::uint8_t>(vals[0] * 16 + vals[1]),
        static_cast<std::uint8_t>(vals[2] * 16 + vals[3]),
        static_cast<std::uint8_t>(vals[4] * 16 + vals[5])};
}

/// "RGB(26, 43, 60)" — the secondary copy format (spec §4.3).
inline std::string colorPickRgbText(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return "RGB(" + std::to_string(r) + ", " + std::to_string(g) + ", " +
           std::to_string(b) + ")";
}

/// One screen-pixel sample in physical virtual-desktop coordinates.
/// Straight GetPixel on the screen DC — the simplest robust path (the WGC
/// single-frame engine stays reserved for the capture chain; a 1px probe
/// does not justify a frame-pool round trip).
bool colorPickSamplePixel(long x, long y, std::uint8_t* r, std::uint8_t* g,
                          std::uint8_t* b);

class ColorPickerController {
public:
    static ColorPickerController& instance();

    /// Register the window class (once per process).
    static bool registerClass(void* hinstance);

    /// Create the hidden window + D2D/DComp stack at app start (not on the
    /// tray-click hot path). Idempotent; false on device failure (tray item
    /// then reports "unavailable" instead of crashing).
    bool precreate();

    /// Show the lens and run a pick session. Returns false when a session is
    /// already active or the window was never created. The completion fires
    /// on the same (main) thread, after the lens is hidden.
    bool beginSession(ColorPickCompletion onDone);

    /// Cancel the active session (no callback).
    void cancelSession();

    bool isActive() const { return active_; }

    /// Hide the window and fire the completion (called by the wndProc on
    /// click/Esc; public so the smoke test can drive it via messages).
    void finishSession(const ColorPickResult& result);

    /// Smoke-test seam: the lens HWND (hidden or not).
    void* hwndForTest() const { return hwnd_; }

    /// Lens source size in screen px (displayed at 8x). Exposed for tests.
    static constexpr int kLensSourcePx = 25;
    static constexpr int kLensZoom = 8;

private:
    ColorPickerController() = default;
    struct Impl;
    Impl* impl_ = nullptr;   // lazy; owns the D2D stack
    void* hwnd_ = nullptr;
    bool active_ = false;
    ColorPickCompletion completion_;
};

}  // namespace tf

#endif  // TF_COLOR_PICKER_HPP
