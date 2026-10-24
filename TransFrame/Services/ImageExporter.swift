import Foundation
import CoreGraphics
import AppKit
import UniformTypeIdentifiers

/// Service for exporting screenshots to PNG or JPEG files.
/// Uses CGImageDestination for efficient image encoding.
struct ImageExporter: Sendable {
    // MARK: - Constants

    private static let dateFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd 'at' HH.mm.ss"
        return formatter
    }()

    // MARK: - Public API

    func save(
        _ image: CGImage,
        annotations: [Annotation],
        to url: URL,
        format: ExportFormat,
        quality: Double = 0.9,
        beautifyConfig: BeautifyConfig? = nil
    ) throws {
        var finalImage: CGImage
        if annotations.isEmpty {
            finalImage = image
        } else {
            finalImage = try compositeAnnotations(annotations, onto: image)
        }

        if let bConfig = beautifyConfig, bConfig.isEnabled {
            if let beautified = BeautifyRenderer.shared.render(image: finalImage, config: bConfig) {
                finalImage = beautified
            }
        }

        try writeImage(finalImage, to: url, format: format, quality: quality)
    }

    func generateFilename(format: ExportFormat) -> String {
        let timestamp = Self.dateFormatter.string(from: Date())
        return "Screenshot \(timestamp).\(format.fileExtension)"
    }

    func generateFileURL(in directory: URL, format: ExportFormat) -> URL {
        let filename = generateFilename(format: format)
        var url = directory.appendingPathComponent(filename)

        var counter = 1
        while FileManager.default.fileExists(atPath: url.path) {
            let baseName = "Screenshot \(Self.dateFormatter.string(from: Date())) (\(counter))"
            url = directory.appendingPathComponent("\(baseName).\(format.fileExtension)")
            counter += 1
        }

        return url
    }

    func estimateFileSize(
        for image: CGImage,
        format: ExportFormat,
        quality: Double = 0.9
    ) -> Int {
        let pixelCount = image.width * image.height

        switch format {
        case .png:
            return pixelCount * 4
        case .jpeg:
            let bytesPerPixel = 0.5 + (0.5 * quality)
            return Int(Double(pixelCount) * bytesPerPixel)
        case .heic:
            let bytesPerPixel = 0.3 + (0.3 * quality)
            return Int(Double(pixelCount) * bytesPerPixel)
        }
    }

    // MARK: - Annotation Compositing

    func compositeAnnotations(
        _ annotations: [Annotation],
        onto image: CGImage
    ) throws -> CGImage {
        let width = image.width
        let height = image.height

        guard let colorSpace = image.colorSpace ?? CGColorSpace(name: CGColorSpace.sRGB),
              let context = CGContext(
                  data: nil,
                  width: width,
                  height: height,
                  bitsPerComponent: 8,
                  bytesPerRow: 0,
                  space: colorSpace,
                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
              ) else {
            throw TransFrameError.exportEncodingFailed(format: .png)
        }

        context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
        context.setLineCap(.round)
        context.setLineJoin(.round)

        for annotation in annotations {
            renderAnnotation(annotation, in: context, imageHeight: CGFloat(height), sourceImage: image)
        }

        guard let result = context.makeImage() else {
            throw TransFrameError.exportEncodingFailed(format: .png)
        }

        return result
    }

    /// Renders composited annotations and optional beautification to a final CGImage.
    func renderFinalImage(
        _ image: CGImage,
        annotations: [Annotation],
        beautifyConfig: BeautifyConfig? = nil
    ) throws -> CGImage {
        var finalImage: CGImage
        if annotations.isEmpty {
            finalImage = image
        } else {
            finalImage = try compositeAnnotations(annotations, onto: image)
        }

        if let bConfig = beautifyConfig, bConfig.isEnabled {
            if let beautified = BeautifyRenderer.shared.render(image: finalImage, config: bConfig) {
                finalImage = beautified
            }
        }
        return finalImage
    }

    // MARK: - Save with Translations

    func saveWithTranslations(
        _ image: CGImage,
        annotations: [Annotation],
        ocrResult: OCRResult?,
        translations: [TranslationResult],
        to url: URL,
        format: ExportFormat,
        quality: Double = 0.9
    ) throws {
        var finalImage = image

        if !annotations.isEmpty {
            finalImage = try compositeAnnotations(annotations, onto: finalImage)
        }

        if let ocrResult = ocrResult, !translations.isEmpty {
            finalImage = try compositeTranslations(finalImage, ocrResult: ocrResult, translations: translations)
        }

        try writeImage(finalImage, to: url, format: format, quality: quality)
    }

    // MARK: - Private Helpers

    private func writeImage(
        _ image: CGImage,
        to url: URL,
        format: ExportFormat,
        quality: Double
    ) throws {
        let directory = url.deletingLastPathComponent()
        guard FileManager.default.isWritableFile(atPath: directory.path) else {
            throw TransFrameError.invalidSaveLocation(directory)
        }

        let estimatedSize = Int64(image.width * image.height * 4)
        do {
            let resourceValues = try directory.resourceValues(forKeys: [.volumeAvailableCapacityKey])
            if let availableCapacity = resourceValues.volumeAvailableCapacity,
               Int64(availableCapacity) < estimatedSize {
                throw TransFrameError.diskFull
            }
        } catch let error as TransFrameError {
            throw error
        } catch {
            // Ignore disk space check errors, proceed with save
        }

        guard let destination = CGImageDestinationCreateWithURL(
            url as CFURL,
            format.uti.identifier as CFString,
            1,
            nil
        ) else {
            throw TransFrameError.exportEncodingFailed(format: format)
        }

        var options: [CFString: Any] = [:]
        if format == .jpeg || format == .heic {
            options[kCGImageDestinationLossyCompressionQuality] = quality
        }

        CGImageDestinationAddImage(destination, image, options as CFDictionary)

        guard CGImageDestinationFinalize(destination) else {
            throw TransFrameError.exportEncodingFailed(format: format)
        }
    }
}

// MARK: - Shared Instance

extension ImageExporter {
    static let shared = ImageExporter()
}
