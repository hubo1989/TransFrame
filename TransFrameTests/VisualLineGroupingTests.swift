import CoreGraphics
import XCTest
@testable import TransFrame

/// 视觉行聚合：同一视觉行上的 OCR/VLM 碎片在送译前合并为完整行。
final class VisualLineGroupingTests: XCTestCase {
    // MARK: - Fixtures

    /// 与截图问题同构：一行被拆成 3 段（含行内代码片段 `main`）。
    private func lineFragments() -> [TextSegment] {
        [
            TextSegment(text: "installed metadata. Prefer a commit SHA in place of", boundingBox: CGRect(x: 0.10, y: 0.40, width: 0.50, height: 0.02)),
            TextSegment(text: "main", boundingBox: CGRect(x: 0.61, y: 0.401, width: 0.04, height: 0.019)),
            TextSegment(text: "for a fixed package revision.", boundingBox: CGRect(x: 0.66, y: 0.402, width: 0.25, height: 0.02)),
        ]
    }

    // MARK: - Same-line merging

    func testSameLineFragmentsMergeIntoSingleSegment() {
        let merged = VisualLineGrouping.mergeLines(in: lineFragments())
        XCTAssertEqual(merged.count, 1)
        XCTAssertEqual(
            merged[0].text,
            "installed metadata. Prefer a commit SHA in place of main for a fixed package revision."
        )
    }

    func testMergedBoundingBoxIsUnionOfFragments() {
        let merged = VisualLineGrouping.mergeLines(in: lineFragments())
        XCTAssertEqual(merged[0].boundingBox.minX, 0.10, accuracy: 1e-9)
        XCTAssertEqual(merged[0].boundingBox.maxX, 0.91, accuracy: 1e-9)
        XCTAssertEqual(merged[0].boundingBox.minY, 0.40, accuracy: 1e-9)
        XCTAssertEqual(merged[0].boundingBox.maxY, 0.422, accuracy: 1e-9)
    }

    func testDistinctLinesStaySeparate() {
        let segments = [
            TextSegment(text: "first line", boundingBox: CGRect(x: 0.1, y: 0.10, width: 0.4, height: 0.02)),
            TextSegment(text: "second line", boundingBox: CGRect(x: 0.1, y: 0.20, width: 0.4, height: 0.02)),
            TextSegment(text: "third line", boundingBox: CGRect(x: 0.1, y: 0.30, width: 0.4, height: 0.02)),
        ]
        let merged = VisualLineGrouping.mergeLines(in: segments)
        XCTAssertEqual(merged.count, 3)
        XCTAssertEqual(merged.map(\.text), ["first line", "second line", "third line"])
    }

    /// 左右分栏：横向间距大于 3 倍行高，不得拼成一行。
    func testDistantColumnsOnSameBaselineDoNotMerge() {
        let segments = [
            TextSegment(text: "left column", boundingBox: CGRect(x: 0.05, y: 0.50, width: 0.20, height: 0.02)),
            TextSegment(text: "right column", boundingBox: CGRect(x: 0.70, y: 0.50, width: 0.25, height: 0.02)),
        ]
        let merged = VisualLineGrouping.mergeLines(in: segments)
        XCTAssertEqual(merged.count, 2)
    }

    /// 相邻碎片（横向间距小于阈值）按视觉行合并。
    func testNearbyFragmentsOnSameBaselineMerge() {
        let segments = [
            TextSegment(text: "Name", boundingBox: CGRect(x: 0.10, y: 0.50, width: 0.10, height: 0.02)),
            TextSegment(text: "Value", boundingBox: CGRect(x: 0.15, y: 0.50, width: 0.10, height: 0.02)),
        ]
        let merged = VisualLineGrouping.mergeLines(in: segments)
        XCTAssertEqual(merged.count, 1)
        XCTAssertEqual(merged[0].text, "Name Value")
    }

    // MARK: - Text joining

    func testLatinFragmentsJoinWithSpace() {
        XCTAssertEqual(VisualLineGrouping.joinText("set", "TOKEN to any"), "set TOKEN to any")
    }

