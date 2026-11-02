# NativeShell — Win32/D2D/WGC/热键/托盘 静态库（任务 B：捕获壳层）
# Portable policy core compiles everywhere; Windows-only TUs are gated.
cmake_minimum_required(VERSION 3.20)
project(TransFrameNativeShell CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT MSVC)
    # Keep the portable TUs warning-clean under clang/gcc too so the policy
    # code can be CI-checked on non-Windows runners.
    add_compile_options(-Wall -Wextra -Werror)
endif()

if (WIN32)
    # The task-A sln sets CharacterSet=Unicode; the CMake build must too, or
    # MAKEINTRESOURCE (IDC_CROSS / IDI_APPLICATION) expands to LPSTR and the
    # W-suffix Win32 APIs reject it.
    add_compile_definitions(UNICODE _UNICODE)
endif()

# ---------------------------------------------------------------------------
# Portable sources (policy core: no Win32/WinRT includes)
# ---------------------------------------------------------------------------
set(PORTABLE_SOURCES
    src/DisplayMonitor.cpp
    src/WindowDetector.cpp
    src/HotkeyManager.cpp
    src/CaptureService.cpp
    src/OverlayRenderer.cpp
    src/TrayMenu.cpp
)

set(PORTABLE_HEADERS
    include/tf/Geo.hpp
    include/tf/CoreTypes.hpp
    include/tf/DisplayMonitor.hpp
    include/tf/WindowDetector.hpp
    include/tf/CaptureService.hpp
    include/tf/HotkeyManager.hpp
    include/tf/OverlayRenderer.hpp
    include/tf/SelectionOverlay.hpp
    include/tf/TrayIcon.hpp
    include/tf/ResultWindow.hpp
)

# ---------------------------------------------------------------------------
# Windows-only sources (Win32 / WinRT / D2D / DComp)
# ---------------------------------------------------------------------------
if (WIN32)
    list(APPEND PORTABLE_SOURCES
        src/DisplayMonitorWin32.cpp
        src/WindowDetectorWin32.cpp
        src/HotkeyManagerWin32.cpp
        src/WGCCaptureEngine.cpp
        src/WGCCaptureEngineFull.cpp
        src/DxgiDuplicationEngine.cpp
        src/GdiCaptureEngine.cpp
        src/SelectionOverlay.cpp
        src/TrayIcon.cpp
        src/ResultWindow.cpp
        src/NativeShellInit.cpp
    )
endif()

add_library(native_shell STATIC ${PORTABLE_SOURCES})
target_include_directories(native_shell PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(native_shell PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(native_shell PRIVATE /W4 /WX /permissive-)
    if (NOT CMAKE_CXX_FLAGS MATCHES "/Zc:__cplusplus")
        target_compile_options(native_shell PRIVATE /Zc:__cplusplus)
    endif()
    target_precompile_headers(native_shell PRIVATE src/pch.h)
endif()

if (WIN32)
    # WinRT + Win32 linkage (task A sln adds the .vcxproj equivalents).
    list(APPEND PORTABLE_SOURCES
        src/WinHttpTransport.cpp
        src/CredManKeyStore.cpp
        src/Win32OcrUiaHosts.cpp
    )
    target_link_libraries(native_shell PUBLIC
        dwmapi shcore dxgi d2d1 dcomp d3d11 windowsapp winhttp advapi32)
endif()

# ---------------------------------------------------------------------------
# Tests (Catch2 v3 amalgamated — vendored, no package manager needed).
# Portable by design: the same suite runs on Windows CI and on dev machines.
# ---------------------------------------------------------------------------
enable_testing()

add_executable(native_shell_tests
    tests/TestMain.cpp
    tests/catch_amalgamated.cpp
    tests/GeoTests.cpp
    tests/DisplayMonitorTests.cpp
    tests/DpiScalingTests.cpp
    tests/WindowDetectorTests.cpp
    tests/HotkeyManagerTests.cpp
    tests/CaptureServiceTests.cpp
    tests/OverlayRendererTests.cpp
    tests/TrayMenuTests.cpp
)
target_include_directories(native_shell_tests PRIVATE tests)
target_link_libraries(native_shell_tests PRIVATE native_shell)
if (MSVC)
    target_compile_options(native_shell_tests PRIVATE /W4)
    # Amalgamated Catch2 + /W4 is noisy; tests are not /WX (policy TUs are).
    target_compile_options(native_shell_tests PRIVATE /wd4996 /wd26819)
endif()

add_test(NAME native_shell_tests COMMAND native_shell_tests)
