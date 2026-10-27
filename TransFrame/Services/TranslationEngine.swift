import Foundation
import Translation
import NaturalLanguage
import os.signpost
import os.log

// MARK: - Translation Language (Shared Type)

/// Translation languages supported by Translation framework
/// Defined at module level for use in AppSettings without direct coupling
enum TranslationLanguage: String, CaseIterable, Sendable, Codable {
    case auto = "auto"
    case english = "en"
    case chineseSimplified = "zh-Hans"
    case chineseTraditional = "zh-Hant"
    case japanese = "ja"
    case korean = "ko"
    case french = "fr"
    case german = "de"
    case spanish = "es"
    case italian = "it"
    case portuguese = "pt"
    case russian = "ru"
    case arabic = "ar"
    case hindi = "hi"
    case thai = "th"
    case vietnamese = "vi"
    case dutch = "nl"
    case polish = "pl"
    case turkish = "tr"
    case ukrainian = "uk"
    case czech = "cs"
    case swedish = "sv"
    case danish = "da"
    case finnish = "fi"
    case norwegian = "no"
    case greek = "el"
    case hebrew = "he"
    case indonesian = "id"
    case malay = "ms"
    case romanian = "ro"

    /// The Locale.Language identifier for this language.
    /// Uses the full rawValue (BCP 47 tag) to preserve script subtags
    /// (e.g. "zh-Hans", "zh-Hant") which Apple's Translation framework requires.
    var localeLanguage: Locale.Language {
        if self == .auto {
            return Locale.Language(identifier: "en")
        }
        return Locale.Language(identifier: rawValue)
    }

    /// Localized display name
    var localizedName: String {
        Self.displayName(
            for: rawValue,
            locale: .current,
            autoDisplayName: NSLocalizedString("translation.auto", comment: "")
        )
    }

    /// Flag emoji for quick visual pickers. Verified rendering as real flag
    /// glyphs on macOS (not regional-indicator letters).
    var flagEmoji: String {
        switch self {
        case .auto: return "🌐"
        case .english: return "🇬🇧"
        case .chineseSimplified: return "🇨🇳"
        case .chineseTraditional: return "🇹🇼"
        case .japanese: return "🇯🇵"
        case .korean: return "🇰🇷"
        case .french: return "🇫🇷"
        case .german: return "🇩🇪"
        case .spanish: return "🇪🇸"
        case .italian: return "🇮🇹"
        case .portuguese: return "🇵🇹"
        case .russian: return "🇷🇺"
        case .arabic: return "🇸🇦"
        case .hindi: return "🇮🇳"
        case .thai: return "🇹🇭"
        case .vietnamese: return "🇻🇳"
        case .dutch: return "🇳🇱"
        case .polish: return "🇵🇱"
        case .turkish: return "🇹🇷"
        case .ukrainian: return "🇺🇦"
        case .czech: return "🇨🇿"
        case .swedish: return "🇸🇪"
        case .danish: return "🇩🇰"
        case .finnish: return "🇫🇮"
        case .norwegian: return "🇳🇴"
        case .greek: return "🇬🇷"
        case .hebrew: return "🇮🇱"
        case .indonesian: return "🇮🇩"
        case .malay: return "🇲🇾"
        case .romanian: return "🇷🇴"
        }
    }

    /// BCP 47 language tag
    var bcp47Tag: String {
        rawValue
    }

    static func fromTranslationCode(_ code: String?) -> TranslationLanguage? {
        guard let code,
              !code.isEmpty,
              code.lowercased() != TranslationLanguage.auto.rawValue else {
            return nil
        }

        // Normalize underscores to hyphens
        let normalized = code.replacingOccurrences(of: "_", with: "-")

        // Exact match
        if let match = TranslationLanguage(rawValue: normalized) {
            return match
        }

        // Fuzzy match: map bare language codes (e.g. "zh") to their default script
        let lowercased = normalized.lowercased()
        switch lowercased {
        case "zh", "zh-cn", "zh-sg":
            return .chineseSimplified
        case "zh-tw", "zh-hk", "zh-mo":
            return .chineseTraditional
        default:
            break
        }

        // Prefix match (e.g. "en-US" → .english, "zh-Hans-CN" → .chineseSimplified)
        return TranslationLanguage.allCases.first(where: { lowercased.hasPrefix($0.rawValue.lowercased()) })
    }

