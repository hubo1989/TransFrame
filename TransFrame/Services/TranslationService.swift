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
    typealias EngineConfigsProvider = @Sendable () async -> [TranslationEngineType: TranslationEngineConfig]

    static let shared = TranslationService()

    private let registry: TranslationEngineRegistry
    private let engineConfigsProvider: EngineConfigsProvider
    private let keychain = KeychainService.shared
    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "TransFrame",
        category: "TranslationService"
    )

    // Prompt configuration
    private var promptConfig: TranslationPromptConfig = TranslationPromptConfig()

    init(
        registry: TranslationEngineRegistry = .shared,
        engineConfigsProvider: @escaping EngineConfigsProvider = {
            await MainActor.run { AppSettings.shared.engineConfigs }
        }
    ) {
        self.registry = registry
        self.engineConfigsProvider = engineConfigsProvider
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
        // Phase 6 teardown: the legacy four-mode dispatch is gone; the mode
        // parameter maps onto V2 semantics. primaryWithFallback/quickSwitch/
        // sceneBinding -> ordered chain; parallel -> compare set.
        switch mode {
        case .primaryWithFallback, .quickSwitch, .sceneBinding:
            var chain: [TranslationEngineType]
            var useFallback: Bool
            switch mode {
            case .sceneBinding:
                // Scene binding supplied its own primary/fallback pair
                let binding = (scene.flatMap { sceneBindings[$0] })
                    ?? SceneEngineBinding.default(for: scene ?? .screenshot)
                chain = [binding.primaryEngine]
                if binding.fallbackEnabled, let fallback = binding.fallbackEngine {
                    chain.append(fallback)
                }
                useFallback = binding.fallbackEnabled
            case .quickSwitch:
                // Legacy quickSwitch = primary without fallback
                chain = [preferredEngine]
                useFallback = false
            default:
                chain = parallelEngines.isEmpty ? [preferredEngine] : parallelEngines
                useFallback = fallbackEnabled
            }
            if !useFallback { chain = Array(chain.prefix(1)) }
            if !chain.contains(preferredEngine), mode != .sceneBinding {
                chain.insert(preferredEngine, at: 0)
            }
            // Preserve the legacy implicit fallback: a single-engine chain
            // gained a complementary fallback (apple<->mtranServer).
            if useFallback, chain.count == 1 {
                let only = chain[0]
                let implicitFallback: TranslationEngineType
                if let scene {
                    implicitFallback = SceneEngineBinding.default(for: scene).fallbackEngine ?? (only == .apple ? .mtranServer : .apple)
                } else {
                    implicitFallback = only == .apple ? .mtranServer : .apple
                }
                chain.append(implicitFallback)
            }
            return try await translateOrderedV2(
                segments: segments,
                to: targetLanguage,
                from: sourceLanguage,
                scene: scene,
                chain: chain.map { .builtin($0) }
            )
        case .parallel:
            let engines = parallelEngines.isEmpty ? [preferredEngine] : parallelEngines
            return try await translateCompareV2(
                segments: segments,
                to: targetLanguage,
                from: sourceLanguage,
                scene: scene,
                engines: engines.map { .builtin($0) }
            )
        }
    }

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
    private func resolvedProvider(
        for engine: TranslationEngineType,
        config overrideConfig: TranslationEngineConfig? = nil
    ) async throws -> any TranslationProvider {
        if overrideConfig == nil, await registry.hasManualOverride(for: engine),
           let provider = await registry.provider(for: engine) {
            return provider
        }

        if let overrideConfig {
            return try await registry.createTemporaryProvider(for: engine, config: overrideConfig)
        }

        let configs = await engineConfigsProvider()
        let engineConfig = configs[engine] ?? .default(for: engine)

        return try await registry.createProvider(for: engine, config: engineConfig)
    }

    // MARK: - V2 Ordered Mode (Phase 3)

    /// Ordered fallback chain: try each entry in order until one succeeds.
    /// Chain members that are unavailable (not configured / no credentials)
    /// are skipped with a logged EngineResult; all-failure throws an
    /// aggregated error. Compatible instances resolve by UUID; a dangling
    /// UUID (instance deleted) is skipped the same way.
    func translateOrderedV2(
        segments: [String],
        to targetLanguage: String,
        from sourceLanguage: String? = nil,
        scene: TranslationScene? = nil,
        chain: [EngineChainItem]
    ) async throws -> TranslationResultBundle {
        guard !segments.isEmpty else {
            return TranslationResultBundle(
                results: [],
                primaryEngine: chain.first.map(\.resolvedEngine) ?? .apple,
                selectionMode: .primaryWithFallback,
                scene: scene
            )
        }

        // Defensive: empty chain falls back to Apple so the pipeline still runs
        let effectiveChain = chain.isEmpty ? [.builtin(.apple)] : chain

        var failures: [EngineResult] = []
        for item in effectiveChain {
            do {
                let bundle = try await translateWithChainItem(
                    item,
                    segments: segments,
                    to: targetLanguage,
                    from: sourceLanguage,
                    scene: scene
                )
                return TranslationResultBundle(
                    results: failures + bundle.results,
                    primaryEngine: item.resolvedEngine,
                    selectionMode: .primaryWithFallback,
                    scene: scene
                )
            } catch {
                // Cancellation is not an engine failure: the user stopped the
                // operation — rethrow immediately instead of grinding through
                // the remaining chain (PR #92 review).
                if error is CancellationError {
                    throw error
                }
                failures.append(EngineResult.failed(
                    engine: item.resolvedEngine,
                    error: error,
                    latency: 0
                ))
                logger.warning("ordered chain: entry failed engine=\(item.resolvedEngine.rawValue, privacy: .public) err=\(String(describing: error), privacy: .public)")
            }
        }

        // Wrap per-entry errors with the engine identity: provider error
        // descriptions are generic ("not available", "timed out") and the
        // aggregated message must tell the user WHICH engine needs attention
        // (PR #92 review).
        throw MultiEngineError.allEnginesFailed(
            failures.map { result in
                EngineFailedError(engine: result.engine, underlying: result.error)
            }
        )
    }

    /// Translate with one chain entry, resolving compatible instances by UUID.
    private func translateWithChainItem(
        _ item: EngineChainItem,
        segments: [String],
        to targetLanguage: String,
        from sourceLanguage: String?,
        scene: TranslationScene?
    ) async throws -> TranslationResultBundle {
        switch item.kind {
        case .builtin(let engine):
            return try await translateWithEngine(
                engine,
                segments: segments,
                to: targetLanguage,
                from: sourceLanguage,
                scene: scene
            )
        case .compatible(let instanceID):
            let configs = await compatibleConfigsForChain()
            guard let config = configs.first(where: { $0.id == instanceID }) else {
                throw MultiEngineError.chainEntryUnavailable(
                    "compatible instance deleted: \(instanceID)"
                )
            }
            let provider = try await registry.createCompatibleProvider(
                compatibleConfig: config
            )
            guard await provider.isAvailable else {
                throw TranslationProviderError.notAvailable
            }
            let start = Date()
            let results = try await translateWithResolvedPrompt(
                provider: provider,
                engine: .custom,
                texts: segments,
                from: sourceLanguage,
                to: targetLanguage,
                scene: scene
            )
            let bilingual = results.map { BilingualSegment(from: $0) }
            guard !bilingual.isEmpty else {
                throw TranslationProviderError.translationFailed(
                    "\(provider.name) returned no results"
                )
            }
            let result = EngineResult(
                engine: .custom,
                segments: bilingual,
                latency: Date().timeIntervalSince(start),
                displayName: config.displayName
            )
            return TranslationResultBundle(
                results: [result],
                primaryEngine: .custom,
                selectionMode: .primaryWithFallback,
                scene: scene
            )
        }
    }

    /// Compatible instance list for chain resolution.
    private func compatibleConfigsForChain() async -> [CompatibleTranslationProvider.CompatibleConfig] {
        await registry.compatibleConfigList()
    }

    // MARK: - V2 Compare Mode (Phase 4)

    /// Parallel compare: run 2-4 entries concurrently, return one
    /// EngineResult per entry (success or failure) in selection order.
    /// At least one success → successful bundle; all failed → aggregated
    /// error with engine identity. Cancellation propagates immediately.
    func translateCompareV2(
        segments: [String],
        to targetLanguage: String,
        from sourceLanguage: String? = nil,
        scene: TranslationScene? = nil,
        engines: [EngineChainItem]
    ) async throws -> TranslationResultBundle {
        guard !segments.isEmpty else {
            // Empty input is a caller bug; log it instead of silently
            // returning a bundle whose primaryEngine may not even resolve.
            logger.notice("compare: empty segments (caller bug)")
            return TranslationResultBundle(
                results: [],
                primaryEngine: .apple,
                selectionMode: .parallel,
                scene: scene
            )
        }

        // Defensive: empty selection runs Apple alone; clamp to the compare
        // limit so hand-built configs cannot fan out unbounded paid calls
        // (service-layer backstop for EngineSelectionLimits, PR #93 review).
        var effectiveEngines = engines.isEmpty ? [.builtin(.apple)] : engines
        if effectiveEngines.count > EngineSelectionLimits.compareEnginesLimit {
            logger.warning("compare: clamping \(effectiveEngines.count) entries to \(EngineSelectionLimits.compareEnginesLimit)")
            effectiveEngines = Array(effectiveEngines.prefix(EngineSelectionLimits.compareEnginesLimit))
        }

        // Correlate results with chain items by entry id: the same engine
        // may legitimately appear twice (e.g. two compatible instances).
        // Cancellation is recorded as a failed result and rethrown after the
        // group drains (task-group children cannot throw here).
        let results = await withTaskGroup(
            of: (UUID, EngineResult).self,
            returning: [UUID: EngineResult].self
        ) { group in
            for item in effectiveEngines {
                group.addTask {
                    do {
                        let bundle = try await self.translateWithChainItem(
                            item,
                            segments: segments,
                            to: targetLanguage,
                            from: sourceLanguage,
                            scene: scene
                        )
                        var result = bundle.results.first ?? EngineResult.failed(
                            engine: item.resolvedEngine,
                            error: TranslationProviderError.translationFailed("no result")
                        )
                        if case .compatible(let instanceID) = item.kind,
                           result.displayName == nil,
                           let name = await self.compatibleConfigsForChain()
                               .first(where: { $0.id == instanceID })?.displayName {
                            result.displayName = name
                        }
                        return (item.id, result)
                    } catch {
                        return (item.id, EngineResult.failed(
                            engine: item.resolvedEngine,
                            error: error
                        ))
                    }
                }
            }
            var collected: [UUID: EngineResult] = [:]
            for await pair in group {
                collected[pair.0] = pair.1
            }
            return collected
        }

        try Task.checkCancellation()

        let orderedResults = effectiveEngines.compactMap { results[$0.id] }

        guard orderedResults.contains(where: { $0.isSuccess }) else {
            throw MultiEngineError.allEnginesFailed(
                orderedResults.map { EngineFailedError(engine: $0.engine, underlying: $0.error) }
            )
        }

        let primary = orderedResults.first(where: \.isSuccess) ?? orderedResults[0]
        return TranslationResultBundle(
            results: orderedResults,
            primaryEngine: primary.engine,
            selectionMode: .parallel,
            scene: scene
        )
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
        try await verifyConnection(for: engine, config: nil)
    }

    func verifyConnection(
        for engine: TranslationEngineType,
        config: TranslationEngineConfig?
    ) async throws {
        let provider = try await resolvedProvider(for: engine, config: config)
        try await provider.verifyConnection()
    }

    /// Test connection to a specific engine
    func testConnection(for engine: TranslationEngineType) async -> Bool {
        if await registry.hasManualOverride(for: engine),
           let provider = await registry.provider(for: engine) {
            return await provider.checkConnection()
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
