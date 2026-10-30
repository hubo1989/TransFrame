//
//  TranslationEngineRegistry.swift
//  TransFrame
//
//  Registry for managing translation engine providers
//

import Foundation
import os.log

/// Actor-based registry for managing translation engine providers
actor TranslationEngineRegistry {
    typealias ProviderFactory = @Sendable (
        TranslationEngineType,
        TranslationEngineConfig,
        CompatibleTranslationProvider.ConfigurationSnapshot?
    ) async throws -> any TranslationProvider

    typealias CompatibleProviderFactory = @Sendable (
        CompatibleTranslationProvider.CompatibleConfig,
        TranslationEngineConfig
    ) async throws -> CompatibleTranslationProvider

    typealias CompatibleConfigsProvider = @Sendable () async -> [CompatibleTranslationProvider.CompatibleConfig]

    private struct CachedProviderEntry {
        let provider: any TranslationProvider
        let config: TranslationEngineConfig?
        let customSnapshot: CompatibleTranslationProvider.ConfigurationSnapshot?
        let isManualOverride: Bool
    }

    private struct InFlightProviderCreation {
        let token: UInt64
        let config: TranslationEngineConfig
        let customSnapshot: CompatibleTranslationProvider.ConfigurationSnapshot?
        let task: Task<any TranslationProvider, Error>
    }

    private struct CachedCompatibleProviderEntry {
        let provider: CompatibleTranslationProvider
        let config: CompatibleTranslationProvider.CompatibleConfig
    }

    private struct InFlightCompatibleCreation {
        let token: UInt64
        let config: CompatibleTranslationProvider.CompatibleConfig
        let task: Task<CompatibleTranslationProvider, Error>
    }

    /// Shared singleton instance
    static let shared = TranslationEngineRegistry()

    /// Registered providers keyed by engine type
    private var providers: [TranslationEngineType: CachedProviderEntry] = [:]
    private var inFlightCreations: [TranslationEngineType: InFlightProviderCreation] = [:]
    private var engineMutationRevisions: [TranslationEngineType: UInt64] = [:]
    private var nextCreationToken: UInt64 = 0

    /// Compatible engine providers keyed by composite ID (e.g., "custom:0", "custom:1")
    private var compatibleProviders: [String: CachedCompatibleProviderEntry] = [:]
    private var inFlightCompatibleCreations: [String: InFlightCompatibleCreation] = [:]
    private var compatibleMutationRevisions: [String: UInt64] = [:]
    private var compatibleClearRevision: UInt64 = 0

    /// Keychain service for credential access
    private let keychain = KeychainService.shared
    private let providerFactory: ProviderFactory?
    private let compatibleProviderFactory: CompatibleProviderFactory?
    private let compatibleConfigsProvider: CompatibleConfigsProvider

    /// Logger instance
    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "TransFrame",
        category: "TranslationEngineRegistry"
    )

    init(
        registerBuiltInProviders: Bool = true,
        providerFactory: ProviderFactory? = nil,
        compatibleProviderFactory: CompatibleProviderFactory? = nil,
        compatibleConfigsProvider: @escaping CompatibleConfigsProvider = {
            await MainActor.run { AppSettings.shared.compatibleProviderConfigs }
        }
    ) {
        self.providerFactory = providerFactory
        self.compatibleProviderFactory = compatibleProviderFactory
        self.compatibleConfigsProvider = compatibleConfigsProvider

        if registerBuiltInProviders {
            // Cannot call async method in init, so we register synchronously.
            // Built-in providers don't need async setup.
            let appleProvider = AppleTranslationProvider()
            providers[.apple] = CachedProviderEntry(
                provider: appleProvider,
                config: nil,
                customSnapshot: nil,
                isManualOverride: true
            )

            let mtranProvider = MTranServerEngine.shared
            providers[.mtranServer] = CachedProviderEntry(
                provider: mtranProvider,
                config: .default(for: .mtranServer),
                customSnapshot: nil,
                isManualOverride: false
            )

            logger.info("Registered 2 built-in providers")
        }
    }

    // MARK: - Registration

    /// Register a provider for an engine type
    func register(_ provider: any TranslationProvider, for type: TranslationEngineType) {
        bumpEngineMutationRevision(for: type)
        inFlightCreations[type]?.task.cancel()
        inFlightCreations.removeValue(forKey: type)
        providers[type] = CachedProviderEntry(
            provider: provider,
            config: nil,
            customSnapshot: nil,
            isManualOverride: true
        )
        logger.info("Registered provider for \(type.rawValue)")
    }

    /// Unregister a provider for an engine type
    func unregister(_ type: TranslationEngineType) {
        bumpEngineMutationRevision(for: type)
        inFlightCreations[type]?.task.cancel()
        inFlightCreations.removeValue(forKey: type)
        providers.removeValue(forKey: type)
        logger.info("Unregistered provider for \(type.rawValue)")
    }

    /// Saved compatible instance list (for chain resolution by UUID).
    func compatibleConfigList() async -> [CompatibleTranslationProvider.CompatibleConfig] {
        await compatibleConfigsProvider()
    }

    /// Get the provider for an engine type
    func provider(for type: TranslationEngineType) -> (any TranslationProvider)? {
        return providers[type]?.provider
    }

    func hasManualOverride(for type: TranslationEngineType) -> Bool {
        return providers[type]?.isManualOverride == true
    }

    // MARK: - Availability

    /// List all registered engine types
    func registeredEngines() -> [TranslationEngineType] {
        Array(providers.keys)
    }

    /// List all available engines (registered and configured)
    func availableEngines() async -> [TranslationEngineType] {
        let results = await withTaskGroup(of: (TranslationEngineType, Bool).self) { group in
            for (type, entry) in providers {
                let provider = entry.provider
                group.addTask { await (type, provider.isAvailable) }
            }
            var availability: [TranslationEngineType: Bool] = [:]
            for await (type, isAvailable) in group {
                availability[type] = isAvailable
            }
            return availability
        }
        return results
            .filter { $0.value }
            .keys
            .sorted { $0.rawValue < $1.rawValue }
    }

    /// Check if an engine is configured (has required credentials)
    func isEngineConfigured(_ type: TranslationEngineType) async -> Bool {
        // Built-in engines don't need credentials
        if !type.requiresAPIKey {
            return await providers[type]?.provider.isAvailable ?? false
        }

        // Check if credentials exist in Keychain
        return await keychain.hasCredentials(for: type)
    }

    /// Check if an engine is available for use
    func isEngineAvailable(_ type: TranslationEngineType) async -> Bool {
        guard let provider = providers[type]?.provider else { return false }
        return await provider.isAvailable
    }

    private func allocateCreationToken() -> UInt64 {
        nextCreationToken &+= 1
        return nextCreationToken
    }

    @discardableResult
    private func bumpEngineMutationRevision(for type: TranslationEngineType) -> UInt64 {
        let next = (engineMutationRevisions[type] ?? 0) &+ 1
        engineMutationRevisions[type] = next
        return next
    }

    @discardableResult
    private func bumpCompatibleMutationRevision(for compositeId: String) -> UInt64 {
        let next = (compatibleMutationRevisions[compositeId] ?? 0) &+ 1
        compatibleMutationRevisions[compositeId] = next
        return next
    }
}

