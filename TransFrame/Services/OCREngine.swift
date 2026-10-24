import Foundation
import Vision
import CoreGraphics
import os.signpost
import os.log

/// Actor responsible for performing OCR on images using the Vision framework.
/// Thread-safe, async text recognition with support for multiple languages.
actor OCREngine {
    // MARK: - Performance Logging

    private static let performanceLog = OSLog(
        subsystem: Bundle.main.bundleIdentifier ?? "ScreenCapture",
        category: .pointsOfInterest
    )

    private static let signpostID = OSSignpostID(log: performanceLog)

    // MARK: - Properties

    /// Shared instance for app-wide OCR operations
    static let shared = OCREngine()

    // MARK: - Recognition Language

    /// Text recognition languages supported by Vision framework
    enum RecognitionLanguage: String, CaseIterable, Sendable {
        case english = "en-US"
        case chineseSimplified = "zh-Hans"
        case chineseTraditional = "zh-Hant"
        case japanese = "ja-JP"
        case korean = "ko-KR"
        case french = "fr-FR"
        case german = "de-DE"
        case spanish = "es-ES"
        case italian = "it-IT"
        case portuguese = "pt-BR"
        case russian = "ru-RU"
        case arabic = "ar"
        case hindi = "hi-IN"
        case thai = "th-TH"
        case vietnamese = "vi-VN"

        /// The VNRecognizeTextRequest revision for this language
        var visionLanguage: String {
            rawValue
        }

        /// Maps a UI source-language preference to a Vision recognition
        /// language. Returns nil for `.auto` and languages Vision does not
        /// support, letting the caller fall back to automatic detection.
        init?(translationLanguage language: TranslationLanguage) {
            switch language {
            case .auto: return nil
            case .english: self = .english
            case .chineseSimplified: self = .chineseSimplified
            case .chineseTraditional: self = .chineseTraditional
            case .japanese: self = .japanese
            case .korean: self = .korean
            case .french: self = .french
            case .german: self = .german
            case .spanish: self = .spanish
            case .italian: self = .italian
            case .portuguese: self = .portuguese
            case .russian: self = .russian
            case .arabic: self = .arabic
            case .hindi: self = .hindi
            case .thai: self = .thai
            case .vietnamese: self = .vietnamese
            default: return nil
            }
        }

        /// Localized display name
        var localizedName: String {
            switch self {
            case .english: return NSLocalizedString("lang.english", comment: "")
            case .chineseSimplified: return NSLocalizedString("lang.chinese.simplified", comment: "")
            case .chineseTraditional: return NSLocalizedString("lang.chinese.traditional", comment: "")
            case .japanese: return NSLocalizedString("lang.japanese", comment: "")
            case .korean: return NSLocalizedString("lang.korean", comment: "")
            case .french: return NSLocalizedString("lang.french", comment: "")
            case .german: return NSLocalizedString("lang.german", comment: "")
            case .spanish: return NSLocalizedString("lang.spanish", comment: "")
            case .italian: return NSLocalizedString("lang.italian", comment: "")
            case .portuguese: return NSLocalizedString("lang.portuguese", comment: "")
            case .russian: return NSLocalizedString("lang.russian", comment: "")
            case .arabic: return NSLocalizedString("lang.arabic", comment: "")
            case .hindi: return NSLocalizedString("lang.hindi", comment: "")
            case .thai: return NSLocalizedString("lang.thai", comment: "")
            case .vietnamese: return NSLocalizedString("lang.vietnamese", comment: "")
            }
        }
    }

    // MARK: - Configuration

    /// OCR configuration options
    struct Configuration: Sendable {
        /// Recognition languages (empty for auto-detection)
        var languages: Set<RecognitionLanguage>

        /// Minimum confidence threshold (0.0 to 1.0)
        var minimumConfidence: Float

        /// Whether to use automatic language detection
        var useAutoLanguageDetection: Bool

        /// Recognition level (higher = more accurate but slower)
        var recognitionLevel: RecognitionLevel

        /// Whether to prioritize speed over accuracy
        var prefersFastRecognition: Bool

        static let `default` = Configuration(
            languages: [],
            minimumConfidence: 0.0,
            useAutoLanguageDetection: true,
            recognitionLevel: .accurate,
            prefersFastRecognition: false
        )
    }

    /// Recognition accuracy level
    enum RecognitionLevel: Sendable {
        case fast
        case accurate

        var visionLevel: VNRequestTextRecognitionLevel {
            switch self {
            case .fast: return .fast
            case .accurate: return .accurate
            }
        }
    }

    // MARK: - Initialization

    private init() {}

    // MARK: - Public API

    /// Performs OCR on a CGImage with the specified configuration.
    /// - Parameters:
    ///   - image: The image to process
    ///   - config: OCR configuration (uses default if not specified)
    /// - Returns: OCRResult containing all recognized text
    /// - Throws: OCRError if recognition fails
    func recognize(
        _ image: CGImage,
        config: Configuration = .default
    ) async throws -> OCRResult {
        // Validate image
        guard image.width > 0 && image.height > 0 else {
            throw OCREngineError.invalidImage
        }

        let imageSize = CGSize(width: image.width, height: image.height)

        // Create the request
        let request = createRecognitionRequest(config: config)

        // Perform recognition with signpost for profiling
        os_signpost(.begin, log: Self.performanceLog, name: "OCRRecognition", signpostID: Self.signpostID)
        let startTime = CFAbsoluteTimeGetCurrent()

        // VNImageRequestHandler.perform is synchronous and CPU-bound. Run it
        // on a global queue so it doesn't block this actor's executor, and so
        // concurrent recognize() callers don't serialize behind each other.
        // Each call builds its own handler and request; nothing is shared
        // across threads, so the unsafe wrapper only silences Sendable noise.
        let boxedImage = UnsafeSendable(value: image)
        let boxedRequest = UnsafeSendable(value: request)
        let texts: [OCRText]
        do {
            texts = try await withCheckedThrowingContinuation { continuation in
                DispatchQueue.global(qos: .userInitiated).async {
                    let handler = VNImageRequestHandler(cgImage: boxedImage.value, options: [:])
                    do {
                        try handler.perform([boxedRequest.value])
                        let observations = boxedRequest.value.results ?? []
                        continuation.resume(returning: observations.compactMap {
                            OCRText.from($0, imageSize: imageSize)
                        })
                    } catch {
                        continuation.resume(throwing: OCREngineError.recognitionFailed(underlying: error))
                    }
                }
            }
        } catch {
            os_signpost(.end, log: Self.performanceLog, name: "OCRRecognition", signpostID: Self.signpostID)
            throw error
        }

        let duration = (CFAbsoluteTimeGetCurrent() - startTime) * 1000
        os_signpost(.end, log: Self.performanceLog, name: "OCRRecognition", signpostID: Self.signpostID)

        #if DEBUG
        os_log("OCR recognition completed in %.1fms", log: OSLog.default, type: .info, duration)
        #endif

        // Filter by confidence
        let filteredTexts = texts.filter { $0.confidence >= config.minimumConfidence }

        return OCRResult(
            observations: filteredTexts,
            imageSize: imageSize
        )
    }

    /// Performs OCR on a CGImage with automatic language detection.
    /// - Parameter image: The image to process
    /// - Returns: OCRResult containing all recognized text
    /// - Throws: OCRError if recognition fails
    func recognize(_ image: CGImage) async throws -> OCRResult {
        try await recognize(image, config: .default)
    }

    /// Performs OCR with specific languages.
    /// - Parameters:
    ///   - image: The image to process
    ///   - languages: Set of languages to recognize
    /// - Returns: OCRResult containing all recognized text
    /// - Throws: OCRError if recognition fails
    func recognize(
        _ image: CGImage,
        languages: Set<RecognitionLanguage>
    ) async throws -> OCRResult {
        var config = Configuration.default
        config.languages = languages
        config.useAutoLanguageDetection = languages.isEmpty
        return try await recognize(image, config: config)
    }

    // MARK: - Private Methods

    /// Creates a configured VNRecognizeTextRequest
    private func createRecognitionRequest(config: Configuration) -> VNRecognizeTextRequest {
        let request = VNRecognizeTextRequest { _, _ in }

        // Set recognition level
        request.recognitionLevel = config.recognitionLevel.visionLevel

        // With auto detection, leave recognitionLanguages at Vision's default
        if !config.useAutoLanguageDetection {
            request.recognitionLanguages = Array(config.languages).map(\.visionLanguage)
        }

        request.usesLanguageCorrection = true

        return request
    }
}

