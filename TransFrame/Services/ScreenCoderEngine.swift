//
//  ScreenCoderEngine.swift
//  TransFrame
//
//  Created for US-008: ScreenCoder Engine
//

import CoreGraphics
import Foundation

// MARK: - ScreenCoder Engine Errors

/// Errors specific to the ScreenCoder engine
enum ScreenCoderEngineError: LocalizedError, Sendable {
    case noProviderConfigured
    case providerNotAvailable(String)
    case invalidConfiguration(String)
    
    var errorDescription: String? {
        switch self {
        case .noProviderConfigured:
            return "No VLM provider is configured. Please configure a provider in Settings."
        case .providerNotAvailable(let name):
            return "The VLM provider '\(name)' is not available. Check your API key and network connection."
        case .invalidConfiguration(let message):
            return "Invalid configuration: \(message)"
        }
    }
}

// MARK: - ScreenCoder Engine

/// Unified engine for VLM-based screen analysis.
/// Manages multiple VLM providers and routes analysis requests to the currently selected provider.
///
/// Usage:
/// ```swift
/// let engine = ScreenCoderEngine.shared
/// let result = try await engine.analyze(image: cgImage)
/// ```
actor ScreenCoderEngine {
    // MARK: - Singleton
    
    /// Shared instance for app-wide screen analysis operations
    static let shared = ScreenCoderEngine()
    
    // MARK: - Properties
    
    /// Cached provider instances by type
    private var providerCache: [VLMProviderType: any VLMProvider] = [:]
    
    /// Last known configuration hash for cache invalidation
    private var lastConfigurationHash: Int = 0
    
    // MARK: - Initialization
    
    private init() {}
    
    // MARK: - Public API
    
    /// Analyzes an image using the currently configured VLM provider.
    /// - Parameter image: The CGImage to analyze
    /// - Returns: ScreenAnalysisResult containing extracted text segments with positions
    /// - Throws: ScreenCoderEngineError or VLMProviderError if analysis fails
    func analyze(image: CGImage) async throws -> ScreenAnalysisResult {
        let provider = try await currentProvider()
        
        guard await provider.isAvailable else {
            throw ScreenCoderEngineError.providerNotAvailable(provider.name)
        }
        
        return try await provider.analyze(image: image)
    }
    
    /// Returns the currently configured VLM provider.
    /// - Returns: The active VLMProvider instance
    /// - Throws: ScreenCoderEngineError if no provider is configured
    func currentProvider() async throws -> any VLMProvider {
        let settings = await MainActor.run { AppSettings.shared }
        let providerType = await MainActor.run { settings.vlmProvider }
        
        return try await provider(for: providerType)
    }
    
    /// Returns a provider instance for the specified type.
    /// Creates a new instance if not cached or if configuration has changed.
    /// - Parameter type: The VLM provider type
    /// - Returns: A configured VLMProvider instance
    /// - Throws: ScreenCoderEngineError if configuration is invalid
    func provider(for type: VLMProviderType) async throws -> any VLMProvider {
        let currentHash = await configurationHash()
        
        if currentHash != lastConfigurationHash {
            providerCache.removeAll()
            lastConfigurationHash = currentHash
        }
        
        if let cached = providerCache[type] {
            return cached
        }
        
        let newProvider = try await createProvider(for: type)
        providerCache[type] = newProvider
        return newProvider
    }
    
    /// Checks if the current provider is available and properly configured.
    /// - Returns: true if the provider is ready for use
    func isCurrentProviderAvailable() async -> Bool {
        guard let provider = try? await currentProvider() else {
            return false
        }
        return await provider.isAvailable
    }
    
    /// Clears the provider cache, forcing recreation on next use.
    /// Call this when settings change significantly.
    func invalidateCache() {
        providerCache.removeAll()
        lastConfigurationHash = 0
    }
    
    /// Returns all supported provider types.
    var supportedProviderTypes: [VLMProviderType] {
        VLMProviderType.allCases
    }
    
    // MARK: - Private Methods
    
    /// Creates a provider instance for the given type using current settings.
    private func createProvider(for type: VLMProviderType) async throws -> any VLMProvider {
        let (apiKey, baseURLString, modelName, openAIMode) = await MainActor.run {
            let settings = AppSettings.shared
            return (
                settings.vlmAPIKey(for: type),
                settings.vlmBaseURL(for: type),
                settings.vlmModelName(for: type),
                settings.vlmOpenAIMode
            )
        }
        
        let effectiveBaseURL = baseURLString.isEmpty ? type.defaultBaseURL : baseURLString
        let effectiveModel = modelName.isEmpty ? type.defaultModelName : modelName
        
        guard let baseURL = URL(string: effectiveBaseURL) else {
            throw ScreenCoderEngineError.invalidConfiguration("Invalid base URL: \(effectiveBaseURL)")
        }
        
        let configuration = VLMProviderConfiguration(
            apiKey: apiKey,
            baseURL: baseURL,
            modelName: effectiveModel
        )
        
        switch type {
        case .openai:
            return OpenAIVLMProvider(configuration: configuration, apiMode: openAIMode)
        case .claude:
            return ClaudeVLMProvider(configuration: configuration)
        case .gemini:
            return GeminiVLMProvider(configuration: configuration)
        case .ollama:
            return OllamaVLMProvider(configuration: configuration)
        case .paddleocr:
            return PaddleOCRVLMProvider()
        }
    }
    
    /// Computes a hash of the current configuration for cache invalidation.
    private func configurationHash() async -> Int {
        let (providerType, apiKey, baseURL, modelName, openAIMode) = await MainActor.run {
            let settings = AppSettings.shared
            return (
                settings.vlmProvider,
                settings.vlmAPIKey(for: settings.vlmProvider),
                settings.vlmBaseURL(for: settings.vlmProvider),
                settings.vlmModelName(for: settings.vlmProvider),
                settings.vlmOpenAIMode
            )
        }
        
        var hasher = Hasher()
        hasher.combine(providerType)
        hasher.combine(apiKey)
        hasher.combine(baseURL)
        hasher.combine(modelName)
        hasher.combine(openAIMode)
        return hasher.finalize()
    }
}