    func testCJKFragmentsJoinWithoutSpace() {
        XCTAssertEqual(VisualLineGrouping.joinText("该注册表是公开的", "因此不需要令牌"), "该注册表是公开的因此不需要令牌")
    }

    func testCJKAndLatinBoundaryGetsSpace() {
        XCTAssertEqual(VisualLineGrouping.joinText("令牌", "TOKEN"), "令牌 TOKEN")
    }

    // MARK: - Ordering & edge cases

    func testOutputIsSortedTopToBottom() {
        // 输入乱序：第三行在前
        let segments = [
            TextSegment(text: "bottom", boundingBox: CGRect(x: 0.1, y: 0.60, width: 0.3, height: 0.02)),
            TextSegment(text: "top", boundingBox: CGRect(x: 0.1, y: 0.10, width: 0.3, height: 0.02)),
            TextSegment(text: "middle", boundingBox: CGRect(x: 0.1, y: 0.35, width: 0.3, height: 0.02)),
        ]
        let merged = VisualLineGrouping.mergeLines(in: segments)
        XCTAssertEqual(merged.map(\.text), ["top", "middle", "bottom"])
    }

    func testSingleSegmentPassesThrough() {
        let only = TextSegment(text: "solo", boundingBox: CGRect(x: 0.1, y: 0.1, width: 0.2, height: 0.02))
        XCTAssertEqual(VisualLineGrouping.mergeLines(in: [only]), [only])
    }

    func testEmptyInputReturnsEmpty() {
        XCTAssertTrue(VisualLineGrouping.mergeLines(in: []).isEmpty)
    }

    /// 零尺寸包围盒（如无坐标的纯文本结果）不参与合并、不崩溃。
    func testZeroSizeBoxesAreKeptAsIs() {
        let segments = [
            TextSegment(text: "no box", boundingBox: .zero),
            TextSegment(text: "normal", boundingBox: CGRect(x: 0.1, y: 0.1, width: 0.3, height: 0.02)),
        ]
        let merged = VisualLineGrouping.mergeLines(in: segments)
        XCTAssertEqual(merged.count, 2)
        XCTAssertEqual(Set(merged.map(\.text)), ["no box", "normal"])
    }

    /// Y 重叠低于 50%（基线错开的邻行）不得合并。
    func testLowVerticalOverlapDoesNotMerge() {
        let segments = [
            TextSegment(text: "upper", boundingBox: CGRect(x: 0.1, y: 0.100, width: 0.3, height: 0.02)),
            TextSegment(text: "lower", boundingBox: CGRect(x: 0.5, y: 0.115, width: 0.3, height: 0.02)),
        ]
        let merged = VisualLineGrouping.mergeLines(in: segments)
        XCTAssertEqual(merged.count, 2)
    }

    func testUserEditedFlagPropagatesToMergedSegment() {
        let edited = TextSegment(
            text: "edited", boundingBox: CGRect(x: 0.1, y: 0.1, width: 0.2, height: 0.02),
            confidence: 0.9, isUserEdited: true
        )
        let plain = TextSegment(text: "plain", boundingBox: CGRect(x: 0.35, y: 0.1, width: 0.2, height: 0.02))
        let merged = VisualLineGrouping.mergeLines(in: [edited, plain])
        XCTAssertEqual(merged.count, 1)
        XCTAssertTrue(merged[0].isUserEdited)
    }

    // MARK: - ScreenAnalysisResult integration

    func testMergingVisualLinesReturnsGroupedResult() {
        let result = ScreenAnalysisResult(
            segments: lineFragments() + [
                TextSegment(text: "next line", boundingBox: CGRect(x: 0.10, y: 0.50, width: 0.3, height: 0.02)),
            ],
            imageSize: CGSize(width: 1000, height: 600)
        )
        let grouped = result.mergingVisualLines()
        XCTAssertEqual(grouped.segments.count, 2)
        XCTAssertTrue(grouped.segments[0].text.hasPrefix("installed metadata"))
        XCTAssertEqual(grouped.segments[1].text, "next line")
    }
}
