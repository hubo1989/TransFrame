// VisualLineGrouping.cpp — pure port of Swift VisualLineGrouping.swift.
#include "tf/VisualLineGrouping.hpp"

#include <algorithm>
#include <cstdint>

namespace tf {

namespace {

/// Decode one UTF-8 code point at index i; advances i.
char32_t decodeUtf8(const std::string& s, std::size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        ++i;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
        char32_t cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F);
        i += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
        char32_t cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
        i += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
        char32_t cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) |
                      (s[i + 3] & 0x3F);
        i += 4;
        return cp;
    }
    ++i;
    return 0xFFFD;
}

char32_t lastCodePoint(const std::string& s) {
    if (s.empty()) return 0;
    // Walk all code points, keep the last.
    std::size_t i = 0;
    char32_t last = 0;
    while (i < s.size()) last = decodeUtf8(s, i);
    return last;
}

char32_t firstCodePoint(const std::string& s) {
    if (s.empty()) return 0;
    std::size_t i = 0;
    return decodeUtf8(s, i);
}

}  // namespace

bool VisualLineGrouping::isCJK(char32_t c) {
    return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) ||
           (c >= 0x3000 && c <= 0x303F) || (c >= 0xFF00 && c <= 0xFFEF);
}

bool VisualLineGrouping::isIdentifierCharacter(char32_t c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool VisualLineGrouping::isIdentifierLike(const std::string& s) {
    if (s.empty()) return false;
    // Underscore, digit anywhere, or uppercase after the first character.
    if (s.find('_') != std::string::npos) return true;
    std::size_t i = 0;
    bool first = true;
    while (i < s.size()) {
        const char32_t c = decodeUtf8(s, i);
        if (!first) {
            if (c >= 'A' && c <= 'Z') return true;
        }
        first = false;
        if (c >= '0' && c <= '9') return true;
    }
    return false;
}

bool VisualLineGrouping::belongsToSameLine(const TextSegment& fragment,
                                           const std::vector<TextSegment>& lineFragments) {
    const NormRect& box = fragment.boundingBox;
    if (!(box.width > 0) || !(box.height > 0)) return false;
    NormRect lineBox = lineFragments.front().boundingBox;
    for (std::size_t i = 1; i < lineFragments.size(); ++i) {
        lineBox = lineBox.unionWith(lineFragments[i].boundingBox);
    }
    if (!(lineBox.width > 0) || !(lineBox.height > 0)) return false;

    const double overlap = std::min(box.maxY(), lineBox.maxY()) - std::max(box.minY(), lineBox.minY());
    if (!(overlap > 0)) return false;
    const double shorter = std::min(box.height, lineBox.height);
    if (overlap / shorter < verticalOverlapThreshold) return false;

    const double gap =
        std::max(0.0, std::max(box.minX(), lineBox.minX()) - std::min(box.maxX(), lineBox.maxX()));
    const double averageHeight = (box.height + lineBox.height) / 2;
    return gap <= horizontalGapFactor * averageHeight;
}

std::vector<TextSegment> VisualLineGrouping::mergeLines(const std::vector<TextSegment>& segments) {
    if (segments.size() <= 1) return segments;

    // Sort by midY, then minX (stable like Swift's sorted with tie-break).
    std::vector<TextSegment> sorted = segments;
    std::stable_sort(sorted.begin(), sorted.end(), [](const TextSegment& a, const TextSegment& b) {
        const double ay = a.boundingBox.midY();
        const double by = b.boundingBox.midY();
        if (std::abs(ay - by) > 1e-6) return ay < by;
        return a.boundingBox.minX() < b.boundingBox.minX();
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
        if (!joined) lines.push_back({segment});
    }

    // Merge each line's fragments left-to-right.
    std::vector<TextSegment> out;
    out.reserve(lines.size());
    for (auto& line : lines) {
        if (line.empty()) continue;
        if (line.size() == 1) {
            out.push_back(line.front());
            continue;
        }
        std::vector<TextSegment> ordered = line;
        std::stable_sort(ordered.begin(), ordered.end(), [](const TextSegment& a, const TextSegment& b) {
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
        NormRect box = ordered.front().boundingBox;
        for (std::size_t i = 1; i < ordered.size(); ++i) {
            box = box.unionWith(ordered[i].boundingBox);
        }
        float confidence = 0;
        for (const auto& f : ordered) confidence += f.confidence;
        confidence /= static_cast<float>(ordered.size());
        TextSegment merged(joinedText, box, confidence);
        for (const auto& f : ordered) {
            if (f.isUserEdited) {
                merged.isUserEdited = true;
                break;
            }
        }
        out.push_back(std::move(merged));
    }
    return out;
}

std::string VisualLineGrouping::joinText(const std::string& lhs, const std::string& rhs,
                                         bool gluesDirectly) {
    if (lhs.empty() || rhs.empty()) return lhs + rhs;
    const char32_t last = lastCodePoint(lhs);
    const char32_t first = firstCodePoint(rhs);
    if (isCJK(last) && isCJK(first)) return lhs + rhs;
    const bool lhsEndsUnderscore = !lhs.empty() && lhs.back() == '_';
    const bool rhsStartsUnderscore = !rhs.empty() && rhs.front() == '_';
    if (gluesDirectly && isIdentifierCharacter(last) && isIdentifierCharacter(first) &&
        (lhsEndsUnderscore || rhsStartsUnderscore ||
         (isIdentifierLike(lhs) && isIdentifierLike(rhs)))) {
        return lhs + rhs;
    }
    return lhs + " " + rhs;
}

}  // namespace tf