// MARK: - OCR Engine Errors

/// Errors that can occur during OCR operations
enum OCREngineError: LocalizedError, Sendable {
    /// The provided image is invalid or empty
    case invalidImage

    /// Text recognition failed with an underlying error
    case recognitionFailed(underlying: any Error)

    /// No languages are available for recognition
    case noLanguagesAvailable

    /// The selected OCR engine is not available
    case engineNotAvailable

    var errorDescription: String? {
        switch self {
        case .invalidImage:
            return NSLocalizedString("error.ocr.invalid.image", comment: "")
        case .recognitionFailed:
            return NSLocalizedString("error.ocr.recognition.failed", comment: "")
        case .noLanguagesAvailable:
            return NSLocalizedString("error.ocr.no.languages", comment: "")
        case .engineNotAvailable:
            return NSLocalizedString("error.ocr.engine.not.available", comment: "")
        }
    }

    var recoverySuggestion: String? {
        switch self {
        case .invalidImage:
            return NSLocalizedString("error.ocr.invalid.image.recovery", comment: "")
        case .recognitionFailed:
            return NSLocalizedString("error.ocr.recognition.failed.recovery", comment: "")
        case .noLanguagesAvailable:
            return NSLocalizedString("error.ocr.no.languages.recovery", comment: "")
        case .engineNotAvailable:
            return NSLocalizedString("error.ocr.engine.not.available.recovery", comment: "")
        }
    }
}

/// Wraps a value so it can cross an isolation boundary. Safe here because the
/// wrapped value is created and used exclusively inside a single dispatch
/// closure (`OCREngine.recognize`), never shared between threads.
private struct UnsafeSendable<T>: @unchecked Sendable {
    let value: T
}
