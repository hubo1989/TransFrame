// GdiCaptureEngine.cpp — GDI BitBlt last-resort engine.
//
// Always available (any session type). BitBlt with SRCCOPY|CAPTUREBLT from
// the primary/screen DC of the target monitor. CAPTUREBLT includes layered
// windows. ~30-80ms for 4K — acceptable only as the final fallback.
#include "pch.h"

#include "tf/CaptureService.hpp"

namespace tf {

bool probeGdiAvailability() {
    HDC screen = ::GetDC(nullptr);
    if (!screen) return false;
    ::ReleaseDC(nullptr, screen);
    return true;
}

} // namespace tf
