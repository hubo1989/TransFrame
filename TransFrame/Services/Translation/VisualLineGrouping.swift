import CoreGraphics
import Foundation

/// Merges OCR/VLM fragments that sit on the same visual line into complete
/// line segments before translation.
///
/// VLM analysis frequently splits inline code (`main`, `DESIGMA_PLUGINS_TOKEN`)
/// into standalone fragments. Translated independently they produce misplaced
/// overlay strips and out-of-context mistranslations ("main" → “主要”).
/// Grouping first gives every visual line a single text and a single anchor.
enum VisualLineGrouping {
    /// Minimum Y-projection overlap (fraction of the shorter box height) for
    /// a fragment to join a line.
    static let verticalOverlapThreshold: CGFloat = 0.5
    /// Maximum horizontal gap between a fragment and the line it joins, as a
    /// multiple of the average line height. Keeps table columns and
    /// multi-column layouts from being glued into one line.
    static let horizontalGapFactor: CGFloat = 3.0

    /// Groups `segments` into visual lines, ordered top-to-bottom, left-to-right.
    static func mergeLines(in segments: [TextSegment]) -> [TextSegment] {
        guard segments.count > 1 else { return segments }

        let sorted = segments.sorted { lhs, rhs in
            let lhsY = lhs.boundingBox.midY
            let rhsY = rhs.boundingBox.midY
            if abs(lhsY - rhsY) > 1e-6 { return lhsY < rhsY }
            return lhs.boundingBox.minX < rhs.boundingBox.minX
        }

        var lines: [[TextSegment]] = []
        for segment in sorted {
            if let index = lines.firstIndex(where: { belongsToSameLine(segment, lineFragments: $0) }) {
                lines[index].append(segment)
            } else {
                lines.append([segment])
            }
        }
        return lines.compactMap(mergedSegment)
    }

    // MARK: - Internals

    /// A fragment joins a line when it overlaps the line vertically (Y
    /// projection ≥ 50% of the shorter box) and sits within a small
    /// horizontal gap of the line's current X span.
    private static func belongsToSameLine(_ segment: TextSegment, lineFragments: [TextSegment]) -> Bool {
        let box = segment.boundingBox
        guard box.width > 0, box.height > 0 else { return false }
        let lineBox = union(of: lineFragments.map(\.boundingBox))
        guard lineBox.width > 0, lineBox.height > 0 else { return false }

        let overlap = min(box.maxY, lineBox.maxY) - max(box.minY, lineBox.minY)
        guard overlap > 0,
              overlap / min(box.height, lineBox.height) >= verticalOverlapThreshold else { return false }

        let gap = max(0, max(box.minX, lineBox.minX) - min(box.maxX, lineBox.maxX))
        let averageHeight = (box.height + lineBox.height) / 2
        return gap <= horizontalGapFactor * averageHeight
    }

    /// Merges one line's fragments (left-to-right) into a single segment:
    /// joined text, union bounding box, averaged confidence. Fragments that
    /// nearly touch (gap ≤ 25% of the shorter line height) join without a
    /// space so VLM-split code identifiers ("DESIGMA" + "_PLUGINS_TOKEN")
    /// are not corrupted by inserted spaces.
    private static func mergedSegment(from fragments: [TextSegment]) -> TextSegment? {
        guard let first = fragments.first else { return nil }
        guard fragments.count > 1 else { return first }

        let ordered = fragments.sorted { $0.boundingBox.minX < $1.boundingBox.minX }
        var joinedText = ordered[0].text
        for (previous, next) in zip(ordered, ordered.dropFirst()) {
            let gap = next.boundingBox.minX - previous.boundingBox.maxX
            let referenceHeight = min(previous.boundingBox.height, next.boundingBox.height)
            let nearlyTouching = referenceHeight > 0 && gap <= referenceHeight * 0.25
            joinedText = joinText(joinedText, next.text, gluesDirectly: nearlyTouching)
        }
        let box = union(of: ordered.map(\.boundingBox))
        let confidence = ordered.map(\.confidence).reduce(0, +) / Float(ordered.count)
        return TextSegment(
            text: joinedText,
            boundingBox: box,
            confidence: confidence,
            isUserEdited: ordered.contains(where: \.isUserEdited)
        )
    }

    /// Joins two fragments: CJK-adjacent text glues directly; a near-zero gap
    /// between two identifier-shaped fragments (underscore boundary, or both
    /// sides look like code identifiers) glues directly so VLM-split tokens
    /// ("DESIGMA" + "_PLUGINS_TOKEN") survive; everything else gets a space.
    static func joinText(_ lhs: String, _ rhs: String, gluesDirectly: Bool = false) -> String {
        guard let last = lhs.last, let first = rhs.first else { return lhs + rhs }
        if isCJK(last), isCJK(first) { return lhs + rhs }
        if gluesDirectly,
           isIdentifierCharacter(last), isIdentifierCharacter(first),
           lhs.hasSuffix("_") || rhs.hasPrefix("_")
               || (isIdentifierLike(lhs) && isIdentifierLike(rhs)) {
            return lhs + rhs
        }
        return lhs + " " + rhs
    }

    /// Heuristic "looks like a code identifier": contains an underscore or a
    /// digit, or has an uppercase letter after the first character
    /// (camelCase / SCREAMING_CASE). Capitalized plain words like "Name"
    /// don't qualify, so touching OCR fragments of normal prose still get
    /// the separating space.
    private static func isIdentifierLike(_ text: String) -> Bool {
        guard let first = text.first else { return false }
        let afterFirst = text.dropFirst()
        return text.contains("_")
            || text.contains(where: \.isNumber)
            || afterFirst.contains(where: \.isUppercase)
    }

    private static func isIdentifierCharacter(_ character: Character) -> Bool {
        guard let ascii = character.asciiValue else { return false }
        return (0x30...0x39).contains(ascii)
            || (0x41...0x5A).contains(ascii)
            || (0x61...0x7A).contains(ascii)
            || ascii == 0x5F
    }

    private static func isCJK(_ character: Character) -> Bool {
        character.unicodeScalars.allSatisfy { scalar in
            (0x4E00...0x9FFF).contains(scalar.value)
                || (0x3400...0x4DBF).contains(scalar.value)
                || (0x3000...0x303F).contains(scalar.value)
                || (0xFF00...0xFFEF).contains(scalar.value)
        }
    }

    private static func union(of boxes: [CGRect]) -> CGRect {
        boxes.reduce(CGRect.null) { $0.union($1) }
    }
}
