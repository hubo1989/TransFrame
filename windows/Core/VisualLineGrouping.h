#pragma once
// VisualLineGrouping.h — pure port of Swift VisualLineGrouping.swift:
// merge OCR/VLM fragments that sit on the same visual line into complete
// line segments before translation. Identical thresholds; test vectors
// shared with TransFrameTests/VisualLineGroupingTests.swift.

#include "CaptureDocument.h"

#include <string>
#include <vector>

namespace tf::core {

struct VisualLineGrouping {
    /// Minimum Y-projection overlap (fraction of the shorter box height) for
    /// a fragment to join a line.
    static constexpr double verticalOverlapThreshold = 0.5;
    /// Maximum horizontal gap between a fragment and the line it joins, as a
    /// multiple of the average line height.
    static constexpr double horizontalGapFactor = 3.0;

    /// Groups segments into visual lines, ordered top-to-bottom, left-to-right.
    static std::vector<TextSegment> mergeLines(const std::vector<TextSegment>& segments);

    /// Joins two fragments: CJK-adjacent text glues directly; near-zero gap
    /// between identifier-shaped fragments glues; everything else gets a
    /// space (Swift joinText).
    static std::string joinText(const std::string& lhs, const std::string& rhs,
                                bool gluesDirectly = false);

    // Internals exposed for tests (mirrors Swift's @testable visibility).
    static bool isCJK(char32_t c);
    static bool isIdentifierCharacter(char32_t c);
    static bool isIdentifierLike(const std::string& s);
    static bool belongsToSameLine(const TextSegment& fragment,
                                  const std::vector<TextSegment>& lineFragments);
    static std::optional<TextSegment> mergedSegment(const std::vector<TextSegment>& fragments);
};

}  // namespace tf::core