// MARK: - Provider Creation

extension TranslationEngineRegistry {
    /// Create and register a provider for an engine type
    /// This is used for engines that require configuration (LLM, cloud services)
    /// - Parameters:
    ///   - type: The engine type to create
    ///   - config: The engine configuration
    ///   - forceRefresh: Force recreation even if cached
    /// - Returns: The created provider
    func createProvider(
        for type: TranslationEngineType,
        config: TranslationEngineConfig,
        forceRefresh: Bool = false
    ) async throws -> any TranslationProvider {
        if !forceRefresh,
           let existing = providers[type],
           existing.isManualOverride {
            return existing.provider
        }

        let customSnapshot: CompatibleTranslationProvider.ConfigurationSnapshot?
        if type == .custom {
            let savedConfigs = await compatibleConfigsProvider()
            customSnapshot = CompatibleTranslationProvider.resolveConfigurationSnapshot(
                from: config,
                savedConfigs: savedConfigs
            )
            if !forceRefresh,
               let existing = providers[type],
               existing.isManualOverride {
                return existing.provider
            }
        } else {
            customSnapshot = nil
        }

        if !forceRefresh, let existing = providers[type] {
            if existing.isManualOverride || matchesCachedProvider(
                entry: existing,
                type: type,
                config: config,
                customSnapshot: customSnapshot
            ) {
                return existing.provider
            }
            providers.removeValue(forKey: type)
        } else if forceRefresh {
            providers.removeValue(forKey: type)
        }

        if !forceRefresh,
           let inFlight = inFlightCreations[type],
           inFlight.config == config,
           inFlight.customSnapshot == customSnapshot {
            return try await inFlight.task.value
        }
        // Stale or force-refreshed creation: cancel and drop it so repeated
        // saves don't spawn concurrent duplicate providers for the same type.
        if let stale = inFlightCreations[type] {
            stale.task.cancel()
            inFlightCreations.removeValue(forKey: type)
        }

        let mutationRevision = bumpEngineMutationRevision(for: type)
        let token = allocateCreationToken()
        let task = makeProviderTask(
            for: type,
            config: config,
            customSnapshot: customSnapshot
        )
        inFlightCreations[type] = InFlightProviderCreation(
            token: token,
            config: config,
            customSnapshot: customSnapshot,
            task: task
        )

        do {
            let provider = try await task.value
            if inFlightCreations[type]?.token == token,
               engineMutationRevisions[type] == mutationRevision {
                inFlightCreations.removeValue(forKey: type)
                providers[type] = CachedProviderEntry(
                    provider: provider,
                    config: config,
                    customSnapshot: customSnapshot,
                    isManualOverride: false
                )
                logger.info("Registered configured provider for \(type.rawValue)")
            }
            return provider
        } catch {
            if inFlightCreations[type]?.token == token {
                inFlightCreations.removeValue(forKey: type)
            }
            throw error
        }
    }

