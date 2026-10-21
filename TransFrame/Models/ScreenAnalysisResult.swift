import CoreGraphics
import Foundation

// MARK: - TextSegment

/// A single text segment extracted by VLM analysis.
/// Contains the recognized text, its normalized position, and confidence score.
struct TextSegment: Identifiable, Codable, Sendable, Equatable {
    /// Unique identifier for this segment
    let id: UUID

    /// The recognized text content
    let text: String

    /// Bounding box of the text in the image (normalized coordinates 0-1)
    /// Origin is top-left, coordinates represent (x, y, width, height)
    let boundingBox: CGRect

    /// Confidence score from VLM (0.0 to 1.0)
    let confidence: Float

    /// Whether this segment was manually modified by the user
    var isUserEdited: Bool

    /// Initialize with all properties
    init(id: UUID = UUID(), text: String, boundingBox: CGRect, confidence: Float = 1.0, isUserEdited: Bool = false) {
        self.id = id
        self.text = text
        self.boundingBox = boundingBox
        self.confidence = confidence
        self.isUserEdited = isUserEdited
    }

    /// Whether this segment has high confidence (> 0.7)
    var isHighConfidence: Bool {
        confidence > 0.7
    }
}

// MARK: - TextSegment Bounding Box Utilities

extension TextSegment {
    /// Returns the bounding box in pixel coordinates
    /// - Parameter imageSize: The image size in pixels
    /// - Returns: Bounding box in pixel coordinates
    func pixelBoundingBox(in imageSize: CGSize) -> CGRect {
        CGRect(
            x: boundingBox.minX * imageSize.width,
            y: boundingBox.minY * imageSize.height,
            width: boundingBox.width * imageSize.width,
            height: boundingBox.height * imageSize.height
        )
    }

    /// Returns the center point in pixel coordinates
    /// - Parameter imageSize: The image size in pixels
    /// - Returns: Center point in pixel coordinates
    func centerPoint(in imageSize: CGSize) -> CGPoint {
        CGPoint(
            x: boundingBox.midX * imageSize.width,
            y: boundingBox.midY * imageSize.height
        )
    }

    /// Heuristic filter for OCR noise that should not be translated as primary content.
    var isLikelyTranslationNoise: Bool {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return true }
        let isNearImageEdge =
            boundingBox.minX < 0.08
            || boundingBox.maxX > 0.92
            || boundingBox.minY < 0.08
            || boundingBox.maxY > 0.92

        // Filter coordinate-like strings (e.g., "0.5, 0.3", "(x:0.5, y:0.3)", "x: 0.5")
        if trimmed.range(of: #"^[\(\[]?[xy]?\s*[:\:]?\s*[\d.]+\s*[,，]\s*[xy]?\s*[:\:]?\s*[\d.]+[\)\]]?$"#, options: .regularExpression) != nil {
            return true
        }
        // Filter single coordinate values (e.g., "x: 0.5", "y: 0.3")
        if trimmed.range(of: #"^[xy]\s*[:\:]?\s*[\d.]+$"#, options: .regularExpression) != nil {
            return true
        }

        if trimmed.count == 1,
           trimmed.range(of: #"^[\d\p{P}\p{S}]$"#, options: .regularExpression) != nil {
            return true
        }

        if trimmed.count <= 12,
           trimmed.range(of: #"^[\d\s.,:;%+\-_=(){}\[\]/\\|<>]+$"#, options: .regularExpression) != nil {
            return true
        }

        if trimmed.count <= 4,
           confidence < 0.35,
           trimmed.range(of: #"^[\p{P}\p{S}\dA-Za-z]{1,4}$"#, options: .regularExpression) != nil {
            return true
        }

        if isNearImageEdge,
           trimmed.count <= 8,
           trimmed.range(
               of: #"^(?:q[1-4]|jan|feb|mar|apr|may|jun|jul|aug|sep|sept|oct|nov|dec|mon|tue|wed|thu|fri|sat|sun|\d{1,2}:\d{2}(?:am|pm)?|\d{4})$"#,
               options: [.regularExpression, .caseInsensitive]
           ) != nil {
            return true
        }

        return false
    }
    /// Heuristic for leaked OCR prompt/schema instructions accidentally returned by VLMs.
    var isLikelyOCRPromptLeakage: Bool {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return false }

        let normalized = trimmed.lowercased()
        let strongSignals = [
            "\"segments\"",
            "\"boundingbox\"",
            "\"confidence\"",
            "\"width\"",
            "\"height\"",
            "return json",
            "json format",
            "top-left corner",
            "normalized to image size",
            "boundingbox",
            "置信度",
            "左上角",
            "右上角",
            "箱形尺寸",
            "json格式",
            "返回json",
        ]

        if strongSignals.contains(where: normalized.contains) {
            return true
        }

        let weakSignals = [
            "x, y",
            "width, height",
            "0.0-1.0",
            "0.0–1.0",
            "宽度",
            "高度",
            "归一化",
        ]
        let weakSignalCount = weakSignals.reduce(into: 0) { count, signal in
            if normalized.contains(signal) {
                count += 1
            }
        }

        return weakSignalCount >= 2
    }
}

// MARK: - ScreenAnalysisResult

