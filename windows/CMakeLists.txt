# Translation services (Task C) — Core unit tests, portable.
# Core is platform-clean (no Win32/WinRT); this project builds the Catch2
# test runner on any OS. NativeShell (Win32/WinRT TUs) is built only on
# Windows via the WIN32 gate below (compile + link into a smoke target).
cmake_minimum_required(VERSION 3.20)
project(TransFrameTranslation CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Core portable sources + Catch2 tests
# ---------------------------------------------------------------------------
file(GLOB CORE_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Core/src/*.cpp)
file(GLOB TEST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp)

add_executable(translation_tests
    ${CORE_SRC}
    ${TEST_SRC}
    Tests/vendor/catch_amalgamated.cpp
)
target_include_directories(translation_tests PRIVATE
    Core/include
    Core/vendor
    Tests
    Tests/vendor
)

if (MSVC)
    target_compile_options(translation_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    # Amalgamated Catch2 + /W4 is noisy; tests are not /WX.
    target_compile_options(translation_tests PRIVATE /wd4996 /wd26819 /wd5054)
else()
    target_compile_options(translation_tests PRIVATE -Wall -Wextra)
    # {status, body} brace-init against a 4-field struct with defaults is
    # intentional in tests; GCC's -Wmissing-field-initializers disagrees.
    target_compile_options(translation_tests PRIVATE -Wno-missing-field-initializers)
endif()

enable_testing()
add_test(NAME translation_tests COMMAND translation_tests)

# ---------------------------------------------------------------------------
# NativeShell (Win32/WinRT TUs) — Windows only. Compile + link a smoke
# binary to prove the sources build under MSVC; running needs a GUI
# session (see windows/README.md manual smoke).
# ---------------------------------------------------------------------------
if (WIN32)
    file(GLOB NS_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/NativeShell/src/*.cpp)
    # Smoke links Core sources too (per windows/README.md manual command).
    add_executable(nativeshell_smoke
        ${CORE_SRC}
        ${NS_SRC}
        Tests/manual/smoke_translation.cpp
    )
    target_include_directories(nativeshell_smoke PRIVATE
        Core/include
        Core/vendor
        NativeShell/include
    )
    if (MSVC)
        target_compile_options(nativeshell_smoke PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    target_link_libraries(nativeshell_smoke PRIVATE winhttp credui advapi32 user32 ole32)
endif()
