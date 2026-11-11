// PlatformFeatures.cpp — OS feature policy probes (see header).
//
// Windows TU: version via GetVersionExW under the 10.0.19041.0 baseline.
// All other platforms (GCC portable-check leg): the overrides stay the
// only source of truth so Catch2 policy tests remain meaningful.
#include "tf/PlatformFeatures.hpp"

// GetVersionExW is deprecated in name only; under the 10.0.19041.0 baseline
// it is the sanctioned way to read the real OS build (RtlGetVersion needs
// the DDK header and VerifyVersionInfo is worse). Suppression is scoped to
// this TU (MSVC only) — the policy layer is exactly where version truth
// belongs.
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace tf {
namespace {
std::uint32_t g_overrideBuild = 0;
std::uint32_t queryBuild() {
#ifdef _WIN32
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    // Declared at _WIN32_WINNT_WIN10_TH2; with the 19041 baseline header
    // this resolves to the real marketing build (22000+ on Win11).
    if (::GetVersionExW(&info) != 0) return info.dwBuildNumber;
#endif
    return 0;  // unknown → all probes false; callers keep their fallbacks
}
} // namespace

void setPlatformBuildOverrideForTests(std::uint32_t build) { g_overrideBuild = build; }
void clearPlatformBuildOverrideForTests() { g_overrideBuild = 0; }

std::uint32_t platformBuildNumber() {
    return g_overrideBuild != 0 ? g_overrideBuild : queryBuild();
}

} // namespace tf
