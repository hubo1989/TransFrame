import Foundation
import CoreGraphics
import Combine

/// Centralized thread-safe in-memory store for active CaptureDocument instances.
/// Allows In-Place Toolbar, Editor, and PinnedWindows to stay strictly synchronized.
@MainActor
final class CaptureDocumentStore: ObservableObject {
    static let shared = CaptureDocumentStore()

    @Published private(set) var activeDocuments: [UUID: CaptureDocument] = [:]

    private init() {}

    /// Registers a newly created capture document.
    func register(_ document: CaptureDocument) {
        activeDocuments[document.id] = document
    }

    /// Retrieves an active capture document by its stable ID.
    func document(for id: UUID) -> CaptureDocument? {
        activeDocuments[id]
    }

    /// Mutates an active capture document in place.
    func update(id: UUID, mutate: (inout CaptureDocument) -> Void) {
        guard var doc = activeDocuments[id] else { return }
        mutate(&doc)
        activeDocuments[id] = doc
        NotificationCenter.default.post(name: .captureDocumentDidUpdate, object: doc)
    }

    /// Replaces an active capture document.
    func update(_ document: CaptureDocument) {
        activeDocuments[document.id] = document
        NotificationCenter.default.post(name: .captureDocumentDidUpdate, object: document)
    }

    /// Removes a document when completely closed or discarded.
    func unregister(id: UUID) {
        activeDocuments.removeValue(forKey: id)
    }
}

// MARK: - Notifications

extension Notification.Name {
    static let captureDocumentDidUpdate = Notification.Name("captureDocumentDidUpdate")
}
