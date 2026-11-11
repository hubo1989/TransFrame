#pragma once
// tf/PlatformFeatures.hpp — policy-level OS feature probes (PRD §6.13).
//
// "Does this machine support feature F?" answered by OS build/version
// checks — never by the presence of an API symbol alone. Runtime
// call sites still keep their per-API guards (ApiInformation /
// try-catch); this module is the single testable decision point used by
// strategy code (e.g. "Mica backdrop or plain color on Win10").
//
// The build comes from RtlGetVersion, resolved at runtime independently of
// the host EXE's compatibility manifest. No Windows headers here, so the
// policy layer also builds on GCC (portable-check leg).
#ifndef TF_PLATFORM_FEATURES_HPP
#define TF_PLATFORM_FEATURES_HPP

#include <cstdint>

namespace tf {

/// Caches the real OS build number once; afterwards pure lookups.
/// Cached (not re-read per call) so behavior is stable within a process
/// and unit tests can inject a fake build without touching the OS.
void setPlatformBuildOverrideForTests(std::uint32_t build);
void clearPlatformBuildOverrideForTests();
std::uint32_t platformBuildNumber();

/// Windows 11 21H2 is build 22000.
inline bool isWindows11OrLater() { return platformBuildNumber() >= 22000; }

/// Mica needs Win11 21H2+. On Windows 10 SystemBackdrop ignores the
/// controller, so hosts must fall back to a plain color (PRD §6.13).
inline bool supportsMicaBackdrop() { return isWindows11OrLater(); }

/// Product policy for IsBorderRequired (SDK 20348, contract 12):
/// Win10 keeps the yellow capture border; Win11 may request borderless.
inline bool supportsBorderlessCapture() { return isWindows11OrLater(); }

} // namespace tf

#endif // TF_PLATFORM_FEATURES_HPP
