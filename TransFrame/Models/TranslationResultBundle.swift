//
//  TranslationResultBundle.swift
//  TransFrame
//
//  Bundle containing results from multiple translation engines
//

import Foundation

/// Result from a single translation engine
struct EngineResult: Sendable, Identifiable {
    /// Engine type that produced this result
    let engine: TranslationEngineType

    /// Translated segments
    let segments: [BilingualSegment]

    /// Time taken for translation in seconds
    let latency: TimeInterval

    /// Error if translation failed
    let error: Error?

    /// Unique identifier
    let id: UUID

    init(
        engine: TranslationEngineType,
        segments: [BilingualSegment],
        latency: TimeInterval,
        error: Error? = nil
    ) {
        self.id = UUID()
        self.engine = engine
        self.segments = segments
        self.latency = latency
        self.error = error
    }

    /// Whether this result was successful
    var isSuccess: Bool {
        error == nil && !segments.isEmpty
    }

    /// Create a failed result
    static func failed(engine: TranslationEngineType, error: Error, latency: TimeInterval = 0) -> EngineResult {
        EngineResult(
            engine: engine,
            segments: [],
            latency: latency,
            error: error
        )
    }
}

/// Bundle containing results from multiple translation engines
struct TranslationResultBundle: Sendable {
    /// Results from all attempted engines
    let results: [EngineResult]

    /// The primary engine that was used
    let primaryEngine: TranslationEngineType

    /// Selection mode used for this translation
    let selectionMode: EngineSelectionMode

    /// Scene that triggered this translation
    let scene: TranslationScene?

    /// When the translation was performed
    let timestamp: Date

    init(
        results: [EngineResult],
        primaryEngine: TranslationEngineType,
        selectionMode: EngineSelectionMode,
        scene: TranslationScene? = nil,
        timestamp: Date = Date()
    ) {
        self.results = results
        self.primaryEngine = primaryEngine
        self.selectionMode = selectionMode
        self.scene = scene
        self.timestamp = timestamp
    }

    // MARK: - Computed Properties

    /// Primary result (from the primary engine, falling back to any successful engine)
    var primaryResult: [BilingualSegment] {
        if let primary = results.first(where: { $0.engine == primaryEngine && $0.isSuccess }) {
            return primary.segments
        }
        return results.first(where: { $0.isSuccess })?.segments ?? []
    }

    /// Whether any engine had errors
    var hasErrors: Bool {
        results.contains { $0.error != nil }
    }

    /// Whether all engines failed
    var allFailed: Bool {
        results.allSatisfy { $0.error != nil }
    }

    /// List of engines that succeeded
    var successfulEngines: [TranslationEngineType] {
        results.filter { $0.isSuccess }.map { $0.engine }
    }

    /// List of engines that failed
    var failedEngines: [TranslationEngineType] {
        results.filter { !$0.isSuccess }.map { $0.engine }
    }

    /// Get result for a specific engine
    func result(for engine: TranslationEngineType) -> EngineResult? {
        results.first { $0.engine == engine }
    }

    /// Get segments from a specific engine
    func segments(for engine: TranslationEngineType) -> [BilingualSegment]? {
        result(for: engine)?.segments
    }

    /// Average latency across successful engines
    var averageLatency: TimeInterval {
        let successfulResults = results.filter { $0.isSuccess }
        guard !successfulResults.isEmpty else { return 0 }
        return successfulResults.map(\.latency).reduce(0, +) / Double(successfulResults.count)
    }

    // MARK: - Factory Methods

    /// Create a bundle from a single engine result (backward compatible)
    static func single(
        engine: TranslationEngineType,
        segments: [BilingualSegment],
        latency: TimeInterval,
        selectionMode: EngineSelectionMode = .primaryWithFallback,
        scene: TranslationScene? = nil
    ) -> TranslationResultBundle {
        let result = EngineResult(
            engine: engine,
            segments: segments,
            latency: latency
        )
        return TranslationResultBundle(
            results: [result],
            primaryEngine: engine,
            selectionMode: selectionMode,
            scene: scene
        )
    }

    /// Create a failed bundle
    static func failed(
        engine: TranslationEngineType,
        error: Error,
        selectionMode: EngineSelectionMode = .primaryWithFallback,
        scene: TranslationScene? = nil
    ) -> TranslationResultBundle {
        let result = EngineResult.failed(engine: engine, error: error)
        return TranslationResultBundle(
            results: [result],
            primaryEngine: engine,
            selectionMode: selectionMode,
            scene: scene
        )
    }
}

// MARK: - Error Types for Bundle

/// Errors specific to multi-engine translation
enum MultiEngineError: LocalizedError, Sendable {
    /// All engines failed
    case allEnginesFailed([Error])

    /// No engines configured
    case noEnginesConfigured

    /// Primary engine not available
    case primaryNotAvailable(TranslationEngineType)

    /// No results available
    case noResults

    var errorDescription: String? {
        switch self {
        case .allEnginesFailed(let errors):
            let errorMessages = errors.map { $0.localizedDescription }.joined(separator: "; ")
            return NSLocalizedString(
                "multiengine.error.all_failed",
                comment: "All translation engines failed"
            ) + ": " + errorMessages
        case .noEnginesConfigured:
            return NSLocalizedString(
                "multiengine.error.no_engines",
                comment: "No translation engines are configured"
            )
        case .primaryNotAvailable(let engine):
            return String(
                format: NSLocalizedString(
                    "multiengine.error.primary_unavailable",
                    comment: "Primary engine %@ is not available"
                ),
                engine.localizedName
            )
        case .noResults:
            return NSLocalizedString(
                "multiengine.error.no_results",
                comment: "No translation results available"
            )
        }
    }
}
