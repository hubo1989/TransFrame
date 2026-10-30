//
//  TextTranslationFlow.swift
//  TransFrame
//
//  Created for US-003: Create TextTranslationFlow for plain text translation
//

import Foundation
import os.log

/// Flow phase for plain text translation
enum TextTranslationPhase: Sendable, Equatable {
    case idle
    case translating
    case completed
    case failed(TextTranslationError)

    var isProcessing: Bool {
        self == .translating
    }

    var localizedDescription: String {
        switch self {
        case .idle:
            return String(localized: "textTranslation.phase.idle")
        case .translating:
            return String(localized: "textTranslation.phase.translating")
        case .completed:
            return String(localized: "textTranslation.phase.completed")
        case .failed:
            return String(localized: "textTranslation.phase.failed")
        }
    }
}

/// Errors that can occur during text translation
enum TextTranslationError: LocalizedError, Sendable, Equatable {
    /// The input text is empty
    case emptyInput
    /// Translation failed with underlying error
    case translationFailed(String)
    /// The operation was cancelled
    case cancelled
    /// Translation service is not available
    case serviceUnavailable

    var errorDescription: String? {
        switch self {
        case .emptyInput:
            return String(localized: "textTranslation.error.emptyInput")
        case .translationFailed(let message):
            return String(format: NSLocalizedString("textTranslation.error.translationFailed", comment: ""), message)
        case .cancelled:
            return String(localized: "textTranslation.error.cancelled")
        case .serviceUnavailable:
            return String(localized: "textTranslation.error.serviceUnavailable")
        }
    }

    var recoverySuggestion: String? {
        switch self {
        case .emptyInput:
            return String(localized: "textTranslation.recovery.emptyInput")
        case .translationFailed:
            return String(localized: "textTranslation.recovery.translationFailed")
        case .cancelled:
            return nil
        case .serviceUnavailable:
            return String(localized: "textTranslation.recovery.serviceUnavailable")
        }
    }
}

/// Per-engine translation result for parallel/multi-engine display
struct EngineTranslationInfo: Sendable, Identifiable {
    let id = UUID()
    let engine: TranslationEngineType
    let translatedText: String?
    let errorMessage: String?
    let latency: TimeInterval

    var isSuccess: Bool { translatedText != nil && !(translatedText?.isEmpty ?? true) }

    static func fromEngineResult(_ result: EngineResult) -> EngineTranslationInfo {
        EngineTranslationInfo(
            engine: result.engine,
            translatedText: result.segments.first?.translated,
            errorMessage: result.error?.localizedDescription,
            latency: result.latency
        )
    }
}

/// Result of a plain text translation operation
struct TextTranslationResult: Sendable {
    /// The original text that was translated
    let originalText: String
    /// The translated text (from first successful engine)
    let translatedText: String
    /// Detected or specified source language
    let sourceLanguage: String?
    /// Target language for translation
    let targetLanguage: String
    /// Bilingual segments (single segment for plain text)
    let segments: [BilingualSegment]
    /// Processing time in seconds
    let processingTime: TimeInterval
    /// Per-engine results (populated for parallel mode; empty for single-engine modes)
    let engineResults: [EngineTranslationInfo]
    /// Whether this result contains multiple engine results
    var hasMultipleResults: Bool { engineResults.count > 1 }
}

/// Configuration for text translation
struct TextTranslationConfig: Sendable {
    /// Target language code
    let targetLanguage: String
    /// Source language code (nil for auto-detect)
    let sourceLanguage: String?
    /// Preferred translation engine
    let preferredEngine: TranslationEngineType
    /// Translation scene for prompt selection and scene bindings
    let scene: TranslationScene?
    /// Engine selection mode for the request
    let mode: EngineSelectionMode
    /// Whether fallback is enabled for the request
    let fallbackEnabled: Bool
    /// Engines participating in parallel/quick-switch modes
    let parallelEngines: [TranslationEngineType]
    /// Scene routing bindings active for the request
    let sceneBindings: [TranslationScene: SceneEngineBinding]
    /// V2 selection: ordered chain or compare set, active when engineModeV2
    /// routes the request (Phase 4). nil keeps legacy dispatch.
    var v2Selection: (mode: EngineModeV2, entries: [EngineChainItem])? = nil

