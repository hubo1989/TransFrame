//
//  SceneEngineBinding.swift
//  TransFrame
//
//  Configuration for binding engines to translation scenes
//

import Foundation

/// Binding configuration between a translation scene and engines
/// Legacy scene-to-engine binding (Phase 6 teardown): kept ONLY as the
/// migration-source type. New code uses the ordered chain (EngineChainItem).
@available(*, deprecated, message: "Use EngineChainItem; retained as migration source")
struct SceneEngineBinding: Codable, Identifiable, Equatable, Sendable {
    /// Scene identifier (also serves as unique ID)
    let scene: TranslationScene

    /// Primary engine for this scene
    var primaryEngine: TranslationEngineType

    /// Fallback engine when primary fails
    var fallbackEngine: TranslationEngineType?

    /// Whether fallback is enabled
    var fallbackEnabled: Bool

    /// Custom prompt for this scene (overrides default)
    var customPrompt: String?

    var id: TranslationScene { scene }

    init(
        scene: TranslationScene,
        primaryEngine: TranslationEngineType,
        fallbackEngine: TranslationEngineType? = nil,
        fallbackEnabled: Bool = true,
        customPrompt: String? = nil
    ) {
        self.scene = scene
        self.primaryEngine = primaryEngine
        self.fallbackEngine = fallbackEngine
        self.fallbackEnabled = fallbackEnabled
        self.customPrompt = customPrompt
    }

    /// Default binding for a scene
    static func `default`(for scene: TranslationScene) -> SceneEngineBinding {
        SceneEngineBinding(
            scene: scene,
            primaryEngine: .apple,
            fallbackEngine: .mtranServer,
            fallbackEnabled: true
        )
    }

    /// All default bindings
    static var allDefaults: [TranslationScene: SceneEngineBinding] {
        TranslationScene.allCases.reduce(into: [:]) { result, scene in
            result[scene] = .default(for: scene)
        }
    }
}
