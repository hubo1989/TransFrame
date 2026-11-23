// NativeShellSkeleton.cpp — task A placeholder translation unit so the
// NativeShell static library builds non-empty. Task B replaces this with the
// capture shell (Windows.Graphics.Capture + D2D overlay + UIA insertion).
#include "pch.h"

// Core interfaces this layer will implement (kept as a comment-level map:
// IOcrEngine, IHotkeyListener, IClipboard live in Core/Interfaces.h and are
// implemented here in task B).
namespace tf::native {
/// Marker so the archive has one symbol; replaced in task B.
extern const char kNativeShellVersion[] = "skeleton-taskA";
}  // namespace tf::native

const char* tf_native_shell_version() { return "skeleton-taskA"; }
