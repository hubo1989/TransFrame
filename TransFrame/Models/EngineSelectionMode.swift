//
//  EngineSelectionMode.swift
//  TransFrame
//
//  Multi-translation engine selection modes
//

import Foundation

/// Engine selection mode for multi-engine translation
/// Legacy four-mode selection (Phase 6 teardown): kept ONLY as the
/// migration-source type for reading pre-V2 UserDefaults keys. New code
/// uses EngineModeV2.
@available(*, deprecated, message: "Use EngineModeV2; retained as migration source")
enum EngineSelectionMode: String, Codable, CaseIterable, Identifiable, Sendable {
    /// Primary engine with fallback on failure
    case primaryWithFallback = "primary_fallback"

    /// Run multiple engines in parallel
    case parallel = "parallel"

    /// Quick switch between engines (lazy load)
    case quickSwitch = "quick_switch"

    /// Bind specific engines to translation scenes
    case sceneBinding = "scene_binding"

    var id: String { rawValue }

    /// Localized display name
    var localizedName: String {
        switch self {
        case .primaryWithFallback:
            return NSLocalizedString(
                "engine.selection.mode.primary_fallback",
                comment: "Primary with Fallback"
            )
        case .parallel:
            return NSLocalizedString(
                "engine.selection.mode.parallel",
                comment: "Parallel"
            )
        case .quickSwitch:
            return NSLocalizedString(
                "engine.selection.mode.quick_switch",
                comment: "Quick Switch"
            )
        case .sceneBinding:
            return NSLocalizedString(
                "engine.selection.mode.scene_binding",
                comment: "Scene Binding"
            )
        }
    }

    /// Detailed description
    var modeDescription: String {
        switch self {
        case .primaryWithFallback:
            return NSLocalizedString(
                "engine.selection.mode.primary_fallback.description",
                comment: "Use primary engine, fall back to secondary on failure"
            )
        case .parallel:
            return NSLocalizedString(
                "engine.selection.mode.parallel.description",
                comment: "Run multiple engines simultaneously and compare results"
            )
        case .quickSwitch:
            return NSLocalizedString(
                "engine.selection.mode.quick_switch.description",
                comment: "Start with primary, quickly switch to other engines on demand"
            )
        case .sceneBinding:
            return NSLocalizedString(
                "engine.selection.mode.scene_binding.description",
                comment: "Use different engines for different translation scenarios"
            )
        }
    }

    /// Icon name for UI
    var iconName: String {
        switch self {
        case .primaryWithFallback:
            return "arrow.triangle.branch"
        case .parallel:
            return "arrow.triangle.merge"
        case .quickSwitch:
            return "arrow.left.arrow.right"
        case .sceneBinding:
            return "slider.horizontal.3"
        }
    }
}
