import CoreGraphics
import Foundation

/// 按原像素切片，避免 Retina 长图缩放到无法识别的小字。
enum AnalysisImageTiler {
    struct Tile {
        let image: CGImage
        let pixelRect: CGRect
    }

    static func tiles(
        for image: CGImage,
        maxDimension: Int = AnalysisImageEncoder.maxDimension,
        overlap: Int = 48
    ) -> [Tile] {
        let limit = max(1, maxDimension)
        let shouldTileWidth = image.width > limit && image.width * 2 > image.height
        let shouldTileHeight = image.height > limit && image.height * 2 > image.width
        let widthLimit = shouldTileWidth ? limit : image.width
        let heightLimit = shouldTileHeight ? limit : image.height
        let widthStep = max(1, widthLimit - max(0, min(overlap, widthLimit / 4)))
        let heightStep = max(1, heightLimit - max(0, min(overlap, heightLimit / 4)))
        let xs = origins(length: image.width, limit: widthLimit, step: widthStep)
        let ys = origins(length: image.height, limit: heightLimit, step: heightStep)
        return ys.flatMap { y in xs.map { x in
            let rect = CGRect(x: x, y: y, width: min(widthLimit, image.width - x), height: min(heightLimit, image.height - y))
            return Tile(image: image.cropping(to: rect) ?? image, pixelRect: rect)
        } }
    }

    static func merging(_ rects: [CGRect], results: [ScreenAnalysisResult]) throws -> ScreenAnalysisResult {
        guard rects.count == results.count, !results.isEmpty,
              let extent = rects.reduce(nil as CGRect?, { partial, rect in partial?.union(rect) ?? rect }),
              extent.width > 0, extent.height > 0 else {
            throw VLMProviderError.invalidResponse("Incomplete image tiling result")
        }
        var segments: [TextSegment] = []
        for (rect, result) in zip(rects, results) {
            guard result.imageSize == rect.size else {
                throw VLMProviderError.invalidResponse("Image tile dimensions do not match source pixels")
            }
            for segment in result.segments {
                let box = segment.boundingBox
                // 重叠区保留完整落在本块的结果；跨块文本交给包含它的完整块。
                guard box.minX >= 0, box.minY >= 0, box.maxX <= 1.0001, box.maxY <= 1.0001 else {
                    continue
                }
                let mappedBox = CGRect(
                    x: (rect.minX + box.minX * rect.width) / extent.width,
                    y: (rect.minY + box.minY * rect.height) / extent.height,
                    width: box.width * rect.width / extent.width,
                    height: box.height * rect.height / extent.height
                )
                segments.append(TextSegment(
                    id: segment.id, text: segment.text, boundingBox: mappedBox,
                    confidence: segment.confidence, isUserEdited: segment.isUserEdited
                ))
            }
        }
        var deduplicated: [TextSegment] = []
        for segment in segments {
            let text = segment.text.trimmingCharacters(in: .whitespacesAndNewlines)
            let isDuplicate = deduplicated.contains { existing in
                existing.text.trimmingCharacters(in: .whitespacesAndNewlines) == text
                    && overlaps(existing.boundingBox, segment.boundingBox)
            }
            if !isDuplicate { deduplicated.append(segment) }
        }
        return ScreenAnalysisResult(segments: deduplicated, rawSegments: segments, imageSize: extent.size)
    }

    private static func overlaps(_ lhs: CGRect, _ rhs: CGRect) -> Bool {
        let intersection = lhs.intersection(rhs)
        guard !intersection.isNull else { return false }
        let minArea = min(lhs.width * lhs.height, rhs.width * rhs.height)
        return minArea > 0 && (intersection.width * intersection.height) / minArea >= 0.5
    }

    private static func origins(length: Int, limit: Int, step: Int) -> [Int] {
        guard length > limit else { return [0] }
        var origins = Array(stride(from: 0, to: length - limit, by: step))
        origins.append(length - limit)
        return origins
    }
}
