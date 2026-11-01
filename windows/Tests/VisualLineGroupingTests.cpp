// VisualLineGroupingTests.cpp — Catch2 port of
// TransFrameTests/VisualLineGroupingTests.swift (same vectors) + the fixture
// file windows/Tests/fixtures/visual-line-grouping.json (extracted from the
// same source).
#include <catch2/catch_amalgamated.hpp>

#include "Core/CaptureDocument.h"
#include "Core/VisualLineGrouping.h"

#include <fstream>
#include <sstream>

namespace {

tf::core::TextSegment seg(const char* text, double x, double y, double w, double h,
                          float confidence = 1.0f, bool edited = false) {
    tf::core::TextSegment s;
    s.id = tf::core::Uuid::randomUuid();
    s.text = text;
    s.boundingBox = tf::core::Rect{x, y, w, h};
    s.confidence = confidence;
    s.isUserEdited = edited;
    return s;
}

/// The screenshot regression fixture: one line split into 3 fragments
/// including the inline code token `main` (Swift lineFragments()).
std::vector<tf::core::TextSegment> lineFragments() {
    return {
        seg("installed metadata. Prefer a commit SHA in place of", 0.10, 0.40, 0.50, 0.02),
        seg("main", 0.61, 0.401, 0.04, 0.019),
        seg("for a fixed package revision.", 0.66, 0.402, 0.25, 0.02),
    };
}

}  // namespace

TEST_CASE("same-line fragments merge into a single segment",
          "[visual-line-grouping]") {
    const auto merged = tf::core::VisualLineGrouping::mergeLines(lineFragments());
    REQUIRE(merged.size() == 1);
    REQUIRE(merged[0].text ==
            "installed metadata. Prefer a commit SHA in place of main for a fixed package "
            "revision.");
}

TEST_CASE("merged bounding box is the union of fragments", "[visual-line-grouping]") {
    const auto merged = tf::core::VisualLineGrouping::mergeLines(lineFragments());
    REQUIRE(merged.size() == 1);
    REQUIRE(merged[0].boundingBox.minX() == Catch::Approx(0.10).epsilon(1e-9));
    REQUIRE(merged[0].boundingBox.maxX() == Catch::Approx(0.91).epsilon(1e-9));
    REQUIRE(merged[0].boundingBox.minY() == Catch::Approx(0.40).epsilon(1e-9));
    REQUIRE(merged[0].boundingBox.maxY() == Catch::Approx(0.422).epsilon(1e-9));
}

TEST_CASE("distinct lines stay separate", "[visual-line-grouping]") {
    const std::vector<tf::core::TextSegment> segments = {
        seg("first line", 0.1, 0.10, 0.4, 0.02),
        seg("second line", 0.1, 0.20, 0.4, 0.02),
        seg("third line", 0.1, 0.30, 0.4, 0.02),
    };
    const auto merged = tf::core::VisualLineGrouping::mergeLines(segments);
    REQUIRE(merged.size() == 3);
    std::vector<std::string> texts;
    for (const auto& s : merged) { texts.push_back(s.text); }
    REQUIRE(texts == std::vector<std::string>{"first line", "second line", "third line"});
}

TEST_CASE("distant columns on the same baseline do not merge", "[visual-line-grouping]") {
    const std::vector<tf::core::TextSegment> segments = {
        seg("left column", 0.05, 0.50, 0.20, 0.02),
        seg("right column", 0.70, 0.50, 0.25, 0.02),
    };
    REQUIRE(tf::core::VisualLineGrouping::mergeLines(segments).size() == 2);
}

TEST_CASE("nearby fragments on the same baseline merge", "[visual-line-grouping]") {
    const std::vector<tf::core::TextSegment> segments = {
        seg("Name", 0.10, 0.50, 0.10, 0.02),
        seg("Value", 0.15, 0.50, 0.10, 0.02),
    };
    const auto merged = tf::core::VisualLineGrouping::mergeLines(segments);
    REQUIRE(merged.size() == 1);
    REQUIRE(merged[0].text == "Name Value");
}

TEST_CASE("latin fragments join with a space", "[visual-line-grouping]") {
    REQUIRE(tf::core::VisualLineGrouping::joinText("set", "TOKEN to any") == "set TOKEN to any");
}

TEST_CASE("CJK fragments join without a space", "[visual-line-grouping]") {
    REQUIRE(tf::core::VisualLineGrouping::joinText("该注册表是公开的", "因此不需要令牌") ==
            "该注册表是公开的因此不需要令牌");
}

TEST_CASE("CJK/Latin boundary gets a space", "[visual-line-grouping]") {
    REQUIRE(tf::core::VisualLineGrouping::joinText("令牌", "TOKEN") == "令牌 TOKEN");
}

TEST_CASE("touching identifier fragments glue without a space", "[visual-line-grouping]") {
    const std::vector<tf::core::TextSegment> segments = {
        seg("set", 0.10, 0.50, 0.03, 0.02),
        seg("DESIGMA_PLUGINS_TOKEN", 0.132, 0.50, 0.20, 0.02),
        seg("to any", 0.34, 0.50, 0.06, 0.02),
    };
    const auto merged = tf::core::VisualLineGrouping::mergeLines(segments);
    REQUIRE(merged.size() == 1);
    REQUIRE(merged[0].text == "set DESIGMA_PLUGINS_TOKEN to any");
}

TEST_CASE("output is sorted top to bottom", "[visual-line-grouping]") {
    const std::vector<tf::core::TextSegment> segments = {
        seg("bottom", 0.1, 0.60, 0.3, 0.02),
        seg("top", 0.1, 0.10, 0.3, 0.02),
        seg("middle", 0.1, 0.35, 0.3, 0.02),
    };
    const auto merged = tf::core::VisualLineGrouping::mergeLines(segments);
    std::vector<std::string> texts;
    for (const auto& s : merged) { texts.push_back(s.text); }
    REQUIRE(texts == std::vector<std::string>{"top", "middle", "bottom"});
}