    /// Default configuration using common settings
    static let `default` = TextTranslationConfig(
        targetLanguage: "zh-Hans",
        sourceLanguage: nil,
        preferredEngine: .apple,
        scene: nil,
        mode: .primaryWithFallback,
        fallbackEnabled: true,
        parallelEngines: [],
        sceneBindings: [:]
    )
}

/// Handles plain text translation without OCR/image analysis.
/// Reuses existing TranslationService for actual translation.
@available(macOS 13.0, *)
actor TextTranslationFlow {

    // MARK: - Properties

    /// Shared instance for convenience
    static let shared = TextTranslationFlow()

    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "TransFrame",
        category: "TextTranslationFlow"
    )

    /// Current translation phase
    private(set) var currentPhase: TextTranslationPhase = .idle

    /// Last translation error (if any)
    private(set) var lastError: TextTranslationError?

    /// Last successful translation result
    private(set) var lastResult: TextTranslationResult?

    /// Current translation task (for cancellation)
    private var currentTask: Task<TextTranslationResult, Error>?

    /// Translation-only pipeline used to execute and measure the underlying work.
    private let translationRenderPipeline: TranslationRenderPipeline

    // MARK: - Initialization

    init(
        service: any TranslationServicing = TranslationService.shared,
        performanceRecorder: PerformanceRecorder = .shared
    ) {
        self.translationRenderPipeline = TranslationRenderPipeline(
            translationService: service,
            recorder: performanceRecorder
        )
    }

    // MARK: - Public API

    /// Translates plain text with explicit configuration.
    /// - Parameters:
    ///   - text: The text to translate
    ///   - config: Translation configuration
    /// - Returns: TextTranslationResult with translation details
    func translate(
        _ text: String,
        config: TextTranslationConfig
    ) async throws -> TextTranslationResult {
        // Cancel any ongoing translation
        cancel()

        // Validate input
        let trimmedText = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedText.isEmpty else {
            currentPhase = .failed(.emptyInput)
            lastError = .emptyInput
            throw TextTranslationError.emptyInput
        }

        currentPhase = .translating
        lastError = nil

        let startTime = Date()
        let context = PipelineContext(startedAt: startTime)

        let task = Task<TextTranslationResult, Error> {
            logger.info("Starting text translation: \(trimmedText.count) chars to \(config.targetLanguage)")

            let bundle = try await translationRenderPipeline.translateBundle(
                text: trimmedText,
                config: config,
                context: context
            )

            // Collect per-engine results for display
            let engineInfos = bundle.results.map { EngineTranslationInfo.fromEngineResult($0) }

            // Use first successful result (any engine)
            guard let firstSuccess = bundle.results.first(where: { $0.isSuccess }),
                  let firstSegment = firstSuccess.segments.first else {
                // All engines failed — build descriptive error
                let errorSummaries = bundle.results.compactMap { result -> String? in
                    guard let error = result.error else { return nil }
                    return "\(result.engine.localizedName): \(error.localizedDescription)"
                }
                let message = errorSummaries.joined(separator: "\n")
                throw TextTranslationError.translationFailed(message)
            }

            let processingTime = Date().timeIntervalSince(startTime)

            return TextTranslationResult(
                originalText: trimmedText,
                translatedText: firstSegment.translated,
                sourceLanguage: firstSegment.sourceLanguage,
                targetLanguage: firstSegment.targetLanguage,
                segments: firstSuccess.segments,
                processingTime: processingTime,
                engineResults: engineInfos
            )
        }

        currentTask = task

        do {
            let result = try await task.value

            lastResult = result
            currentPhase = .completed
            currentTask = nil

            logger.info("Text translation completed in \(result.processingTime * 1000)ms")

            return result

        } catch is CancellationError {
            currentPhase = .failed(.cancelled)
            lastError = .cancelled
            currentTask = nil
            throw TextTranslationError.cancelled
        } catch let error as TextTranslationError {
            currentPhase = .failed(error)
            lastError = error
            currentTask = nil
            throw error
        } catch let error as PipelineError {
            let textError: TextTranslationError
            switch error {
            case .cancelled:
                textError = .cancelled
            default:
                let errorMessage = error.errorDescription ?? error.localizedDescription
                textError = .translationFailed(errorMessage)
            }
            currentPhase = .failed(textError)
            lastError = textError
            currentTask = nil
            throw textError
        } catch {
            let errorMessage = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
            logger.error("Text translation failed: \(errorMessage)")
            let translationError = TextTranslationError.translationFailed(errorMessage)
            currentPhase = .failed(translationError)
            lastError = translationError
            currentTask = nil
            throw translationError
        }
    }

    /// Cancels any ongoing translation operation
    func cancel() {
        currentTask?.cancel()
        currentTask = nil

        if currentPhase == .translating {
            currentPhase = .failed(.cancelled)
            lastError = .cancelled
        }
    }

    /// Resets the flow to idle state
    func reset() {
        cancel()
        currentPhase = .idle
        lastError = nil
        lastResult = nil
    }

    // MARK: - Convenience Methods

    /// Translates text and returns just the translated string.
    /// Useful for quick translations without full result details.
    func translateText(_ text: String, config: TextTranslationConfig = .default) async throws -> String {
        let result = try await translate(text, config: config)
        return result.translatedText
    }

    /// Translates text to a specific target language.
    func translate(
        _ text: String,
        to targetLanguage: String,
        from sourceLanguage: String? = nil,
        preferredEngine: TranslationEngineType = .apple
    ) async throws -> TextTranslationResult {
        let config = TextTranslationConfig(
            targetLanguage: targetLanguage,
            sourceLanguage: sourceLanguage,
            preferredEngine: preferredEngine,
            scene: nil,
            mode: .primaryWithFallback,
            fallbackEnabled: true,
            parallelEngines: [],
            sceneBindings: [:]
        )
        return try await translate(text, config: config)
    }
}

