// PlatformFeatures.cpp — OS feature policy probes (see header).
#include "tf/PlatformFeatures.hpp"
#include <bit>

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
    // GetVersionEx depends on the host EXE's compatibility manifest. Resolve
    // RtlGetVersion at runtime instead; missing export/failure keeps fallback.
    const auto module = ::GetModuleHandleW(L"ntdll.dll");
    if (!module) return 0;
    using GetVersion = LONG (WINAPI*)(OSVERSIONINFOW*);
    const auto getVersion = std::bit_cast<GetVersion>(
        ::GetProcAddress(module, "RtlGetVersion"));
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (getVersion && getVersion(&info) == 0) return info.dwBuildNumber;
#endif
    return 0;  // unknown → all probes false; callers keep their fallbacks
}
} // namespace

void setPlatformBuildOverrideForTests(std::uint32_t build) { g_overrideBuild = build; }
void clearPlatformBuildOverrideForTests() { g_overrideBuild = 0; }

std::uint32_t platformBuildNumber() {
    static const auto build = queryBuild();
    return g_overrideBuild != 0 ? g_overrideBuild : build;
}

} // namespace tf
