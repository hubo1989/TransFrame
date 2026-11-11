// PlatformFeaturesTests.cpp — PRD §6.13 policy probes (Catch2).
// Version boundaries: Win10 21H2 = build 19044, Win11 21H2 = 22000,
// Win11 22H2 = 22621.
#include "../include/tf/PlatformFeatures.hpp"

#include <catch_amalgamated.hpp>

namespace {
class BuildOverride {
public:
    explicit BuildOverride(std::uint32_t build) { tf::setPlatformBuildOverrideForTests(build); }
    ~BuildOverride() { tf::clearPlatformBuildOverrideForTests(); }
};
} // namespace

TEST_CASE("platformBuildNumber returns the injected override", "[platform]") {
    {
        BuildOverride guard{19044};
        CHECK(tf::platformBuildNumber() == 19044);
    }
    tf::clearPlatformBuildOverrideForTests();
}

TEST_CASE("Windows 10 21H2 (19044) reports no Win11-only features", "[platform]") {
    BuildOverride guard{19044};
    CHECK_FALSE(tf::isWindows11OrLater());
    CHECK_FALSE(tf::supportsMicaBackdrop());
    CHECK_FALSE(tf::supportsBorderlessCapture());
}

TEST_CASE("Windows 11 21H2 (22000) is the first build with Mica/borderless", "[platform]") {
    {
        BuildOverride guard{21999};
        CHECK_FALSE(tf::isWindows11OrLater());
        CHECK_FALSE(tf::supportsMicaBackdrop());
    }
    {
        BuildOverride guard{22000};
        CHECK(tf::isWindows11OrLater());
        CHECK(tf::supportsMicaBackdrop());
        CHECK(tf::supportsBorderlessCapture());
    }
}

TEST_CASE("Windows 11 22H2 (22621) keeps the Win11 feature set", "[platform]") {
    BuildOverride guard{22621};
    CHECK(tf::isWindows11OrLater());
    CHECK(tf::supportsMicaBackdrop());
    CHECK(tf::supportsBorderlessCapture());
}