/// The result of VLM-based screen analysis.
/// Contains all extracted text segments with their positions.
struct ScreenAnalysisResult: Codable, Sendable, Equatable {
    /// Active text segments used for display and translation
    var segments: [TextSegment]

    /// Unfiltered original segments directly from OCR/VLM
    var rawSegments: [TextSegment]

    /// Segments filtered out due to noise or low confidence
    var filteredOutSegments: [TextSegment]

    /// The source image dimensions (width x height in pixels)
    let imageSize: CGSize

    /// Initialize with segments, rawSegments, and image size
    init(
        segments: [TextSegment] = [],
        rawSegments: [TextSegment] = [],
        filteredOutSegments: [TextSegment] = [],
        imageSize: CGSize
    ) {
        self.segments = segments
        self.rawSegments = rawSegments.isEmpty ? segments : rawSegments
        self.filteredOutSegments = filteredOutSegments
        self.imageSize = imageSize
    }

    /// Total number of text segments
    var count: Int {
        segments.count
    }

    /// Whether any text was found
    var hasResults: Bool {
        !segments.isEmpty
    }

    /// All recognized text concatenated with newlines, sorted by vertical position
    var fullText: String {
        segments
            .sorted { $0.boundingBox.minY < $1.boundingBox.minY }
            .map(\.text)
            .joined(separator: "\n")
    }

    /// Filter segments by minimum confidence level
    func filter(minimumConfidence: Float) -> ScreenAnalysisResult {
        let passed = segments.filter { $0.confidence >= minimumConfidence }
        let excluded = segments.filter { $0.confidence < minimumConfidence }
        return ScreenAnalysisResult(
            segments: passed,
            rawSegments: rawSegments,
            filteredOutSegments: filteredOutSegments + excluded,
            imageSize: imageSize
        )
    }

    /// Restores all previously filtered-out segments
    func recoveringFilteredSegments() -> ScreenAnalysisResult {
        var combined = segments + filteredOutSegments
        // Deduplicate by ID
        var seen = Set<UUID>()
        combined.removeAll { !seen.insert($0.id).inserted }
        return ScreenAnalysisResult(
            segments: combined,
            rawSegments: rawSegments,
            filteredOutSegments: [],
            imageSize: imageSize
        )
    }

    /// Manually updates a segment's recognized text, preserving isUserEdited flag
    func updatingSegment(id: UUID, newText: String) -> ScreenAnalysisResult {
        let updated = segments.map { segment -> TextSegment in
            if segment.id == id {
                return TextSegment(
                    id: segment.id,
                    text: newText,
                    boundingBox: segment.boundingBox,
                    confidence: 1.0,
                    isUserEdited: true
                )
            }
            return segment
        }
        return ScreenAnalysisResult(
            segments: updated,
            rawSegments: rawSegments,
            filteredOutSegments: filteredOutSegments,
            imageSize: imageSize
        )
    }

    /// Merges re-recognized segments within a specific region of interest (ROI)
    func mergingROISegments(_ newSegments: [TextSegment], in roiNormalizedRect: CGRect) -> ScreenAnalysisResult {
        // Keep existing segments outside ROI, and replace overlapping non-edited segments
        var remaining = segments.filter { segment in
            if segment.isUserEdited { return true }
            return !segment.boundingBox.intersects(roiNormalizedRect)
        }
        remaining.append(contentsOf: newSegments)
        return ScreenAnalysisResult(
            segments: remaining,
            rawSegments: rawSegments + newSegments,
            filteredOutSegments: filteredOutSegments,
            imageSize: imageSize
        )
    }

    /// Get segments within a specific region
    func segments(in rect: CGRect) -> [TextSegment] {
        segments.filter { $0.boundingBox.intersects(rect) }
    }

    /// Removes coordinate ticks, isolated symbols, and similar OCR noise before translation.
    func filteredForTranslation() -> ScreenAnalysisResult {
        let filtered = segments.filter {
            !$0.isLikelyTranslationNoise && !$0.isLikelyOCRPromptLeakage
        }
        let excluded = segments.filter {
            $0.isLikelyTranslationNoise || $0.isLikelyOCRPromptLeakage
        }
        return ScreenAnalysisResult(
            segments: filtered,
            rawSegments: rawSegments,
            filteredOutSegments: filteredOutSegments + excluded,
            imageSize: imageSize
        )
    }

    /// Whether every segment appears to be prompt/schema leakage instead of real UI text.
    var containsOnlyPromptLeakage: Bool {
        let nonNoiseSegments = segments.filter { !$0.isLikelyTranslationNoise }
        return !nonNoiseSegments.isEmpty && nonNoiseSegments.allSatisfy(\.isLikelyOCRPromptLeakage)
    }
}

// MARK: - Empty Result

extension ScreenAnalysisResult {
    /// Creates an empty analysis result for the given image size
    static func empty(imageSize: CGSize) -> ScreenAnalysisResult {
        ScreenAnalysisResult(segments: [], imageSize: imageSize)
    }

    init(ocrResult: OCRResult) {
        self.init(
            segments: ocrResult.observations.map {
                TextSegment(
                    text: $0.text,
                    boundingBox: $0.boundingBox,
                    confidence: $0.confidence
                )
            },
            imageSize: ocrResult.imageSize
        )
    }
}