    func createTemporaryProvider(
        for type: TranslationEngineType,
        config: TranslationEngineConfig
    ) async throws -> any TranslationProvider {
        let customSnapshot: CompatibleTranslationProvider.ConfigurationSnapshot?
        if type == .custom {
            let savedConfigs = await compatibleConfigsProvider()
            customSnapshot = CompatibleTranslationProvider.resolveConfigurationSnapshot(
                from: config, savedConfigs: savedConfigs
            )
        } else {
            customSnapshot = nil
        }
        return try await makeProviderTask(for: type, config: config, customSnapshot: customSnapshot).value
    }

    /// Create and cache a compatible engine provider for a specific instance
    /// - Parameters:
    ///   - compatibleConfig: The compatible engine configuration
    ///   - forceRefresh: Force recreation even if cached
    /// - Returns: The created provider
    func createCompatibleProvider(
        compatibleConfig: CompatibleTranslationProvider.CompatibleConfig,
        forceRefresh: Bool = false
    ) async throws -> CompatibleTranslationProvider {
        let compositeId = compatibleConfig.keychainId

        if !forceRefresh, let existing = compatibleProviders[compositeId] {
            if existing.config == compatibleConfig {
                return existing.provider
            }
            compatibleProviders.removeValue(forKey: compositeId)
        } else if forceRefresh {
            compatibleProviders.removeValue(forKey: compositeId)
        }

        if !forceRefresh,
           let inFlight = inFlightCompatibleCreations[compositeId],
           inFlight.config == compatibleConfig {
            return try await inFlight.task.value
        }

        let mutationRevision = bumpCompatibleMutationRevision(for: compositeId)
        let clearRevision = compatibleClearRevision
        let token = allocateCreationToken()
        let engineConfig = TranslationEngineConfig.default(for: .custom)
        let task = makeCompatibleProviderTask(
            compatibleConfig: compatibleConfig,
            engineConfig: engineConfig
        )
        inFlightCompatibleCreations[compositeId] = InFlightCompatibleCreation(
            token: token,
            config: compatibleConfig,
            task: task
        )

        do {
            let provider = try await task.value
            if inFlightCompatibleCreations[compositeId]?.token == token,
               compatibleMutationRevisions[compositeId] == mutationRevision,
               compatibleClearRevision == clearRevision {
                inFlightCompatibleCreations.removeValue(forKey: compositeId)
                compatibleProviders[compositeId] = CachedCompatibleProviderEntry(
                    provider: provider,
                    config: compatibleConfig
                )
                logger.info("Created compatible provider for \(compositeId)")
            }
            return provider
        } catch {
            if inFlightCompatibleCreations[compositeId]?.token == token {
                inFlightCompatibleCreations.removeValue(forKey: compositeId)
            }
            throw error
        }
    }

    /// Get a cached compatible engine provider
    /// - Parameter compositeId: The composite identifier (e.g., "custom:0")
    /// - Returns: The cached provider, or nil
    func getCompatibleProvider(for compositeId: String) -> CompatibleTranslationProvider? {
        return compatibleProviders[compositeId]?.provider
    }