TEST_CASE("a single segment passes through", "[visual-line-grouping]") {
    const auto only = seg("solo", 0.1, 0.1, 0.2, 0.02);
    const auto merged = tf::core::VisualLineGrouping::mergeLines({only});
    REQUIRE(merged.size() == 1);
    REQUIRE(merged[0] == only);
}

TEST_CASE("empty input returns empty", "[visual-line-grouping]") {
    REQUIRE(tf::core::VisualLineGrouping::mergeLines({}).empty());
}

TEST_CASE("zero-size boxes are kept as-is (no crash, no merge)", "[visual-line-grouping]") {
    const std::vector<tf::core::TextSegment> segments = {
        seg("no box", 0.0, 0.0, 0.0, 0.0),
        seg("normal", 0.1, 0.1, 0.3, 0.02),
    };
    const auto merged = tf::core::VisualLineGrouping::mergeLines(segments);
    REQUIRE(merged.size() == 2);
    // Set comparison (order between unrelated entries is top-to-bottom, both
    // survive; Swift compares via Set because the two entries are unordered).
    std::vector<std::string> texts;
    for (const auto& s : merged) { texts.push_back(s.text); }
    std::sort(texts.begin(), texts.end());
    REQUIRE(texts == std::vector<std::string>{"no box", "normal"});
}

TEST_CASE("low vertical overlap does not merge", "[visual-line-grouping]") {
    const std::vector<tf::core::TextSegment> segments = {
        seg("upper", 0.1, 0.100, 0.3, 0.02),
        seg("lower", 0.5, 0.115, 0.3, 0.02),
    };
    REQUIRE(tf::core::VisualLineGrouping::mergeLines(segments).size() == 2);
}

TEST_CASE("user-edited flag propagates to the merged segment", "[visual-line-grouping]") {
    const std::vector<tf::core::TextSegment> segments = {
        seg("edited", 0.1, 0.1, 0.2, 0.02, 0.9f, true),
        seg("plain", 0.35, 0.1, 0.2, 0.02),
    };
    const auto merged = tf::core::VisualLineGrouping::mergeLines(segments);
    REQUIRE(merged.size() == 1);
    REQUIRE(merged[0].isUserEdited);
}

TEST_CASE("fixture file loads and every case reproduces its expectation",
          "[visual-line-grouping][fixture]") {
    // Fixture resolution: cwd-relative first (repo-root runs), then relative
    // to the executable (Windows MSBuild output dir gets a copy).
    std::ifstream in("Tests/fixtures/visual-line-grouping.json");
    if (!in) {
        in.open("windows/Tests/fixtures/visual-line-grouping.json");
    }
    if (!in) {
        // MSBuild output layout: .../build/<plat>/<cfg>/TransFrame.Core.Tests/
        // with fixtures copied next to the exe.
        in.open("visual-line-grouping.json");
    }
    REQUIRE(in.is_open());
    std::stringstream buffer;
    buffer << in.rdbuf();
    const auto fixture = tf::core::Json::parse(buffer.str());
    REQUIRE(fixture.contains("cases"));
    REQUIRE(fixture.contains("joinTextCases"));

    std::size_t verified = 0;
    for (const auto& testCase : fixture.at("cases")) {
        std::vector<tf::core::TextSegment> input;
        for (const auto& s : testCase.at("input")) {
            input.push_back(seg(s.at("text").get<std::string>().c_str(),
                                s.at("x").get<double>(), s.at("y").get<double>(),
                                s.at("width").get<double>(), s.at("height").get<double>(),
                                s.value("confidence", 1.0),
                                s.value("isUserEdited", false)));
        }
        const auto merged = tf::core::VisualLineGrouping::mergeLines(input);
        if (testCase.contains("expectedCount")) {
            REQUIRE(merged.size() == testCase.at("expectedCount").get<std::size_t>());
        }
        if (testCase.contains("expectedFirstText")) {
            REQUIRE(merged[0].text == testCase.at("expectedFirstText").get<std::string>());
        }
        if (testCase.contains("expectedTexts")) {
            std::vector<std::string> texts;
            for (const auto& m : merged) { texts.push_back(m.text); }
            REQUIRE(texts == testCase.at("expectedTexts").get<std::vector<std::string>>());
        }
        if (testCase.contains("expectedFirstBox")) {
            const auto& box = testCase.at("expectedFirstBox");
            REQUIRE(merged[0].boundingBox.minX() ==
                    Catch::Approx(box.at("minX").get<double>()).epsilon(1e-9));
            REQUIRE(merged[0].boundingBox.maxX() ==
                    Catch::Approx(box.at("maxX").get<double>()).epsilon(1e-9));
            REQUIRE(merged[0].boundingBox.minY() ==
                    Catch::Approx(box.at("minY").get<double>()).epsilon(1e-9));
            REQUIRE(merged[0].boundingBox.maxY() ==
                    Catch::Approx(box.at("maxY").get<double>()).epsilon(1e-9));
        }
        if (testCase.contains("expectedFirstUserEdited")) {
            REQUIRE(merged[0].isUserEdited == testCase.at("expectedFirstUserEdited").get<bool>());
        }
        ++verified;
    }
    REQUIRE(verified >= 12);

    for (const auto& joinCase : fixture.at("joinTextCases")) {
        REQUIRE(tf::core::VisualLineGrouping::joinText(
                    joinCase.at("lhs").get<std::string>(),
                    joinCase.at("rhs").get<std::string>()) ==
                joinCase.at("expected").get<std::string>());
    }
}