// MARK: - AppSettings Integration

extension TextTranslationConfig {
    /// Creates a configuration from current AppSettings.
    /// Must be called from @MainActor context.
    @MainActor
    static func fromAppSettings() -> TextTranslationConfig {
        let settings = AppSettings.shared
        let targetLanguage = settings.translationTargetLanguage?.rawValue ?? "zh-Hans"
        let sourceLanguage: String? = settings.translationSourceLanguage == .auto ? nil : settings.translationSourceLanguage.rawValue

        var config = TextTranslationConfig(
            targetLanguage: targetLanguage,
            sourceLanguage: sourceLanguage,
            preferredEngine: settings.engineChainV2.first?.resolvedEngine ?? .apple,
            scene: .textSelection,
            mode: .primaryWithFallback,
            fallbackEnabled: true,
            parallelEngines: [],
            sceneBindings: [:]
        )
        config.v2Selection = (settings.engineModeV2, settings.engineModeV2 == .ordered
            ? settings.engineChainV2
            : settings.compareEnginesV2)
        return config
    }

    /// Creates a configuration specifically for translate and insert functionality.
    /// Uses separate language settings from global translation settings.
    /// Must be called from @MainActor context.
    @MainActor
    static func forTranslateAndInsert() -> TextTranslationConfig {
        let settings = AppSettings.shared
        let targetLanguage = settings.translateAndInsertTargetLanguage?.rawValue ?? "zh-Hans"
        let sourceLanguage: String? = settings.translateAndInsertSourceLanguage == .auto ? nil : settings.translateAndInsertSourceLanguage.rawValue
        let preferredEngine = settings.engineChainV2.first?.resolvedEngine ?? .apple

        #if DEBUG
        Logger.translation.debug(
            """
            Translate-and-insert config resolved: \
            target=\(targetLanguage, privacy: .public), \
            source=\(sourceLanguage ?? "auto", privacy: .public), \
            engine=\(preferredEngine.rawValue, privacy: .public)
            """
        )
        #endif

        var config = TextTranslationConfig(
            targetLanguage: targetLanguage,
            sourceLanguage: sourceLanguage,
            preferredEngine: settings.engineChainV2.first?.resolvedEngine ?? .apple,
            scene: .translateAndInsert,
            mode: .primaryWithFallback,
            fallbackEnabled: true,
            parallelEngines: [],
            sceneBindings: [:]
        )
        // Phase 6: translate-and-insert follows the ordered chain (compare
        // makes no sense for a one-shot insertion target).
        config.v2Selection = (.ordered, settings.engineChainV2)
        return config
    }
}