    static func promptDisplayName(for code: String?) -> String {
        displayName(
            for: code,
            locale: Locale(identifier: "en"),
            autoDisplayName: "Auto Detect"
        )
    }

    static func displayName(
        for code: String?,
        locale: Locale,
        autoDisplayName: String
    ) -> String {
        guard let code,
              !code.isEmpty,
              code.lowercased() != TranslationLanguage.auto.rawValue else {
            return autoDisplayName
        }

        let normalized = code.replacingOccurrences(of: "_", with: "-")

        if let fullName = locale.localizedString(forIdentifier: normalized), !fullName.isEmpty {
            return normalizedDisplayName(fullName)
        }

        let baseLanguageCode = normalized.components(separatedBy: "-").first ?? normalized
        if let languageName = locale.localizedString(forLanguageCode: baseLanguageCode),
           !languageName.isEmpty {
            return normalizedDisplayName(languageName)
        }

        return normalized
    }

    private static func normalizedDisplayName(_ name: String) -> String {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        let commaSeparatedParts = trimmed
            .split(separator: ",", maxSplits: 1, omittingEmptySubsequences: true)
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }

        guard commaSeparatedParts.count == 2,
              !commaSeparatedParts[0].isEmpty,
              !commaSeparatedParts[1].isEmpty else {
            return trimmed
        }

        return "\(commaSeparatedParts[0]) (\(commaSeparatedParts[1]))"
    }
}