    /// Remove a cached compatible engine provider
    /// - Parameter compositeId: The composite identifier
    func removeCompatibleProvider(for compositeId: String) {
        bumpCompatibleMutationRevision(for: compositeId)
        inFlightCompatibleCreations[compositeId]?.task.cancel()
        inFlightCompatibleCreations.removeValue(forKey: compositeId)
        compatibleProviders.removeValue(forKey: compositeId)
        logger.info("Removed compatible provider for \(compositeId)")
    }

    /// Clear all cached compatible providers
    func clearCompatibleProviders() {
        compatibleClearRevision &+= 1
        for compositeId in Set(compatibleProviders.keys).union(inFlightCompatibleCreations.keys) {
            bumpCompatibleMutationRevision(for: compositeId)
        }
        for inFlight in inFlightCompatibleCreations.values {
            inFlight.task.cancel()
        }
        inFlightCompatibleCreations.removeAll()
        compatibleProviders.removeAll()
        logger.info("Cleared all compatible providers")
    }

    private func matchesCachedProvider(
        entry: CachedProviderEntry,
        type: TranslationEngineType,
        config: TranslationEngineConfig,
        customSnapshot: CompatibleTranslationProvider.ConfigurationSnapshot?
    ) -> Bool {
        guard entry.config == config else {
            return false
        }
        if type == .custom {
            return entry.customSnapshot == customSnapshot
        }
        return true
    }

    private func makeProviderTask(
        for type: TranslationEngineType,
        config: TranslationEngineConfig,
        customSnapshot: CompatibleTranslationProvider.ConfigurationSnapshot?
    ) -> Task<any TranslationProvider, Error> {
        if let providerFactory {
            return Task {
                try await providerFactory(type, config, customSnapshot)
            }
        }

        let keychain = self.keychain
        let appleAlreadyRegistered = providers[.apple] != nil

        return Task {
            switch type {
            case .apple:
                if appleAlreadyRegistered {
                    throw RegistryError.alreadyRegistered
                }
                return AppleTranslationProvider()

            case .mtranServer:
                return MTranServerEngine.provider(config: config)

            case .openai, .claude, .gemini, .ollama:
                return try await LLMTranslationProvider(
                    type: type,
                    config: config,
                    keychain: keychain
                )

            case .google:
                return try await GoogleTranslationProvider(
                    config: config,
                    keychain: keychain
                )

            case .deepl:
                return try await DeepLTranslationProvider(
                    config: config,
                    keychain: keychain
                )

            case .baidu:
                return try await BaiduTranslationProvider(
                    config: config,
                    keychain: keychain
                )

            case .custom:
                if let customSnapshot {
                    return CompatibleTranslationProvider(
                        snapshot: customSnapshot,
                        keychain: keychain,
                        providerID: "custom"
                    )
                }
                return try await CompatibleTranslationProvider(
                    config: config,
                    keychain: keychain
                )
            }
        }
    }

    private func makeCompatibleProviderTask(
        compatibleConfig: CompatibleTranslationProvider.CompatibleConfig,
        engineConfig: TranslationEngineConfig
    ) -> Task<CompatibleTranslationProvider, Error> {
        if let compatibleProviderFactory {
            return Task {
                try await compatibleProviderFactory(compatibleConfig, engineConfig)
            }
        }

        let keychain = self.keychain
        return Task {
            try await CompatibleTranslationProvider(
                config: engineConfig,
                compatibleConfig: compatibleConfig,
                keychain: keychain
            )
        }
    }
}

// MARK: - Registry Errors

enum RegistryError: LocalizedError, Sendable {
    case alreadyRegistered
    case notRegistered(TranslationEngineType)
    case configurationMissing(TranslationEngineType)
    case credentialsNotFound(TranslationEngineType)

    var errorDescription: String? {
        switch self {
        case .alreadyRegistered:
            return NSLocalizedString(
                "registry.error.already_registered",
                comment: "Provider is already registered"
            )
        case .notRegistered(let type):
            return String(
                format: NSLocalizedString(
                    "registry.error.not_registered",
                    comment: "No provider registered for %@"
                ),
                type.localizedName
            )
        case .configurationMissing(let type):
            return String(
                format: NSLocalizedString(
                    "registry.error.config_missing",
                    comment: "Configuration missing for %@"
                ),
                type.localizedName
            )
        case .credentialsNotFound(let type):
            return String(
                format: NSLocalizedString(
                    "registry.error.credentials_not_found",
                    comment: "Credentials not found for %@"
                ),
                type.localizedName
            )
        }
    }
}
