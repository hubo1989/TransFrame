//
//  TranslationProvider.swift
//  TransFrame
//
//  Created for US-009: 扩展 MTransServerProvider 翻译能力
//

import Foundation

// MARK: - Translation Provider Protocol

/// Protocol defining a translation service provider
/// Implementations can wrap different translation APIs (Apple Translation, MTransServer, etc.)
protocol TranslationProvider: Sendable {
    /// Unique identifier for this provider
    var id: String { get }
    
    /// Human-readable name for display
    var name: String { get }
    
    /// Whether the provider is currently available (configured and reachable)
    var isAvailable: Bool { get async }
    
    /// Translate a single text
    /// - Parameters:
    ///   - text: The text to translate
    ///   - sourceLanguage: Source language code (nil for auto-detect)
    ///   - targetLanguage: Target language code
    /// - Returns: Translation result
    func translate(
        text: String,
        from sourceLanguage: String?,
        to targetLanguage: String
    ) async throws -> TranslationResult
    
    /// Translate multiple texts in batch
    /// - Parameters:
    ///   - texts: Array of texts to translate
    ///   - sourceLanguage: Source language code (nil for auto-detect)
    ///   - targetLanguage: Target language code
    /// - Returns: Array of translation results in the same order as input
    func translate(
        texts: [String],
        from sourceLanguage: String?,
        to targetLanguage: String
    ) async throws -> [TranslationResult]
    
    /// Check connection status to the translation service
    /// - Returns: true if the service is reachable and operational
    func checkConnection() async -> Bool

    /// Verify connection status to the translation service
    /// - Throws: An error if the service is unreachable or misconfigured
    func verifyConnection() async throws
}

/// Providers that can execute a translation request with a request-scoped prompt template.
protocol TranslationPromptConfigurable: Sendable {
    func translate(
        texts: [String],
        from sourceLanguage: String?,
        to targetLanguage: String,
        promptTemplate: String?
    ) async throws -> [TranslationResult]
}

/// Providers that can expose prompt-selection context, such as compatible-engine identifiers.
protocol TranslationPromptContextProviding: Sendable {
    func compatiblePromptIdentifier() async -> String?
}

// MARK: - Translation Provider Errors

/// Errors that can occur during translation provider operations
enum TranslationProviderError: LocalizedError, Sendable {
    case notAvailable
    case connectionFailed(String)
    case invalidConfiguration(String)
    case translationFailed(String)
    case incompleteResponse(String)
    case emptyInput
    case unsupportedLanguage(String)
    case timeout
    case rateLimited(retryAfter: TimeInterval?)
    
    var errorDescription: String? {
        switch self {
        case .notAvailable:
            return "Translation provider is not available."
        case .connectionFailed(let message):
            return "Connection failed: \(message)"
        case .invalidConfiguration(let message):
            return "Invalid configuration: \(message)"
        case .translationFailed(let message):
            return "Translation failed: \(message)"
        case .incompleteResponse(let message):
            return "Translation output was incomplete: \(message)"
        case .emptyInput:
            return "Cannot translate empty text."
        case .unsupportedLanguage(let language):
            return "Unsupported language: \(language)"
        case .timeout:
            return "Translation request timed out."
        case .rateLimited(let retryAfter):
            if let seconds = retryAfter {
                return "Rate limited. Retry after \(Int(seconds)) seconds."
            }
            return "Rate limited. Please try again later."
        }
    }

    var recoverySuggestion: String? {
        switch self {
        case .notAvailable:
            return NSLocalizedString("translation.provider.recovery.notAvailable", comment: "")
        case .connectionFailed:
            return NSLocalizedString("translation.provider.recovery.connectionFailed", comment: "")
        case .invalidConfiguration:
            return NSLocalizedString("translation.provider.recovery.invalidConfiguration", comment: "")
        case .translationFailed:
            return NSLocalizedString("translation.provider.recovery.translationFailed", comment: "")
        case .incompleteResponse:
            return "Increase the output token limit or retry with a shorter text."
        case .emptyInput:
            return NSLocalizedString("translation.provider.recovery.emptyInput", comment: "")
        case .unsupportedLanguage:
            return NSLocalizedString("translation.provider.recovery.unsupportedLanguage", comment: "")
        case .timeout:
            return NSLocalizedString("translation.provider.recovery.timeout", comment: "")
        case .rateLimited:
            return NSLocalizedString("translation.provider.recovery.rateLimited", comment: "")
        }
    }
}

// MARK: - Default Implementation

extension TranslationProvider {
    /// Verify connection status to the translation service
    /// - Throws: An error if the service is unreachable or misconfigured
    func verifyConnection() async throws {
        _ = try await translate(text: "1", from: "en", to: "zh")
    }

    /// Check connection status to the translation service
    /// - Returns: true if the service is reachable and operational
    func checkConnection() async -> Bool {
        do {
            try await verifyConnection()
            return true
        } catch {
            return false
        }
    }

    /// Default batch translation implementation that calls single translate sequentially
    /// Providers can override this with more efficient batch implementations
    func translate(
        texts: [String],
        from sourceLanguage: String?,
        to targetLanguage: String
    ) async throws -> [TranslationResult] {
        guard !texts.isEmpty else {
            return []
        }
        
        var results: [TranslationResult] = []
        results.reserveCapacity(texts.count)
        
        for text in texts {
            let result = try await translate(
                text: text,
                from: sourceLanguage,
                to: targetLanguage
            )
            results.append(result)
        }
        
        return results
    }
}