/// Actor responsible for translating text using the Translation framework (macOS 12+).
/// Thread-safe, async translation with support for multiple languages.
@available(macOS 13.0, *)
actor TranslationEngine {
    // MARK: - Performance Logging

    private static let performanceLog = OSLog(
        subsystem: Bundle.main.bundleIdentifier ?? "ScreenCapture",
        category: .pointsOfInterest
    )

    private static let signpostID = OSSignpostID(log: performanceLog)

    // MARK: - Properties

    /// Shared instance for app-wide translation operations
    static let shared = TranslationEngine()

    /// Whether a translation operation is currently in progress
    private var isProcessing = false

    // MARK: - Internal Error Types

    private struct TranslationTimeout: Error {}

    // MARK: - Configuration

    /// Translation configuration options
    struct Configuration: Sendable {
        /// Explicit source language for translation. nil means fallback behavior.
        var sourceLanguage: TranslationLanguage?

        /// Target language for translation (nil for system default)
        var targetLanguage: TranslationLanguage?

        /// Request timeout in seconds
        var timeout: TimeInterval

        /// Whether to automatically detect source language
        var autoDetectSourceLanguage: Bool

        static let `default` = Configuration(
            sourceLanguage: nil,
            targetLanguage: nil,
            timeout: 10.0,
            autoDetectSourceLanguage: true
        )
    }

    // MARK: - Initialization

    private init() {}

    // MARK: - Public API

    /// Translates text using the Translation framework.
    /// - Parameters:
    ///   - text: The text to translate
    ///   - config: Translation configuration (uses default if not specified)
    /// - Returns: TranslationResult containing translated text
    /// - Throws: TranslationEngineError if translation fails
    func translate(
        _ text: String,
        config: Configuration = .default
    ) async throws -> TranslationResult {
        // Prevent concurrent translation operations
        guard !isProcessing else {
            throw TranslationEngineError.operationInProgress
        }
        isProcessing = true
        defer { isProcessing = false }

        // Validate input
        guard !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            throw TranslationEngineError.emptyInput
        }

        // Determine target language (auto means use system default)
        let effectiveTargetLanguage: TranslationLanguage
        if let target = config.targetLanguage, target != .auto {
            effectiveTargetLanguage = target
        } else {
            effectiveTargetLanguage = Self.systemTargetLanguage()
        }

        // Perform translation with signpost for profiling
        os_signpost(.begin, log: Self.performanceLog, name: "Translation", signpostID: Self.signpostID)
        let startTime = CFAbsoluteTimeGetCurrent()

        let detectedSource = config.sourceLanguage ?? Self.detectLanguage(for: text)
        if let detectedSource, detectedSource == effectiveTargetLanguage {
            let duration = (CFAbsoluteTimeGetCurrent() - startTime) * 1000
            os_signpost(.end, log: Self.performanceLog, name: "Translation", signpostID: Self.signpostID)
            #if DEBUG
            os_log("Translation skipped (source == target) in %.1fms", log: OSLog.default, type: .info, duration)
            #endif
            return TranslationResult(
                sourceText: text,
                translatedText: text,
                sourceLanguage: detectedSource.rawValue,
                targetLanguage: effectiveTargetLanguage.rawValue
            )
        }

        do {
            let response = try await performTranslation(
                text: text,
                source: config.sourceLanguage,
                target: effectiveTargetLanguage,
                timeout: config.timeout
            )

            let duration = (CFAbsoluteTimeGetCurrent() - startTime) * 1000
            os_signpost(.end, log: Self.performanceLog, name: "Translation", signpostID: Self.signpostID)

            #if DEBUG
            os_log("Translation completed in %.1fms", log: OSLog.default, type: .info, duration)
            #endif

            return TranslationResult(
                sourceText: response.sourceText,
                translatedText: response.targetText,
                sourceLanguage: response.sourceLanguage.minimalIdentifier,
                targetLanguage: response.targetLanguage.minimalIdentifier
            )
        } catch {
            os_signpost(.end, log: Self.performanceLog, name: "Translation", signpostID: Self.signpostID)
            throw mapTranslationError(error, targetLanguage: effectiveTargetLanguage)
        }
    }

    /// Translates text with automatic language detection.
    /// - Parameter text: The text to translate
    /// - Returns: TranslationResult containing translated text
    /// - Throws: TranslationEngineError if translation fails
    func translate(_ text: String) async throws -> TranslationResult {
        try await translate(text, config: .default)
    }

    /// Translates text to a specific target language.
    /// - Parameters:
    ///   - text: The text to translate
    ///   - targetLanguage: The target translation language
    /// - Returns: TranslationResult containing translated text
    /// - Throws: TranslationEngineError if translation fails
    func translate(
        _ text: String,
        to targetLanguage: TranslationLanguage
    ) async throws -> TranslationResult {
        var config = Configuration.default
        config.targetLanguage = targetLanguage
        return try await translate(text, config: config)
    }

    // MARK: - Private Methods

    /// Validates if the target language is available and installed
    private func validateLanguageAvailability(for language: TranslationLanguage) async throws {
        try await validateLanguageAvailability(for: language, sourceLanguage: nil, text: nil)
    }

    /// Validates if the target language is available and installed for the selected source language.
    private func validateLanguageAvailability(
        for language: TranslationLanguage,
        sourceLanguage: TranslationLanguage?,
        text: String?
    ) async throws {
        let languageStatus: LanguageAvailabilityStatus

        if let sourceLocaleLanguage = Self.sourceLocaleLanguage(for: sourceLanguage) {
            languageStatus = await Self.checkLanguageAvailability(
                source: sourceLocaleLanguage,
                target: language.localeLanguage
            )
        } else if let text, !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            languageStatus = await Self.checkLanguageAvailability(
                text: text,
                target: language.localeLanguage
            )
        } else {
            return
        }

        switch languageStatus {
        case .installed:
            break
        case .supported(let languageName):
            throw TranslationEngineError.languageNotInstalled(
                language: languageName,
                downloadInstructions: NSLocalizedString(
                    "error.translation.language.download.instructions",
                    comment: ""
                )
            )
        case .unsupported(let languageName):
            throw TranslationEngineError.unsupportedLanguagePair(
                source: NSLocalizedString("translation.auto.detected", comment: ""),
                target: languageName
            )
        }
    }

    /// Performs the actual translation with a timeout
    private func performTranslation(
        text: String,
        source: TranslationLanguage?,
        target: TranslationLanguage,
        timeout: TimeInterval
    ) async throws -> TranslationSession.Response {
        try await withThrowingTaskGroup(
            of: Result<TranslationSession.Response, any Error>.self
        ) { group in
            group.addTask { [text, source, target] in
                do {
                    let detectedSource = source ?? Self.detectLanguage(for: text) ?? .english
                    let session = TranslationSession(
                        installedSource: detectedSource.localeLanguage,
                        target: target.localeLanguage
                    )
                    let result = try await session.translate(text)
                    return .success(result)
                } catch {
                    return .failure(error)
                }
            }

            _ = group.addTaskUnlessCancelled {
                try await Task.sleep(nanoseconds: UInt64(timeout * 1_000_000_000))
                return .failure(TranslationTimeout())
            }

            guard let result = try await group.next() else {
                throw TranslationTimeout()
            }
            group.cancelAll()
            return try result.get()
        }
    }

    /// Maps internal and framework errors to TranslationEngineError
    private func mapTranslationError(_ error: Error, targetLanguage: TranslationLanguage) -> Error {
        if error is TranslationTimeout {
            return TranslationEngineError.timeout
        }

        let nsError = error as NSError
        if nsError.domain == "TranslationErrorDomain" || nsError.domain.contains("Translation") {
            let desc = nsError.localizedDescription.lowercased()
            if nsError.code == 16 || nsError.code == 5 || desc.contains("offline models") || desc.contains("not installed") || desc.contains("not available") {
                return TranslationEngineError.languageNotInstalled(
                    language: targetLanguage.localizedName,
                    downloadInstructions: NSLocalizedString(
                        "error.translation.language.download.instructions",
                        comment: ""
                    )
                )
            }
            return TranslationEngineError.translationFailed(underlying: nsError)
        }

        return TranslationEngineError.translationFailed(underlying: error)
    }

    /// Detects the language of the given text using NaturalLanguage framework.
    private static func detectLanguage(for text: String) -> TranslationLanguage? {
        guard let dominantLanguage = NLLanguageRecognizer.dominantLanguage(for: text) else {
            return nil
        }
        return TranslationLanguage.fromTranslationCode(dominantLanguage.rawValue)
    }

    /// Returns the system's target language based on user preferences
    private static func systemTargetLanguage() -> TranslationLanguage {
        let systemLanguage = Locale.current.language.languageCode?.identifier ?? "en"
        let systemRegion = Locale.current.language.region?.identifier ?? ""

        let bcp47 = systemRegion.isEmpty ? systemLanguage : "\(systemLanguage)-\(systemRegion)"

        // Find exact match
        if let match = TranslationLanguage(rawValue: bcp47) {
            return match
        }

        // Try language-only match
        if let match = TranslationLanguage.allCases.first(where: { $0.rawValue.hasPrefix(systemLanguage) }) {
            return match
        }

        return .english
    }

    /// Checks if a language pair is supported for translation
    func isLanguagePairSupported(
        source: TranslationLanguage,
        target: TranslationLanguage
    ) -> Bool {
        // Auto is always valid (will use system default)
        guard source != .auto, target != .auto else { return true }

        // Most common pairs are supported; this is a simplified check
        return source != target
    }

    // MARK: - Language Availability

    /// Represents the availability status of a translation language
    enum LanguageAvailabilityStatus {
        case installed
        case supported(languageName: String)
        case unsupported(languageName: String)
    }

    /// Checks if the target language is available for translation
    /// - Parameter target: The target language to check
    /// - Returns: The availability status of the language
    static func sourceLocaleLanguage(for sourceLanguage: TranslationLanguage?) -> Locale.Language? {
        guard let sourceLanguage, sourceLanguage != .auto else {
            return nil
        }
        return sourceLanguage.localeLanguage
    }

    private static func checkLanguageAvailability(
        source: Locale.Language,
        target: Locale.Language
    ) async -> LanguageAvailabilityStatus {
        let availability = LanguageAvailability()
        let status = await availability.status(from: source, to: target)
        return languageAvailabilityStatus(from: status, target: target)
    }

    private static func checkLanguageAvailability(
        text: String,
        target: Locale.Language
    ) async -> LanguageAvailabilityStatus {
        let availability = LanguageAvailability()
        let status: LanguageAvailability.Status
        do {
            status = try await availability.status(for: text, to: target)
        } catch {
            return .unsupported(languageName: fullIdentifier(for: target))
        }
        return languageAvailabilityStatus(from: status, target: target)
    }

    /// Builds a full BCP 47 identifier from a Locale.Language (e.g. "zh-Hans")
    /// Unlike minimalIdentifier which strips script/region to just "zh"
    private static func fullIdentifier(for language: Locale.Language) -> String {
        var components: [String] = []
        if let code = language.languageCode?.identifier { components.append(code) }
        if let script = language.script?.identifier { components.append(script) }
        return components.joined(separator: "-")
    }

    private static func languageAvailabilityStatus(
        from status: LanguageAvailability.Status,
        target: Locale.Language
    ) -> LanguageAvailabilityStatus {
        // Build full identifier (e.g. "zh-Hans") instead of minimalIdentifier ("zh")
        let languageName = fullIdentifier(for: target)

        switch status {
        case .installed:
            return .installed
        case .supported:
            return .supported(languageName: languageName)
        case .unsupported:
            return .unsupported(languageName: languageName)
        @unknown default:
            return .unsupported(languageName: languageName)
        }
    }
}

