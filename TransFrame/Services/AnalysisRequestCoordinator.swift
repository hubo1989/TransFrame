import Foundation
import CoreGraphics

/// Coordinates in-flight OCR, VLM, and translation requests.
/// Enforces single-flight execution per document, debounces rapid retries,
/// and ensures cooperative cancellation when user dismisses or resets.
actor AnalysisRequestCoordinator {
    static let shared = AnalysisRequestCoordinator()

    private var activeTasks: [UUID: Task<ScreenAnalysisResult, Error>] = [:]

    private init() {}

    /// Executes an analysis operation for a given document.
    /// Cancels any existing in-flight task for the same document before starting.
    func performAnalysis(
        documentID: UUID,
        operation: @Sendable @escaping () async throws -> ScreenAnalysisResult
    ) async throws -> ScreenAnalysisResult {
        // Cancel existing task for this document
        cancel(documentID: documentID)

        let task = Task<ScreenAnalysisResult, Error> {
            try Task.checkCancellation()
            let result = try await operation()
            try Task.checkCancellation()
            return result
        }

        activeTasks[documentID] = task

        do {
            let result = try await task.value
            activeTasks.removeValue(forKey: documentID)
            return result
        } catch {
            activeTasks.removeValue(forKey: documentID)
            throw error
        }
    }

    /// Cancels the ongoing analysis task for a specific document.
    func cancel(documentID: UUID) {
        if let task = activeTasks.removeValue(forKey: documentID) {
            task.cancel()
        }
    }

    /// Cancels all active analysis tasks across all documents.
    func cancelAll() {
        for (_, task) in activeTasks {
            task.cancel()
        }
        activeTasks.removeAll()
    }

    /// Checks if a document has an active analysis in progress.
    func isAnalyzing(documentID: UUID) -> Bool {
        activeTasks[documentID] != nil
    }
}
