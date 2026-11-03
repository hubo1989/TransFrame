// src/pch.h — precompiled header for NativeShell Windows TU set.
// Includes the Win32/WinRT baseline shared by every implementation file.
// Pure-model TUs (DisplayMonitor policy, WindowDetector policy, HotkeyManager
// policy, OverlayRenderer) do NOT include this header; they stay portable so
// Catch2 can run them on any OS.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>     // Shell_NotifyIcon
#include <dwmapi.h>       // DwmGetWindowAttribute
#include <shellscalingapi.h>  // GetDpiForMonitor (per-monitor V2)
#include <dxgi1_2.h>     // desktop duplication
#include <d2d1_1.h>      // D2D 1.1 (command lists, effects)
#include <d2d1_1helper.h>
#include <dcomp.h>       // DirectComposition
#include <d3d11.h>
#include <wrl/client.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d3d11.lib")
