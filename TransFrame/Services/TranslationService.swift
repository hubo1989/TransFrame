//
//  TranslationService.swift
//  TransFrame
//
//  Created for US-010: 创建 TranslationService 编排层
//  Updated for multi-engine support
//

import Foundation
import os.log
import NaturalLanguage

/// Orchestrates multiple translation providers with various selection modes
@available(macOS 13.0, *)
actor TranslationService {
    static let shared = TranslationService()

    private let registry: TranslationEngineRegistry
    private let keychain = KeychainService.shared
    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "TransFrame",
        category: "TranslationService"
    )

    // Prompt configuration
    private var promptConfig: TranslationPromptConfig = TranslationPromptConfig()

    init(registry: TranslationEngineRegistry = .shared) {
        self.registry = registry
    }

    // MARK: - Main Translation API

    /// Translate text using specified selection mode
    /// - Parameters:
    ///   - segments: Source texts to translate
    ///   - targetLanguage: Target language code
    ///   - sourceLanguage: Source language code (nil for auto-detect)
    ///   - scene: Translation scene (for scene binding mode)
    ///   - mode: Engine selection mode
    ///   - preferredEngine: Primary engine (for modes that need it)
    ///   - fallbackEnabled: Whether to use fallback
    ///   - parallelEngines: Engines to run in parallel mode
    ///   - sceneBindings: Scene-to-engine bindings
    /// - Returns: Bundle with results from all engines
    func translate(
        segments: [String],
        to targetLanguage: String,
        from sourceLanguage: String? = nil,
        scene: TranslationScene? = nil,
        mode: EngineSelectionMode,
        preferredEngine: TranslationEngineType = .apple,
        fallbackEnabled: Bool = true,
        parallelEngines: [TranslationEngineType] = [],
        sceneBindings: [TranslationScene: SceneEngineBinding] = [:]
    ) async throws -> TranslationResultBundle {
        guard !segments.isEmpty else {
            return TranslationResultBundle(
                results: [],
                primaryEngine: preferredEngine,
                selectionMode: mode,
                scene: scene
            )
        }

        switch mode {
        case .primaryWithFallback:
            // Use the user-configured second engine as fallback when available
            let configuredFallback: TranslationEngineType? = {
                guard fallbackEnabled else { return nil }
                if parallelEngines.count > 1 {
                    return parallelEngines[1]
                }
                return nil
            }()
            return try await translateWithFallback(
                segments: segments,
                to: targetLanguage,
                from: sourceLanguage,
                primaryEngine: preferredEngine,
                fallbackEnabled: fallbackEnabled,
                fallbackEngine: configuredFallback,
                scene: scene
            )

        case .parallel:
            let effectiveParallelEngines = await filterEnabledEngines(
                parallelEngines.isEmpty ? [preferredEngine] : parallelEngines
            )
            return try await translateParallel(
                segments: segments,
                to: targetLanguage,
                from: sourceLanguage,
                engines: effectiveParallelEngines,
                scene: scene
            )

        case .quickSwitch:
            return try await translateForQuickSwitch(
                segments: segments,
                to: targetLanguage,
                from: sourceLanguage,
                primaryEngine: preferredEngine,
                scene: scene
            )

        case .sceneBinding:
            return try await translateByScene(
                segments: segments,
                to: targetLanguage,
                from: sourceLanguage,
                scene: scene ?? .screenshot,
                bindings: sceneBindings,
                preferredEngine: preferredEngine
            )
        }
    }

    // MARK: - Selection Mode Implementations

    /// Primary with fallback mode
    private func translateWithFallback(
        segments: [String],
        to targetLanguage: String,
        from sourceLanguage: String?,
        primaryEngine: TranslationEngineType,
        fallbackEnabled: Bool,
        fallbackEngine: TranslationEngineType? = nil,
        scene: TranslationScene?
    ) async throws -> TranslationResultBundle {
        var errors: [Error] = []

        // Try primary engine
        do {
            let result = try await translateWithEngine(
                primaryEngine,
                segments: segments,
                to: targetLanguage,
                from: sourceLanguage,
                scene: scene,
                mode: .primaryWithFallback
            )
            return result
        } catch is CancellationError {
            throw CancellationError()
        } catch {
            errors.append(error)
            logger.warning("Primary engine \(primaryEngine.rawValue) failed: \(error.localizedDescription)")
        }

        try Task.checkCancellation()

        // Try fallback if enabled
        if fallbackEnabled {
            let actualFallback: TranslationEngineType
            if let engine = fallbackEngine {
                actualFallback = engine
            } else if let scene = scene {
                actualFallback = SceneEngineBinding.default(for: scene).fallbackEngine ?? .mtranServer
            } else {
                actualFallback = primaryEngine == .apple ? .mtranServer : .apple
            }

            // Avoid retrying the same engine that just failed
            guard actualFallback != primaryEngine else {
                logger.warning("Fallback engine \(actualFallback.rawValue) equals primary, skipping")
                throw MultiEngineError.allEnginesFailed(errors)
            }

            // Skip fallback if the engine is not explicitly enabled in user settings
            let enabledFallbacks = await filterEnabledEngines([actualFallback])
            guard !enabledFallbacks.isEmpty else {
                logger.warning("Fallback engine \(actualFallback.rawValue) is not enabled, skipping")
                throw MultiEngineError.allEnginesFailed(errors)
            }

            do {
                let result = try await translateWithEngine(
                    actualFallback,
                    segments: segments,
                    to: targetLanguage,
                    from: sourceLanguage,
                    scene: scene,
                    mode: .primaryWithFallback
                )
                let failedPrimary = EngineResult.failed(engine: primaryEngine, error: errors[0])
                let mergedResults = [failedPrimary] + result.results
                logger.info("Fallback to \(actualFallback.rawValue) succeeded")
                return TranslationResultBundle(
                    results: mergedResults,
                    primaryEngine: result.primaryEngine,
                    selectionMode: .primaryWithFallback,
                    scene: scene
                )
            } catch is CancellationError {
                throw CancellationError()
            } catch {
                errors.append(error)
                logger.warning("Fallback engine \(actualFallback.rawValue) also failed: \(error.localizedDescription)")
            }
        }

        throw MultiEngineError.allEnginesFailed(errors)
    }

    /// Parallel mode - run multiple engines simultaneously
    private func translateParallel(
        segments: [String],
        to targetLanguage: String,
        from sourceLanguage: String?,
        engines: [TranslationEngineType],
        scene: TranslationScene?
    ) async throws -> TranslationResultBundle {
        let primaryEngine = engines.first ?? .apple

        let results = await withTaskGroup(of: EngineResult.self, returning: [EngineResult].self) { group in
            for engine in engines {
                group.addTask {
                    do {
                        let start = Date()
                        let provider = try await self.resolvedProvider(for: engine)

                        let providerResults = try await self.translateWithResolvedPrompt(
                            provider: provider,
                            engine: engine,
                            texts: segments,
                            from: sourceLanguage,
                            to: targetLanguage,
                            scene: scene
                        )
                        let bilingualSegments = providerResults.map { BilingualSegment(from: $0) }

                        // Treat empty results as failure
                        guard !bilingualSegments.isEmpty else {
                            return EngineResult.failed(
                                engine: engine,
                                error: TranslationProviderError.translationFailed(
                                    "\(provider.name) returned no results"
                                )
                            )
                        }

                        return EngineResult(
                            engine: engine,
                            segments: bilingualSegments,
                            latency: Date().timeIntervalSince(start)
                        )
                    } catch {
                        return EngineResult.failed(engine: engine, error: error)
                    }
                }
            }

            var collectedResults: [EngineResult] = []
            for await result in group {
                collectedResults.append(result)
            }
            return collectedResults
        }

        // If all engines failed (no successful results), throw instead of silently returning empty results
        let failedErrors = results.compactMap { $0.error }
        let hasSuccess = results.contains { $0.isSuccess }
        if !hasSuccess {
            throw MultiEngineError.allEnginesFailed(failedErrors)
        }

        return TranslationResultBundle(
            results: results,
            primaryEngine: primaryEngine,
            selectionMode: .parallel,
            scene: scene
        )
    }

    /// Quick switch mode - start with primary, others load on demand
    private func translateForQuickSwitch(
        segments: [String],
        to targetLanguage: String,
        from sourceLanguage: String?,
        primaryEngine: TranslationEngineType,
        scene: TranslationScene?
    ) async throws -> TranslationResultBundle {
        // For now, behaves like primary without fallback
        // UI layer will handle switching to other engines
        return try await translateWithEngine(
            primaryEngine,
            segments: segments,
            to: targetLanguage,
            from: sourceLanguage,
            scene: scene,
            mode: .quickSwitch
        )
    }

    /// Scene binding mode - use engine configured for the scene
    private func translateByScene(
        segments: [String],
        to targetLanguage: String,
        from sourceLanguage: String?,
        scene: TranslationScene,
        bindings: [TranslationScene: SceneEngineBinding],
        preferredEngine: TranslationEngineType
    ) async throws -> TranslationResultBundle {
        let binding = bindings[scene] ?? SceneEngineBinding.default(for: scene)

        return try await translateWithFallback(
            segments: segments,
            to: targetLanguage,
            from: sourceLanguage,
            primaryEngine: binding.primaryEngine,
            fallbackEnabled: binding.fallbackEnabled,
            fallbackEngine: binding.fallbackEngine,
            scene: scene
        )
    }

    // MARK: - Helper Methods

    /// Translate with a specific engine
    private func translateWithEngine(
        _ engine: TranslationEngineType,
        segments: [String],
        to targetLanguage: String,
        from sourceLanguage: String?,
        scene: TranslationScene?,
        mode: EngineSelectionMode = .primaryWithFallback
    ) async throws -> TranslationResultBundle {
        let start = Date()
        let provider = try await resolvedProvider(for: engine)

        guard await provider.isAvailable else {
            throw TranslationProviderError.notAvailable
        }

        let results = try await translateWithResolvedPrompt(
            provider: provider,
            engine: engine,
            texts: segments,
            from: sourceLanguage,
            to: targetLanguage,
            scene: scene
        )

        let bilingualSegments = results.map { BilingualSegment(from: $0) }

        // Treat empty results as failure so callers can trigger fallback
        guard !bilingualSegments.isEmpty else {
            throw TranslationProviderError.translationFailed(
                "\(provider.name) returned no results"
            )
        }

        let latency = Date().timeIntervalSince(start)

        return TranslationResultBundle.single(
            engine: engine,
            segments: bilingualSegments,
            latency: latency,
            selectionMode: mode,
            scene: scene
        )
    }

    /// Update prompt configuration
    func updatePromptConfig(_ config: TranslationPromptConfig) {
        self.promptConfig = config
    }

    /// Get current prompt configuration
    func getPromptConfig() -> TranslationPromptConfig {
        return promptConfig
    }

    /// Determines if a text contains translatable characters (letters or ideographs)
    private func isTranslatable(_ text: String) -> Bool {
        return text.unicodeScalars.contains { scalar in
            if CharacterSet.letters.contains(scalar) {
                return true
            }
            if (0x4E00...0x9FFF).contains(scalar.value) {
                return true
            }
            return false
        }
    }

    /// Checks if the text contains any Chinese characters
    private func containsHanCharacters(_ text: String) -> Bool {
        return text.unicodeScalars.contains { (0x4E00...0x9FFF).contains($0.value) }
    }

    /// Detects the language of the given text using NaturalLanguage framework.
    private func detectLanguage(for text: String) -> TranslationLanguage? {
        guard let dominantLanguage = NLLanguageRecognizer.dominantLanguage(for: text) else {
            return nil
        }
        return TranslationLanguage.fromTranslationCode(dominantLanguage.rawValue)
    }

    private func translateWithResolvedPrompt(
        provider: any TranslationProvider,
        engine: TranslationEngineType,
        texts: [String],
        from sourceLanguage: String?,
        to targetLanguage: String,
        scene: TranslationScene?
    ) async throws -> [TranslationResult] {
        let targetLang = TranslationLanguage.fromTranslationCode(targetLanguage)
        let explicitSourceLang = sourceLanguage.flatMap { TranslationLanguage.fromTranslationCode($0) }

        var finalResults = [TranslationResult?](repeating: nil, count: texts.count)
        var pendingIndices: [Int] = []
        var pendingTexts: [String] = []

        for (index, text) in texts.enumerated() {
            let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
            if trimmed.isEmpty {
                finalResults[index] = TranslationResult(
                    sourceText: text,
                    translatedText: text,
                    sourceLanguage: sourceLanguage ?? "auto",
                    targetLanguage: targetLanguage
                )
                continue
            }

            // 1. Non-translatable text bypass (e.g. pure numbers, punctuation like "{""}")
            if !isTranslatable(text) {
                finalResults[index] = TranslationResult(
                    sourceText: text,
                    translatedText: text,
                    sourceLanguage: sourceLanguage ?? "auto",
                    targetLanguage: targetLanguage
                )
                continue
            }

            let detectedSourceLang = detectLanguage(for: text)
            var resolvedSourceLang = explicitSourceLang ?? detectedSourceLang

            // 2. Handle cases where text clearly contains Chinese and target is Chinese.
            // Skip when detected source variant matches the target, or when detection
            // failed but the text contains Han characters (likely same-script Chinese).
            // Do NOT rewrite detected Traditional → Simplified or vice versa.
            if explicitSourceLang == nil,
               let targetLang,
               (targetLang == .chineseSimplified || targetLang == .chineseTraditional),
               containsHanCharacters(text) {
                if detectedSourceLang == nil || detectedSourceLang == targetLang {
                    resolvedSourceLang = targetLang
                }
                // If detectedSourceLang is a different Chinese variant, leave it
                // as-is so the engine performs the script conversion.
            }

            // 3. Self-translation bypass — only skip when source and target are
            // truly identical. Simplified ↔ Traditional Chinese are different
            // targets and must go through the translation engine.
            if let resolvedSourceLang, let targetLang, resolvedSourceLang == targetLang {
                finalResults[index] = TranslationResult(
                    sourceText: text,
                    translatedText: text,
                    sourceLanguage: resolvedSourceLang.rawValue,
                    targetLanguage: targetLang.rawValue
                )
            } else {
                pendingIndices.append(index)
                pendingTexts.append(text)
            }
        }

        if !pendingTexts.isEmpty {
            let translatedResults: [TranslationResult]

            if let promptConfigurableProvider = provider as? TranslationPromptConfigurable {
                let promptTemplate = await resolvedPromptTemplate(
                    for: provider,
                    engine: engine,
                    scene: scene
                )
                translatedResults = try await promptConfigurableProvider.translate(
                    texts: pendingTexts,
                    from: sourceLanguage,
                    to: targetLanguage,
                    promptTemplate: promptTemplate
                )
            } else {
                translatedResults = try await provider.translate(
                    texts: pendingTexts,
                    from: sourceLanguage,
                    to: targetLanguage
                )
            }

            if translatedResults.count != pendingTexts.count {
                logger.error("Provider returned mismatch count. Expected: \(pendingTexts.count), got: \(translatedResults.count)")
                throw TranslationProviderError.translationFailed(
                    "\(provider.name) returned \(translatedResults.count) results for \(pendingTexts.count) texts"
                )
            }

            for (offset, result) in translatedResults.enumerated() {
                let originalIndex = pendingIndices[offset]
                finalResults[originalIndex] = result
            }
        }

        var hasFailedSlot = false
        let sanitized = finalResults.map { result -> TranslationResult? in
            guard let res = result else {
                hasFailedSlot = true
                return nil
            }
            let cleaned = self.sanitizeTranslation(translated: res.translatedText, source: res.sourceText)
            // Only treat as failure when the provider returned something clearly
            // broken (empty, JSON garbage, etc.) that sanitize converted back to
            // source. A valid translation that happens to equal the source (proper
            // nouns, code identifiers, "OK") must NOT be treated as failure.
            if cleaned == res.sourceText,
               res.translatedText != res.sourceText,
               self.isLikelyBrokenTranslation(res.translatedText, source: res.sourceText) {
                hasFailedSlot = true
            }
            return TranslationResult(
                sourceText: res.sourceText,
                translatedText: cleaned,
                sourceLanguage: res.sourceLanguage,
                targetLanguage: res.targetLanguage
            )
        }

        if hasFailedSlot {
            throw TranslationProviderError.translationFailed(
                "\(provider.name) returned invalid or empty translations"
            )
        }

        return sanitized.map { result in
            result ?? TranslationResult(
                sourceText: "",
                translatedText: "",
                sourceLanguage: sourceLanguage ?? "auto",
                targetLanguage: targetLanguage
            )
        }
    }

    /// Sanitizes the translated text, reverting to original if it is empty, a broken JSON, or just empty curly braces like {""}
    private func sanitizeTranslation(translated: String, source: String) -> String {
        let trimmed = translated.trimmingCharacters(in: .whitespacesAndNewlines)
        
        if trimmed.isEmpty {
            return source
        }
        
        // 1. 如果原文不包含大括号，且译文以 { 开头、以 } 结尾，进行深度 JSON 解析与内容提取
        let sourceContainsBraces = source.contains("{") || source.contains("}")
        if !sourceContainsBraces && trimmed.hasPrefix("{") && trimmed.hasSuffix("}") {
            if let data = trimmed.data(using: .utf8),
               let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
                // 常见大模型输出的翻译字段
                let translationKeys = ["translated_text", "translatedText", "translation", "result", "text"]
                for key in translationKeys {
                    if let value = json[key] as? String {
                        let trimmedValue = value.trimmingCharacters(in: .whitespacesAndNewlines)
                        return trimmedValue.isEmpty ? source : value
                    }
                }
                // 空 JSON 字典或没有找到任何已知翻译字段的字典，安全回退到原文
                return source
            } else {
                // 损坏的以大括号包围的字符串，大概率也是泄露的大模型 JSON 结构，安全回退到原文
                return source
            }
        }
        
        // 2. 彻底的字符集排查防御：若原文不含大括号，且译文仅由大括号、冒号、空格、各种单双引号组成，直接回退到原文
        if !sourceContainsBraces {
            let isOnlyBracesAndQuotes = trimmed.allSatisfy { char in
                char == "{" || char == "}" || char == "\"" || char == "'" || char == "`" || 
                char == "“" || char == "”" || char == "‘" || char == "’" || char == ":" ||
                char.isWhitespace
            }
            if isOnlyBracesAndQuotes {
                return source
            }
        }
        
        // 3. 兜底兼容变体
        if trimmed.hasPrefix("{") && trimmed.hasSuffix("}") && (trimmed.contains("\"\"") || trimmed.contains("“”")) {
            return source
        }
        
        return translated
    }

    /// Returns true when the raw provider output is clearly broken (empty,
    /// JSON garbage, braces-only), as opposed to a valid translation that
    /// happens to be identical to the source text.
    private func isLikelyBrokenTranslation(_ translated: String, source: String) -> Bool {
        let trimmed = translated.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty { return true }
        // JSON-like garbage that sanitize extracted nothing useful from
        if trimmed.hasPrefix("{"), trimmed.hasSuffix("}") {
            return true
        }
        if trimmed.hasPrefix("["), trimmed.hasSuffix("]") {
            return true
        }
        // Only braces/quotes/colons/whitespace
        let allStructural = trimmed.allSatisfy { char in
            char == "{" || char == "}" || char == "\"" || char == "'" || char == "`" ||
            char == "[" || char == "]" || char == ":" || char == "," ||
            char == "“" || char == "”" || char == "‘" || char == "’" ||
            char.isWhitespace || char.isNumber
        }
        return allStructural
    }

    private func resolvedPromptTemplate(
        for provider: any TranslationProvider,
        engine: TranslationEngineType,
        scene: TranslationScene?
    ) async -> String? {
        let sceneToUse = scene ?? .screenshot
        let compatiblePromptID = await (provider as? TranslationPromptContextProviding)?.compatiblePromptIdentifier()

        let resolvedPrompt = promptConfig.promptPreview(
            for: engine,
            scene: sceneToUse,
            compatiblePromptID: compatiblePromptID
        )

        if resolvedPrompt == TranslationPromptConfig.defaultPrompt {
            return nil
        }

        return resolvedPrompt
    }

    /// Filters engine list to only include engines that are explicitly enabled in user settings.
    /// Apple is treated as always enabled (it's the default built-in engine).
    private func filterEnabledEngines(_ engines: [TranslationEngineType]) async -> [TranslationEngineType] {
        let configs = await MainActor.run {
            AppSettings.shared.engineConfigs
        }
        return engines.filter { engine in
            engine == .apple || configs[engine]?.isEnabled == true
        }
    }

    private func resolvedProvider(for engine: TranslationEngineType) async throws -> any TranslationProvider {
        if let provider = await registry.provider(for: engine) {
            return provider
        }

        let engineConfig = await MainActor.run {
            AppSettings.shared.engineConfigs[engine] ?? .default(for: engine)
        }

        return try await registry.createProvider(for: engine, config: engineConfig)
    }

    // MARK: - Legacy API (Backward Compatible)

    /// Translates segments using the preferred engine with automatic fallback
    /// - Parameters:
    ///   - segments: Source texts to translate
    ///   - targetLanguage: Target language code
    ///   - preferredEngine: User's preferred translation engine
    ///   - sourceLanguage: Source language code (nil for auto-detect)
    /// - Returns: Array of bilingual segments with source and translated text
    func translate(
        segments: [String],
        to targetLanguage: String,
        preferredEngine: TranslationEngineType = .apple,
        from sourceLanguage: String? = nil,
        scene: TranslationScene? = nil,
        mode: EngineSelectionMode = .primaryWithFallback,
        fallbackEnabled: Bool = true,
        parallelEngines: [TranslationEngineType] = [],
        sceneBindings: [TranslationScene: SceneEngineBinding] = [:]
    ) async throws -> [BilingualSegment] {
        guard !segments.isEmpty else { return [] }

        let bundle = try await translate(
            segments: segments,
            to: targetLanguage,
            from: sourceLanguage,
            scene: scene,
            mode: mode,
            preferredEngine: preferredEngine,
            fallbackEnabled: fallbackEnabled,
            parallelEngines: parallelEngines,
            sceneBindings: sceneBindings
        )

        let result = bundle.primaryResult

        // If no engine produced results, propagate the actual errors
        guard !result.isEmpty else {
            if bundle.successfulEngines.isEmpty {
                let errors = bundle.results.compactMap { $0.error }
                throw MultiEngineError.allEnginesFailed(errors)
            }
            throw MultiEngineError.noResults
        }

        return result
    }

    // MARK: - Connection Testing

    /// Verify connection and throw details on failure
    func verifyConnection(for engine: TranslationEngineType) async throws {
        let provider: any TranslationProvider
        if let existing = await registry.provider(for: engine) {
            provider = existing
        } else {
            provider = try await resolvedProvider(for: engine)
        }
        try await provider.verifyConnection()
    }

    /// Test connection to a specific engine
    func testConnection(for engine: TranslationEngineType) async -> Bool {
        // First try to get existing provider
        if let provider = await registry.provider(for: engine) {
            return await provider.checkConnection()
        }

        // If provider doesn't exist, create it for engines that need credentials
        // (Google, DeepL, Baidu, LLM providers, etc.)
        guard engine.requiresAPIKey else {
            // Built-in engines (apple, mtranServer) should already be registered in init
            // If missing, log warning but return true to avoid false failure in UI
            logger.warning("Built-in engine \(engine.rawValue) provider not found in registry")
            return true
        }

        let provider: any TranslationProvider
        do {
            provider = try await resolvedProvider(for: engine)
        } catch {
            logger.error("Failed to resolve provider for \(engine.rawValue): \(error.localizedDescription)")
            return false
        }

        return await provider.checkConnection()
    }
}
