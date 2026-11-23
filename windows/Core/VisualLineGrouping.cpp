// VisualLineGrouping.cpp — pure port of Swift VisualLineGrouping.swift.
#include "VisualLineGrouping.h"

#include <algorithm>
#include <cstdint>

namespace tf::core {
namespace {

/// Decode one UTF-8 code point at index i; advances i (invalid byte -> U+FFFD).
char32_t decodeUtf8(const std::string& s, std::size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        ++i;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
        const char32_t cp = (static_cast<char32_t>(c & 0x1F) << 6) | (s[i + 1] & 0x3F);
        i += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
        const char32_t cp = (static_cast<char32_t>(c & 0x0F) << 12) |
                            (static_cast<char32_t>(s[i + 1] & 0x3F) << 6) |
                            (s[i + 2] & 0x3F);
        i += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
        const char32_t cp = (static_cast<char32_t>(c & 0x07) << 18) |
                            (static_cast<char32_t>(s[i + 1] & 0x3F) << 12) |
                            (static_cast<char32_t>(s[i + 2] & 0x3F) << 6) |
                            (s[i + 3] & 0x3F);
        i += 4;
        return cp;
    }
    ++i;
    return 0xFFFD;
}

char32_t lastCodePoint(const std::string& s) {
    if (s.empty()) { return 0; }
    std::size_t i = 0;
    char32_t last = 0;
    while (i < s.size()) { last = decodeUtf8(s, i); }
    return last;
}

char32_t firstCodePoint(const std::string& s) {
    if (s.empty()) { return 0; }
    std::size_t i = 0;
    return decodeUtf8(s, i);
}

Rect unionOf(const std::vector<Rect>& boxes) {
    Rect out = Rect::nullRect();
    for (const Rect& b : boxes) { out = out.unionWith(b); }
    return out;
}

}  // namespace

bool VisualLineGrouping::isCJK(char32_t c) {
    return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) ||
           (c >= 0x3000 && c <= 0x303F) || (c >= 0xFF00 && c <= 0xFFEF);
}