// MARK: - Translation Engine Errors

/// Errors that can occur during translation operations
enum TranslationEngineError: LocalizedError, Sendable {
    /// Translation operation is already in progress
    case operationInProgress

    /// The input text is empty
    case emptyInput

    /// Translation operation timed out
    case timeout

    /// The requested language pair is not supported
    case unsupportedLanguagePair(source: String, target: String)

    /// Translation language not installed (needs download)
    case languageNotInstalled(language: String, downloadInstructions: String)

    /// Translation failed with an underlying error
    case translationFailed(underlying: any Error)

    var errorDescription: String? {
        switch self {
        case .operationInProgress:
            return NSLocalizedString("error.translation.in.progress", comment: "")
        case .emptyInput:
            return NSLocalizedString("error.translation.empty.input", comment: "")
        case .timeout:
            return NSLocalizedString("error.translation.timeout", comment: "")
        case .unsupportedLanguagePair(let source, let target):
            return String(format: NSLocalizedString("error.translation.unsupported.pair", comment: ""), source, target)
        case .languageNotInstalled(let language, _):
            return String(format: NSLocalizedString("error.translation.language.not.installed", comment: ""), language)
        case .translationFailed(let underlying):
            let underlyingDesc = underlying.localizedDescription
            return "\(NSLocalizedString("error.translation.failed", comment: "")): \(underlyingDesc)"
        }
    }

    var recoverySuggestion: String? {
        switch self {
        case .operationInProgress:
            return NSLocalizedString("error.translation.in.progress.recovery", comment: "")
        case .emptyInput:
            return NSLocalizedString("error.translation.empty.input.recovery", comment: "")
        case .timeout:
            return NSLocalizedString("error.translation.timeout.recovery", comment: "")
        case .unsupportedLanguagePair:
            return NSLocalizedString("error.translation.unsupported.pair.recovery", comment: "")
        case .languageNotInstalled(_, let instructions):
            return instructions
        case .translationFailed:
            return NSLocalizedString("error.translation.failed.recovery", comment: "")
        }
    }
}
