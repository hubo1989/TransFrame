//
//  EngineSelectionV2.swift
//  TransFrame
//
//  Converged engine selection model: ordered fallback chain + compare set.
//  See tasks/prd-engine-mode-convergence.md
//

import Foundation

/// One entry in the ordered chain or compare set.
/// Builtin engines are referenced by type; compatible instances by their
/// stable `CompatibleConfig.id` UUID.
struct EngineChainItem: Codable, Equatable, Sendable, Identifiable {
    enum Kind: Codable, Equatable, Hashable, Sendable {
        case builtin(TranslationEngineType)
        case compatible(UUID)
    }

    /// Unique entry identity (the same engine may appear twice in a chain).
    let id: UUID

    var kind: Kind

    init(kind: Kind, id: UUID = UUID()) {
        self.id = id
        self.kind = kind
    }

    static func builtin(_ type: TranslationEngineType) -> EngineChainItem {
        EngineChainItem(kind: .builtin(type))
    }

    static func compatible(_ instanceID: UUID) -> EngineChainItem {
        EngineChainItem(kind: .compatible(instanceID))
    }

    /// The builtin engine this entry reports as (compatible instances
    /// surface as .custom; UI labels use the instance display name).
    var resolvedEngine: TranslationEngineType {
        switch kind {
        case .builtin(let engine): return engine
        case .compatible: return .custom
        }
    }
}

/// Converged selection modes (replaces EngineSelectionMode's four cases).
enum EngineModeV2: String, Codable, CaseIterable, Identifiable, Sendable {
    /// Ordered fallback chain: try engines in order until one succeeds.
    case ordered

    /// Run 2-4 engines in parallel and show results side by side.
    case compare

    var id: String { rawValue }

    var localizedName: String {
        switch self {
        case .ordered:
            return NSLocalizedString("engine.mode.ordered", comment: "Ordered")
        case .compare:
            return NSLocalizedString("engine.mode.compare", comment: "Compare")
        }
    }
}

/// Structural limits for the two modes.
enum EngineSelectionLimits {
    static let orderedChainLimit = 5
    static let compareEnginesLimit = 4
    static let compareEnginesMinimum = 2
}