bool VisualLineGrouping::isIdentifierCharacter(char32_t c) {
    return (c >= U'0' && c <= U'9') || (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') ||
           c == U'_';
}

bool VisualLineGrouping::isIdentifierLike(const std::string& s) {
    if (s.empty()) { return false; }
    // Contains underscore, a digit anywhere, or an uppercase letter after the
    // first character (camelCase / SCREAMING_CASE). "Name" doesn't qualify.
    if (s.find('_') != std::string::npos) { return true; }
    std::size_t i = 0;
    bool first = true;
    bool hasUpperAfterFirst = false;
    while (i < s.size()) {
        const char32_t c = decodeUtf8(s, i);
        if (!first) {
            if (c >= U'A' && c <= U'Z') { hasUpperAfterFirst = true; }
        }
        if (c >= U'0' && c <= U'9') { return true; }
        first = false;
    }
    return hasUpperAfterFirst;
}

bool VisualLineGrouping::belongsToSameLine(const TextSegment& fragment,
                                           const std::vector<TextSegment>& lineFragments) {
    if (lineFragments.empty()) { return false; }
    const Rect& box = fragment.boundingBox;
    if (!(box.width > 0) || !(box.height > 0)) { return false; }

    // lineBox spans ONLY the existing line fragments (Swift union(of:)).
    std::vector<Rect> lineBoxes;
    lineBoxes.reserve(lineFragments.size());
    for (const auto& f : lineFragments) { lineBoxes.push_back(f.boundingBox); }
    const Rect lineBox = unionOf(lineBoxes);
    if (!(lineBox.width > 0) || !(lineBox.height > 0)) { return false; }

    const double overlap =
        std::min(box.maxY(), lineBox.maxY()) - std::max(box.minY(), lineBox.minY());
    if (!(overlap > 0) ||
        overlap / std::min(box.height, lineBox.height) < verticalOverlapThreshold) {
        return false;
    }

    const double gap =
        std::max(0.0, std::max(box.minX(), lineBox.minX()) - std::min(box.maxX(), lineBox.maxX()));
    const double averageHeight = (box.height + lineBox.height) / 2.0;
    return gap <= horizontalGapFactor * averageHeight;
}

std::optional<TextSegment> VisualLineGrouping::mergedSegment(
    const std::vector<TextSegment>& fragments) {
    if (fragments.empty()) { return std::nullopt; }
    if (fragments.size() == 1) { return fragments.front(); }

    std::vector<TextSegment> ordered = fragments;
    std::sort(ordered.begin(), ordered.end(), [](const TextSegment& a, const TextSegment& b) {
        return a.boundingBox.minX() < b.boundingBox.minX();
    });

    std::string joinedText = ordered.front().text;
    for (std::size_t i = 1; i < ordered.size(); ++i) {
        const TextSegment& previous = ordered[i - 1];
        const TextSegment& next = ordered[i];
        const double gap = next.boundingBox.minX() - previous.boundingBox.maxX();
        const double referenceHeight =
            std::min(previous.boundingBox.height, next.boundingBox.height);
        const bool nearlyTouching = referenceHeight > 0 && gap <= referenceHeight * 0.25;
        joinedText = joinText(joinedText, next.text, nearlyTouching);
    }

    std::vector<Rect> boxes;
    boxes.reserve(ordered.size());
    bool anyEdited = false;
    float confidenceSum = 0.0f;
    for (const auto& f : ordered) {
        boxes.push_back(f.boundingBox);
        anyEdited = anyEdited || f.isUserEdited;
        confidenceSum += f.confidence;
    }

    TextSegment merged;
    merged.text = std::move(joinedText);
    merged.boundingBox = unionOf(boxes);
    merged.confidence = confidenceSum / static_cast<float>(ordered.size());
    merged.isUserEdited = anyEdited;
    return merged;
}

std::vector<TextSegment> VisualLineGrouping::mergeLines(const std::vector<TextSegment>& segments) {
    if (segments.size() <= 1) { return segments; }

    std::vector<TextSegment> sorted = segments;
    std::sort(sorted.begin(), sorted.end(), [](const TextSegment& lhs, const TextSegment& rhs) {
        const double lhsY = lhs.boundingBox.midY();
        const double rhsY = rhs.boundingBox.midY();
        if (std::abs(lhsY - rhsY) > 1e-6) { return lhsY < rhsY; }
        return lhs.boundingBox.minX() < rhs.boundingBox.minX();
    });

    std::vector<std::vector<TextSegment>> lines;
    for (auto& segment : sorted) {
        bool joined = false;
        for (auto& line : lines) {
            if (belongsToSameLine(segment, line)) {
                line.push_back(segment);
                joined = true;
                break;
            }
        }
        if (!joined) { lines.push_back({segment}); }
    }

    std::vector<TextSegment> out;
    out.reserve(lines.size());
    for (const auto& line : lines) {
        if (auto merged = mergedSegment(line)) { out.push_back(std::move(*merged)); }
    }
    return out;
}

std::string VisualLineGrouping::joinText(const std::string& lhs, const std::string& rhs,
                                         bool gluesDirectly) {
    if (lhs.empty() || rhs.empty()) { return lhs + rhs; }
    const char32_t last = lastCodePoint(lhs);
    const char32_t first = firstCodePoint(rhs);
    if (isCJK(last) && isCJK(first)) { return lhs + rhs; }
    if (gluesDirectly && isIdentifierCharacter(last) && isIdentifierCharacter(first)) {
        const bool lhsEndsUnderscore = !lhs.empty() && lhs.back() == '_';
        std::size_t start = 0;
        decodeUtf8(rhs, start);  // measure the first code point's byte length
        const std::string firstCharUtf8 = rhs.substr(0, start);
        const bool rhsStartsUnderscore = firstCharUtf8 == "_";
        if (lhsEndsUnderscore || rhsStartsUnderscore ||
            (isIdentifierLike(lhs) && isIdentifierLike(rhs))) {
            return lhs + rhs;
        }
    }
    return lhs + " " + rhs;
}

}  // namespace tf::core
