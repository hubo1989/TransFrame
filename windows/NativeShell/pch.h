// pch.h — precompiled header for the NativeShell (Win32) layer.
// Task B (capture shell) fills in the real headers; task A ships the
// skeleton so the solution builds end-to-end.
#ifndef TF_NATIVESHELL_PCH_H
#define TF_NATIVESHELL_PCH_H

// Common CRT/std.
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Win32 common (lean, no winsock/rpc headers pulled implicitly).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#endif  // TF_NATIVESHELL_PCH_H
