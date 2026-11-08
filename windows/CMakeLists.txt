# Translation services (Task C) — Core unit tests, portable.
# Core is platform-clean (no Win32/WinRT); this project builds the Catch2
# test runner on any OS. NativeShell (Win32/WinRT TUs) is built only on
# Windows via the WIN32 gate below (compile + link into a smoke target).
cmake_minimum_required(VERSION 3.20)
project(TransFrameTranslation C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Core portable sources + Catch2 tests
# ---------------------------------------------------------------------------
file(GLOB CORE_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Core/src/*.cpp)
# Model-layer sources live in Core/ root (mirrors Core.vcxproj; the src/
# glob above misses them).
set(CORE_MODEL_SRC
    Core/AnnotationDocument.cpp
)
set(TEST_SRC
    Tests/TranslationSchedulerTests.cpp
    Tests/ProvidersTests.cpp
    Tests/LLMTranslationCoreTests.cpp
    Tests/EngineHealthCheckerTests.cpp
    Tests/OcrAndTextSelectionTests.cpp
    Tests/EngineBootstrapTests.cpp
    Tests/AnnotationDocumentTests.cpp
    Tests/EngineConfigStoreTests.cpp
    Tests/HotkeySettingsStoreTests.cpp
    Tests/HistoryStoreTests.cpp
    Tests/ImageUploadProviderTests.cpp
)

# Vendored SQLite amalgamation for HistoryStore (FR-30): compile as C and
# silence its warnings (not /W4-clean; tests are not /WX for Catch2 only).
set(SQLITE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/Core/vendor/sqlite/sqlite3.c)
if (MSVC)
    set_source_files_properties(${SQLITE_SRC} PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(${SQLITE_SRC} PROPERTIES COMPILE_OPTIONS "-w")
endif()

add_executable(translation_tests
    ${CORE_SRC}
    ${CORE_MODEL_SRC}
    # Engine bootstrap lives under App/ but is platform-clean (no Win32), so
    # the portable test runner builds it too (PRD FR-16..24 wiring tests).
    App/EngineBootstrap.cpp
    ${TEST_SRC}
    ${SQLITE_SRC}
    Tests/vendor/catch_amalgamated.cpp
)
target_include_directories(translation_tests PRIVATE
    Core
    Core/include
    Core/vendor
    App
    external
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
    set(NS_SRC
        NativeShell/src/WinHttpTransport.cpp
        NativeShell/src/CredManKeyStore.cpp
        NativeShell/src/OcrUiaHostsWin32.cpp
    )
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
