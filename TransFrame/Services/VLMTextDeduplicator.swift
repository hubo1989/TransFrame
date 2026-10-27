//
//  VLMTextDeduplicator.swift
//  TransFrame
//
//  Shared component for deduplicating VLM text segments
//  Detects hallucinations and removes duplicate segments
//

import Foundation
import CoreGraphics

/// Shared component for deduplicating VLM text segments
/// Used by both ClaudeVLMProvider and OpenAIVLMProvider
enum VLMTextDeduplicator {

    /// Configuration for deduplication behavior
    struct Configuration: Sendable {
        /// Minimum count threshold for detecting overrepresented texts (hallucinations)
        var minCountThreshold: Int

        /// Percentage of total segments to use as threshold (0.0-1.0)
        var percentageMultiplier: Double

        /// Position rounding precision for signature matching
        var positionPrecision: Double

        static let `default` = Configuration(
            minCountThreshold: 5,
            percentageMultiplier: 0.1,
            positionPrecision: 100.0
        )
    }

    /// Removes duplicate segments using a two-pass strategy:
    /// 1. First pass: detect overrepresented texts (hallucinations) and keep only first occurrence
    /// 2. Second pass: remove segments with identical text+position signatures
    /// - Parameters:
    ///   - segments: The segments to deduplicate
    ///   - config: Deduplication configuration
    ///   - logger: Optional logging closure for detected hallucinations
    /// - Returns: Deduplicated segments
    static func deduplicate(
        _ segments: [VLMTextSegment],
        config: Configuration = .default,
        logger: ((Int, Int, Int) -> Void)? = nil
    ) -> [VLMTextSegment] {
        guard !segments.isEmpty else { return segments }

        // Deduplicate using text+position signature only.
        // Same-text segments at different positions are legitimate repeated
        // UI elements (buttons, table cells) and must be preserved.
        var seenSignatures = Set<String>()
        var result: [VLMTextSegment] = []

        for segment in segments {
            let signature = segmentSignature(segment, precision: config.positionPrecision)
            if !seenSignatures.contains(signature) {
                seenSignatures.insert(signature)
                result.append(segment)
            }
        }

        return result
    }

    /// Filters out segments from new array that already exist in existing array
    /// - Parameters:
    ///   - existing: Existing segments to check against
    ///   - new: New segments to filter
    ///   - config: Deduplication configuration
    /// - Returns: Filtered segments that don't exist in existing array
    static func filterDuplicates(
        existing: [VLMTextSegment],
        new: [VLMTextSegment],
        config: Configuration = .default
    ) -> [VLMTextSegment] {
        let existingSignatures = Set(existing.map { segmentSignature($0, precision: config.positionPrecision) })
        return new.filter { !existingSignatures.contains(segmentSignature($0, precision: config.positionPrecision)) }
    }

    /// Creates a unique signature for a segment based on text and approximate position
    /// - Parameters:
    ///   - segment: The segment to create a signature for
    ///   - precision: Position rounding precision (e.g., 100 = 2 decimal places)
    /// - Returns: A unique signature string
    private static func segmentSignature(_ segment: VLMTextSegment, precision: Double) -> String {
        let normalizedText = segment.text.trimmingCharacters(in: .whitespacesAndNewlines)
        // Guard against zero or negative precision to prevent division issues
        let safePrecision = max(1.0, precision)
        let roundedX = (segment.boundingBox.x * safePrecision).rounded() / safePrecision
        let roundedY = (segment.boundingBox.y * safePrecision).rounded() / safePrecision
        return "\(normalizedText)|\(roundedX)|\(roundedY)"
    }
}
