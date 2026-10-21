import Foundation

/// Represents a discrete reversible mutation on a CaptureDocument.
public struct EditTransaction {
    public let description: String
    public let undoAction: () -> Void
    public let redoAction: () -> Void

    public init(description: String, undoAction: @escaping () -> Void, redoAction: @escaping () -> Void) {
        self.description = description
        self.undoAction = undoAction
        self.redoAction = redoAction
    }
}

/// Transactional undo/redo stack manager.
/// Preserves memory by avoiding storing full pixel copies per operation.
@MainActor
public final class EditHistory {
    private var undoStack: [EditTransaction] = []
    private var redoStack: [EditTransaction] = []
    public let maxDepth: Int

    public init(maxDepth: Int = 30) {
        self.maxDepth = maxDepth
    }

    public var canUndo: Bool { !undoStack.isEmpty }
    public var canRedo: Bool { !redoStack.isEmpty }

    public func record(description: String, undo: @escaping () -> Void, redo: @escaping () -> Void) {
        let transaction = EditTransaction(description: description, undoAction: undo, redoAction: redo)
        undoStack.append(transaction)
        if undoStack.count > maxDepth {
            undoStack.removeFirst()
        }
        redoStack.removeAll()
    }

    public func undo() {
        guard let transaction = undoStack.popLast() else { return }
        transaction.undoAction()
        redoStack.append(transaction)
    }

    public func redo() {
        guard let transaction = redoStack.popLast() else { return }
        transaction.redoAction()
        undoStack.append(transaction)
    }

    public func clear() {
        undoStack.removeAll()
        redoStack.removeAll()
    }
}
