import Foundation
import SwiftUI
import os
import Security

/// PaddleOCR mode selection
enum PaddleOCRMode: String, Codable, CaseIterable, Sendable {
    case fast
    case precise

    var localizedName: String {
        switch self {
        case .fast:
            return NSLocalizedString("settings.paddleocr.mode.fast", comment: "Fast mode")
        case .precise:
            return NSLocalizedString("settings.paddleocr.mode.precise", comment: "Precise mode")
        }
    }

    var description: String {
        switch self {
        case .fast:
            return NSLocalizedString("settings.paddleocr.mode.fast.description", comment: "~1s, uses groupIntoLines")
        case .precise:
            return NSLocalizedString("settings.paddleocr.mode.precise.description", comment: "~12s, VL-1.5 model")
        }
    }
}

/// GLM OCR backend mode selection
enum GLMOCRMode: String, Codable, CaseIterable, Sendable {
    case cloud
    case local

    var localizedName: String {
        switch self {
        case .cloud:
            return NSLocalizedString("settings.glmocr.mode.cloud", comment: "Cloud mode")
        case .local:
            return NSLocalizedString("settings.glmocr.mode.local", comment: "Local mode")
        }
    }

    var providerDescription: String {
        switch self {
        case .cloud:
            return NSLocalizedString(
                "vlm.provider.glmocr.description",
                comment: "Zhipu GLM-OCR layout parsing API"
            )
        case .local:
            return NSLocalizedString(
                "vlm.provider.glmocr.local.description",
                comment: "Local MLX-VLM server for GLM-OCR"
            )
        }
    }

    var defaultBaseURL: String {
        switch self {
        case .cloud:
            return "https://open.bigmodel.cn/api/paas/v4"
        case .local:
            return "http://127.0.0.1:18081"
        }
    }

    var defaultModelName: String {
        switch self {
        case .cloud:
            return "glm-ocr"
        case .local:
            return "mlx-community/GLM-OCR-bf16"
        }
    }

    var requiresAPIKey: Bool {
        switch self {
        case .cloud:
            return true
        case .local:
            return false
        }
    }
}

/// User preferences persisted across sessions via UserDefaults.
/// All properties automatically sync to UserDefaults with the `TransFrame.` prefix.
@MainActor
@Observable
final class AppSettings {
    // MARK: - Singleton

    /// Shared settings instance
    static let shared = AppSettings()

    // MARK: - UserDefaults Keys

    enum Keys {
        static let prefix = "TransFrame."
        static let saveLocation = prefix + "saveLocation"
        static let defaultFormat = prefix + "defaultFormat"
        static let jpegQuality = prefix + "jpegQuality"
        static let heicQuality = prefix + "heicQuality"
        static let fullScreenShortcut = prefix + "fullScreenShortcut"
        static let selectionShortcut = prefix + "selectionShortcut"
        static let translationModeShortcut = prefix + "translationModeShortcut"
        static let textSelectionTranslationShortcut = prefix + "textSelectionTranslationShortcut"
        static let translateAndInsertShortcut = prefix + "translateAndInsertShortcut"
        static let strokeColor = prefix + "strokeColor"
        static let strokeWidth = prefix + "strokeWidth"
        static let textSize = prefix + "textSize"
        static let rectangleFilled = prefix + "rectangleFilled"
        static let ellipseFilled = prefix + "ellipseFilled"
        static let mosaicBlockSize = prefix + "mosaicBlockSize"
        static let translationTargetLanguage = prefix + "translationTargetLanguage"
        static let translationSourceLanguage = prefix + "translationSourceLanguage"
        static let translationAutoDetect = prefix + "translationAutoDetect"
        static let ocrEngine = prefix + "ocrEngine"
        static let translationEngine = prefix + "translationEngine"
        static let translationMode = prefix + "translationMode"
        static let onboardingCompleted = prefix + "onboardingCompleted"
        static let userSkippedPermissions = prefix + "userSkippedPermissions"
        static let paddleOCRServerAddress = prefix + "paddleOCRServerAddress"
        static let mtranServerHost = prefix + "mtranServerHost"
        static let mtranServerPort = prefix + "mtranServerPort"
        // VLM Configuration
        static let vlmProvider = prefix + "vlmProvider"
        static let vlmAPIKey = prefix + "vlmAPIKey"
        static let vlmBaseURL = prefix + "vlmBaseURL"
        static let vlmModelName = prefix + "vlmModelName"
        static let vlmOpenAIMode = prefix + "vlmOpenAIMode"
        static func vlmBaseURL(for provider: VLMProviderType) -> String {
            prefix + "vlmBaseURL." + provider.rawValue
        }
        static func vlmModelName(for provider: VLMProviderType) -> String {
            prefix + "vlmModelName." + provider.rawValue
        }
        // Translation Workflow Configuration
        static let preferredTranslationEngine = prefix + "preferredTranslationEngine"
        static let mtranServerURL = prefix + "mtranServerURL"
        static let translationFallbackEnabled = prefix + "translationFallbackEnabled"
        // Translate and Insert Language Configuration
        static let translateAndInsertSourceLanguage = prefix + "translateAndInsertSourceLanguage"
        static let translateAndInsertTargetLanguage = prefix + "translateAndInsertTargetLanguage"
        // Multi-Engine Configuration
        static let engineSelectionMode = prefix + "engineSelectionMode"
        static let engineConfigs = prefix + "engineConfigs"
        static let promptConfig = prefix + "promptConfig"
        static let sceneBindings = prefix + "sceneBindings"
        static let parallelEngines = prefix + "parallelEngines"
        static let compatibleProviderConfigs = prefix + "compatibleProviderConfigs"
        // PaddleOCR Configuration
        static let paddleOCRMode = prefix + "paddleOCRMode"
        static let paddleOCRUseCloud = prefix + "paddleOCRUseCloud"
        static let paddleOCRCloudBaseURL = prefix + "paddleOCRCloudBaseURL"
        static let paddleOCRCloudAPIKey = prefix + "paddleOCRCloudAPIKey"
        static let paddleOCRCloudModelId = prefix + "paddleOCRCloudModelId"
        static let paddleOCRLocalVLModelDir = prefix + "paddleOCRLocalVLModelDir"
    }

    // MARK: - Properties

    /// Default save directory
    var saveLocation: URL {
        didSet { save(saveLocation.path, forKey: Keys.saveLocation) }
    }

    /// Default export format (PNG or JPEG)
    var defaultFormat: ExportFormat {
        didSet { save(defaultFormat.rawValue, forKey: Keys.defaultFormat) }
    }

    /// JPEG compression quality (0.0-1.0)
    var jpegQuality: Double {
        didSet { save(jpegQuality, forKey: Keys.jpegQuality) }
    }

    /// HEIC compression quality (0.0-1.0)
    var heicQuality: Double {
        didSet { save(heicQuality, forKey: Keys.heicQuality) }
    }

    /// Global hotkey for full screen capture
    var fullScreenShortcut: KeyboardShortcut {
        didSet { saveShortcut(fullScreenShortcut, forKey: Keys.fullScreenShortcut) }
    }

    /// Global hotkey for selection capture
    var selectionShortcut: KeyboardShortcut {
        didSet { saveShortcut(selectionShortcut, forKey: Keys.selectionShortcut) }
    }

    /// Global hotkey for translation mode
    var translationModeShortcut: KeyboardShortcut {
        didSet { saveShortcut(translationModeShortcut, forKey: Keys.translationModeShortcut) }
    }

    /// Global hotkey for text selection translation
    var textSelectionTranslationShortcut: KeyboardShortcut {
        didSet { saveShortcut(textSelectionTranslationShortcut, forKey: Keys.textSelectionTranslationShortcut) }
    }

    /// Global hotkey for translate clipboard and insert
    var translateAndInsertShortcut: KeyboardShortcut {
        didSet { saveShortcut(translateAndInsertShortcut, forKey: Keys.translateAndInsertShortcut) }
    }

    /// Default annotation stroke color
    var strokeColor: CodableColor {
        didSet { saveColor(strokeColor, forKey: Keys.strokeColor) }
    }

    /// Default annotation stroke width
    var strokeWidth: CGFloat {
        didSet { save(Double(strokeWidth), forKey: Keys.strokeWidth) }
    }

    /// Default text annotation font size
    var textSize: CGFloat {
        didSet { save(Double(textSize), forKey: Keys.textSize) }
    }

    /// Whether rectangles are filled (solid) by default
    var rectangleFilled: Bool {
        didSet { save(rectangleFilled, forKey: Keys.rectangleFilled) }
    }

    /// Whether ellipses are filled (solid) by default
    var ellipseFilled: Bool {
        didSet { save(ellipseFilled, forKey: Keys.ellipseFilled) }
    }

    /// Default mosaic block size (pixelation level)
    var mosaicBlockSize: CGFloat {
        didSet { save(Double(mosaicBlockSize), forKey: Keys.mosaicBlockSize) }
    }

    /// Translation target language (nil = use system default)
    var translationTargetLanguage: TranslationLanguage? {
        didSet {
            if let language = translationTargetLanguage {
                save(language.rawValue, forKey: Keys.translationTargetLanguage)
            } else {
                UserDefaults.standard.removeObject(forKey: Keys.translationTargetLanguage)
            }
        }
    }

    /// Translation source language (.auto for automatic detection)
    var translationSourceLanguage: TranslationLanguage {
        didSet { save(translationSourceLanguage.rawValue, forKey: Keys.translationSourceLanguage) }
    }

    /// Whether to automatically detect source language
    var translationAutoDetect: Bool {
        didSet { save(translationAutoDetect, forKey: Keys.translationAutoDetect) }
    }

    /// OCR engine type
    var ocrEngine: OCREngineType {
        didSet { save(ocrEngine.rawValue, forKey: Keys.ocrEngine) }
    }

    /// Translation engine type
    var translationEngine: TranslationEngineType {
        didSet { save(translationEngine.rawValue, forKey: Keys.translationEngine) }
    }

    /// Translation display mode
    var translationMode: TranslationMode {
        didSet { save(translationMode.rawValue, forKey: Keys.translationMode) }
    }

    /// Whether the user has completed the first launch onboarding
    var onboardingCompleted: Bool {
        didSet { save(onboardingCompleted, forKey: Keys.onboardingCompleted) }
    }

    /// Whether the user skipped permissions during onboarding
    var userSkippedPermissions: Bool {
        didSet { save(userSkippedPermissions, forKey: Keys.userSkippedPermissions) }
    }

    var paddleOCRServerAddress: String {
        didSet { save(paddleOCRServerAddress, forKey: Keys.paddleOCRServerAddress) }
    }

    var mtranServerHost: String {
        didSet { save(mtranServerHost, forKey: Keys.mtranServerHost) }
    }

    var mtranServerPort: Int {
        didSet { save(mtranServerPort, forKey: Keys.mtranServerPort) }
    }

    // MARK: - VLM Configuration

    var vlmProvider: VLMProviderType {
        didSet {
            save(vlmProvider.rawValue, forKey: Keys.vlmProvider)
            vlmBaseURL = vlmBaseURL(for: vlmProvider)
            vlmModelName = vlmModelName(for: vlmProvider)
            vlmAPIKey = vlmAPIKey(for: vlmProvider)
        }
    }

    var vlmOpenAIMode: OpenAIAPIMode {
        didSet { save(vlmOpenAIMode.rawValue, forKey: Keys.vlmOpenAIMode) }
    }

    @ObservationIgnored
    private var vlmAPIKeySaveTask: Task<Void, Never>?

    @ObservationIgnored
    private var paddleOCRKeySaveTask: Task<Void, Never>?

    var vlmAPIKey: String {
        didSet {
            let capturedKey = vlmAPIKey
            let currentProvider = vlmProvider
            vlmAPIKeySaveTask?.cancel()
            vlmAPIKeySaveTask = Task {
                do {
                    try Task.checkCancellation()
                    if capturedKey.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                        try await KeychainService.shared.deleteVLMCredentials(for: currentProvider)
                    } else {
                        try await KeychainService.shared.saveVLMCredentials(
                            apiKey: capturedKey,
                            for: currentProvider
                        )
                    }
                } catch is CancellationError {
                } catch {
                    Logger.settings.error("Failed to save VLM API key to Keychain: \(error)")
                }
            }
        }
    }

    var vlmBaseURL: String {
        didSet {
            save(vlmBaseURL, forKey: Keys.vlmBaseURL(for: vlmProvider))
            save(vlmBaseURL, forKey: Keys.vlmBaseURL)
        }
    }

    var vlmModelName: String {
        didSet {
            save(vlmModelName, forKey: Keys.vlmModelName(for: vlmProvider))
            save(vlmModelName, forKey: Keys.vlmModelName)
        }
    }

    func vlmBaseURL(for provider: VLMProviderType) -> String {
        if let stored = UserDefaults.standard.string(forKey: Keys.vlmBaseURL(for: provider)), !stored.isEmpty {
            return stored
        }
        if provider == vlmProvider, let legacy = UserDefaults.standard.string(forKey: Keys.vlmBaseURL), !legacy.isEmpty {
            return legacy
        }
        return provider.defaultBaseURL
    }

    func setVLMBaseURL(_ url: String, for provider: VLMProviderType) {
        save(url, forKey: Keys.vlmBaseURL(for: provider))
        if provider == vlmProvider {
            vlmBaseURL = url
        }
    }

    func vlmModelName(for provider: VLMProviderType) -> String {
        if let stored = UserDefaults.standard.string(forKey: Keys.vlmModelName(for: provider)), !stored.isEmpty {
            return stored
        }
        if provider == vlmProvider, let legacy = UserDefaults.standard.string(forKey: Keys.vlmModelName), !legacy.isEmpty {
            return legacy
        }
        return provider.defaultModelName
    }

    func setVLMModelName(_ model: String, for provider: VLMProviderType) {
        save(model, forKey: Keys.vlmModelName(for: provider))
        if provider == vlmProvider {
            vlmModelName = model
        }
    }

    func vlmAPIKey(for provider: VLMProviderType) -> String {
        KeychainService.loadVLMAPIKeySynchronously(for: provider)
    }

    func setVLMAPIKey(_ key: String, for provider: VLMProviderType) {
        let capturedKey = key
        vlmAPIKeySaveTask?.cancel()
        vlmAPIKeySaveTask = Task {
            do {
                try Task.checkCancellation()
                if capturedKey.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                    try await KeychainService.shared.deleteVLMCredentials(for: provider)
                } else {
                    try await KeychainService.shared.saveVLMCredentials(apiKey: capturedKey, for: provider)
                }
            } catch is CancellationError {
            } catch {
                Logger.settings.error("Failed to save VLM API key for \(provider.rawValue): \(error)")
            }
        }
        if provider == vlmProvider {
            vlmAPIKey = key
        }
    }

    // MARK: - Translation Workflow Configuration

    var preferredTranslationEngine: PreferredTranslationEngine {
        didSet { save(preferredTranslationEngine.rawValue, forKey: Keys.preferredTranslationEngine) }
    }

    var mtranServerURL: String {
        didSet { save(mtranServerURL, forKey: Keys.mtranServerURL) }
    }

    var translationFallbackEnabled: Bool {
        didSet { save(translationFallbackEnabled, forKey: Keys.translationFallbackEnabled) }
    }

    // MARK: - Translate and Insert Language Configuration

    /// Source language for translate and insert (default: auto-detect)
    var translateAndInsertSourceLanguage: TranslationLanguage {
        didSet { save(translateAndInsertSourceLanguage.rawValue, forKey: Keys.translateAndInsertSourceLanguage) }
    }

    /// Target language for translate and insert (nil = follow system)
    var translateAndInsertTargetLanguage: TranslationLanguage? {
        didSet {
            if let language = translateAndInsertTargetLanguage {
                save(language.rawValue, forKey: Keys.translateAndInsertTargetLanguage)
            } else {
                UserDefaults.standard.removeObject(forKey: Keys.translateAndInsertTargetLanguage)
            }
        }
    }

    // MARK: - Multi-Engine Configuration

    /// Engine selection mode
    var engineSelectionMode: EngineSelectionMode {
        didSet { save(engineSelectionMode.rawValue, forKey: Keys.engineSelectionMode) }
    }

    /// Engine configurations (JSON encoded)
    /// Note: no didSet because @Observable does not reliably trigger
    /// didSet for value-type mutations, which can cause stale data to overwrite
    /// fresh saves. Always call saveEngineConfigs() explicitly after modifying.
    var engineConfigs: [TranslationEngineType: TranslationEngineConfig]

    /// Prompt configuration
    var promptConfig: TranslationPromptConfig {
        didSet { savePromptConfig() }
    }

    /// Scene-to-engine bindings
    var sceneBindings: [TranslationScene: SceneEngineBinding] {
        didSet { saveSceneBindings() }
    }

    /// Engines to run in parallel mode
    var parallelEngines: [TranslationEngineType] {
        didSet { saveParallelEngines() }
    }

    /// Compatible provider configurations
    var compatibleProviderConfigs: [CompatibleTranslationProvider.CompatibleConfig] {
        didSet { saveCompatibleConfigs() }
    }

    // MARK: - PaddleOCR Configuration

    /// PaddleOCR mode: fast (ocr command) or precise (doc_parser VL-1.5)
    var paddleOCRMode: PaddleOCRMode {
        didSet { save(paddleOCRMode.rawValue, forKey: Keys.paddleOCRMode) }
    }

    /// Whether to use cloud API instead of local CLI
    var paddleOCRUseCloud: Bool {
        didSet { save(paddleOCRUseCloud, forKey: Keys.paddleOCRUseCloud) }
    }

    /// Cloud API base URL (for third-party PaddleOCR cloud services)
    var paddleOCRCloudBaseURL: String {
        didSet { save(paddleOCRCloudBaseURL, forKey: Keys.paddleOCRCloudBaseURL) }
    }

    /// Cloud API key (stored securely in Keychain, not UserDefaults)
    var paddleOCRCloudAPIKey: String {
        didSet {
            let capturedKey = paddleOCRCloudAPIKey
            paddleOCRKeySaveTask?.cancel()
            paddleOCRKeySaveTask = Task {
                do {
                    try Task.checkCancellation()
                    if capturedKey.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                        try await KeychainService.shared.deletePaddleOCRCredentials()
                    } else {
                        try await KeychainService.shared.savePaddleOCRCredentials(apiKey: capturedKey)
                    }
                } catch is CancellationError {
                    // Superseded by newer keystroke
                } catch {
                    Logger.settings.error("Failed to save PaddleOCR cloud API key to Keychain: \(error)")
                }
            }
        }
    }

    /// Cloud API model ID (optional, for specifying which model to use)
    var paddleOCRCloudModelId: String {
        didSet { save(paddleOCRCloudModelId, forKey: Keys.paddleOCRCloudModelId) }
    }

    /// Local VL model directory (for vllm backend)
    var paddleOCRLocalVLModelDir: String {
        didSet { save(paddleOCRLocalVLModelDir, forKey: Keys.paddleOCRLocalVLModelDir) }
    }

    // MARK: - Initialization

    private init() {
        let defaults = UserDefaults.standard

        // Load save location from bookmark first, then path, or use Desktop
        let loadedLocation: URL
        if let bookmarkData = defaults.data(forKey: "SaveLocationBookmark"),
           let url = Self.resolveBookmark(bookmarkData) {
            loadedLocation = url
        } else if let path = defaults.string(forKey: Keys.saveLocation) {
            loadedLocation = URL(fileURLWithPath: path)
        } else {
            loadedLocation = FileManager.default.urls(for: .desktopDirectory, in: .userDomainMask).first
                ?? URL(fileURLWithPath: NSHomeDirectory())
        }
        saveLocation = loadedLocation

        // Load format
        if let formatRaw = defaults.string(forKey: Keys.defaultFormat),
           let format = ExportFormat(rawValue: formatRaw) {
            defaultFormat = format
        } else {
            defaultFormat = .png
        }

        // Load JPEG quality
        jpegQuality = defaults.object(forKey: Keys.jpegQuality) as? Double ?? 0.9

        // Load HEIC quality
        heicQuality = defaults.object(forKey: Keys.heicQuality) as? Double ?? 0.9

        // Load shortcuts
        fullScreenShortcut = Self.loadShortcut(forKey: Keys.fullScreenShortcut)
            ?? KeyboardShortcut.fullScreenDefault
        selectionShortcut = Self.loadShortcut(forKey: Keys.selectionShortcut)
            ?? KeyboardShortcut.selectionDefault
        translationModeShortcut = Self.loadShortcut(forKey: Keys.translationModeShortcut)
            ?? KeyboardShortcut.translationModeDefault
        textSelectionTranslationShortcut = Self.loadShortcut(forKey: Keys.textSelectionTranslationShortcut)
            ?? KeyboardShortcut.textSelectionTranslationDefault
        translateAndInsertShortcut = Self.loadShortcut(forKey: Keys.translateAndInsertShortcut)
            ?? KeyboardShortcut.translateAndInsertDefault

        // Load annotation defaults
        strokeColor = Self.loadColor(forKey: Keys.strokeColor) ?? .red
        strokeWidth = CGFloat(defaults.object(forKey: Keys.strokeWidth) as? Double ?? 2.0)
        textSize = CGFloat(defaults.object(forKey: Keys.textSize) as? Double ?? 14.0)
        rectangleFilled = defaults.object(forKey: Keys.rectangleFilled) as? Bool ?? false
        ellipseFilled = defaults.object(forKey: Keys.ellipseFilled) as? Bool ?? false
        mosaicBlockSize = CGFloat(defaults.object(forKey: Keys.mosaicBlockSize) as? Double ?? 10.0)

        // Load translation settings
        translationTargetLanguage = defaults.string(forKey: Keys.translationTargetLanguage)
            .flatMap { TranslationLanguage(rawValue: $0) }
        translationSourceLanguage = defaults.string(forKey: Keys.translationSourceLanguage)
            .flatMap { TranslationLanguage(rawValue: $0) } ?? .auto
        translationAutoDetect = defaults.object(forKey: Keys.translationAutoDetect) as? Bool ?? true

        // Load engine settings
        ocrEngine = defaults.string(forKey: Keys.ocrEngine)
            .flatMap { OCREngineType(rawValue: $0) } ?? .vision
        translationEngine = defaults.string(forKey: Keys.translationEngine)
            .flatMap { TranslationEngineType(rawValue: $0) } ?? .apple
        translationMode = defaults.string(forKey: Keys.translationMode)
            .flatMap { TranslationMode(rawValue: $0) } ?? .below
        onboardingCompleted = defaults.object(forKey: Keys.onboardingCompleted) as? Bool ?? false
        userSkippedPermissions = defaults.object(forKey: Keys.userSkippedPermissions) as? Bool ?? false
        paddleOCRServerAddress = defaults.string(forKey: Keys.paddleOCRServerAddress) ?? ""
        mtranServerHost = defaults.string(forKey: Keys.mtranServerHost) ?? "localhost"
        mtranServerPort = defaults.object(forKey: Keys.mtranServerPort) as? Int ?? 8989

        let resolvedVLMProvider = defaults.string(forKey: Keys.vlmProvider)
            .flatMap { VLMProviderType(rawValue: $0) } ?? .openai
        let resolvedOpenAIMode = defaults.string(forKey: Keys.vlmOpenAIMode)
            .flatMap { OpenAIAPIMode(rawValue: $0) } ?? .chat
        vlmProvider = resolvedVLMProvider
        vlmOpenAIMode = resolvedOpenAIMode
        vlmAPIKey = ""  // Will be loaded from Keychain in post-init below
        vlmBaseURL = defaults.string(forKey: Keys.vlmBaseURL(for: resolvedVLMProvider))
            ?? defaults.string(forKey: Keys.vlmBaseURL)
            ?? resolvedVLMProvider.defaultBaseURL
        vlmModelName = defaults.string(forKey: Keys.vlmModelName(for: resolvedVLMProvider))
            ?? defaults.string(forKey: Keys.vlmModelName)
            ?? resolvedVLMProvider.defaultModelName

        preferredTranslationEngine = defaults.string(forKey: Keys.preferredTranslationEngine)
            .flatMap { PreferredTranslationEngine(rawValue: $0) } ?? .apple
        mtranServerURL = defaults.string(forKey: Keys.mtranServerURL) ?? "http://localhost:8989"
        translationFallbackEnabled = defaults.object(forKey: Keys.translationFallbackEnabled) as? Bool ?? true

        // Load translate and insert language settings
        translateAndInsertSourceLanguage = defaults.string(forKey: Keys.translateAndInsertSourceLanguage)
            .flatMap { TranslationLanguage(rawValue: $0) } ?? .auto
        translateAndInsertTargetLanguage = defaults.string(forKey: Keys.translateAndInsertTargetLanguage)
            .flatMap { TranslationLanguage(rawValue: $0) }

        // Load multi-engine configuration
        engineSelectionMode = defaults.string(forKey: Keys.engineSelectionMode)
            .flatMap { EngineSelectionMode(rawValue: $0) } ?? .primaryWithFallback

        engineConfigs = Self.loadEngineConfigs()
        let loadedCompatibleProviderConfigs = Self.loadCompatibleConfigs()
        compatibleProviderConfigs = loadedCompatibleProviderConfigs
        promptConfig = Self.loadPromptConfig(compatibleConfigs: loadedCompatibleProviderConfigs)
        sceneBindings = Self.loadSceneBindings()
        parallelEngines = Self.loadParallelEngines()

        // Load PaddleOCR configuration
        paddleOCRMode = defaults.string(forKey: Keys.paddleOCRMode)
            .flatMap { PaddleOCRMode(rawValue: $0) } ?? .fast
        paddleOCRUseCloud = defaults.object(forKey: Keys.paddleOCRUseCloud) as? Bool ?? false
        paddleOCRCloudBaseURL = defaults.string(forKey: Keys.paddleOCRCloudBaseURL) ?? ""

        // Load PaddleOCR cloud API key from Keychain (secure storage)
        paddleOCRCloudAPIKey = Self.loadPaddleOCRAPIKeyFromKeychain()

        // Load cloud model ID
        paddleOCRCloudModelId = defaults.string(forKey: Keys.paddleOCRCloudModelId) ?? ""

        // Load vLLM model directory
        paddleOCRLocalVLModelDir = defaults.string(forKey: Keys.paddleOCRLocalVLModelDir) ?? ""

        // Post-init: Load VLM API key from Keychain (after all stored properties are initialized)
        let keychainKey = vlmAPIKey(for: resolvedVLMProvider)
        if !keychainKey.isEmpty {
            vlmAPIKey = keychainKey
        } else if let legacyKey = defaults.string(forKey: Keys.vlmAPIKey), !legacyKey.isEmpty {
            // Migrate from UserDefaults to Keychain
            vlmAPIKey = legacyKey  // triggers didSet -> saves to Keychain
            defaults.removeObject(forKey: Keys.vlmAPIKey)  // remove plaintext
            Logger.settings.info("Migrated VLM API key from UserDefaults to Keychain")
        }

        Logger.settings.info("ScreenCapture launched - settings loaded from: \(loadedLocation.path)")
    }

    // MARK: - Computed Properties

    /// Default stroke style based on current settings
    var defaultStrokeStyle: StrokeStyle {
        StrokeStyle(color: strokeColor, lineWidth: strokeWidth)
    }

    /// Default text style based on current settings
    var defaultTextStyle: TextStyle {
        TextStyle(color: strokeColor, fontSize: textSize, fontName: ".AppleSystemUIFont")
    }

    // MARK: - Reset

    /// Resets all settings to defaults
    func resetToDefaults() {
        saveLocation = FileManager.default.urls(for: .desktopDirectory, in: .userDomainMask).first
            ?? URL(fileURLWithPath: NSHomeDirectory())
        defaultFormat = .png
        jpegQuality = 0.9
        heicQuality = 0.9
        fullScreenShortcut = .fullScreenDefault
        selectionShortcut = .selectionDefault
        translationModeShortcut = .translationModeDefault
        textSelectionTranslationShortcut = .textSelectionTranslationDefault
        translateAndInsertShortcut = .translateAndInsertDefault
        strokeColor = .red
        strokeWidth = 2.0
        textSize = 14.0
        rectangleFilled = false
        ellipseFilled = false
        mosaicBlockSize = 10.0
        translationTargetLanguage = nil
        translationSourceLanguage = .auto
        translationAutoDetect = true
        ocrEngine = .vision
        translationEngine = .apple
        translationMode = .below
        vlmProvider = .openai
        vlmAPIKey = ""
        vlmOpenAIMode = .chat
        vlmBaseURL = VLMProviderType.openai.defaultBaseURL
        vlmModelName = VLMProviderType.openai.defaultModelName
        onboardingCompleted = false
        userSkippedPermissions = false
        translateAndInsertSourceLanguage = .auto
        translateAndInsertTargetLanguage = nil
        // Reset PaddleOCR settings
        paddleOCRMode = .fast
        paddleOCRUseCloud = false
        paddleOCRCloudBaseURL = ""
        paddleOCRCloudAPIKey = ""
        paddleOCRCloudModelId = ""
        // Delete PaddleOCR cloud API key from Keychain
        Task.detached {
            do {
                try await KeychainService.shared.deletePaddleOCRCredentials()
            } catch {
                Logger.settings.error("Failed to delete PaddleOCR credentials from keychain: \(error.localizedDescription)")
            }
        }
        // Reset vLLM model directory
        paddleOCRLocalVLModelDir = ""
        // Reset multi-engine configuration - directly create defaults, don't load from persistence
        engineSelectionMode = .primaryWithFallback
        var defaultConfigs: [TranslationEngineType: TranslationEngineConfig] = [:]
        for type in TranslationEngineType.allCases {
            defaultConfigs[type] = .default(for: type)
        }
        engineConfigs = defaultConfigs
        saveEngineConfigs()
        promptConfig = TranslationPromptConfig()
        sceneBindings = SceneEngineBinding.allDefaults
        parallelEngines = [.apple]
        compatibleProviderConfigs = []
    }

    // MARK: - Notifications

    /// Posted when any keyboard shortcut is changed
    static let shortcutDidChangeNotification = Notification.Name("AppSettings.shortcutDidChange")

    // MARK: - Persistence Helpers
    // Made internal so SettingsViewModel can call them explicitly
    // to guarantee persistence (since @Observable does not reliably
    // trigger didSet for value-type subscript mutations).

    func save(_ value: Any, forKey key: String) {
        UserDefaults.standard.set(value, forKey: key)
    }

    func saveShortcut(_ shortcut: KeyboardShortcut, forKey key: String) {
        let data: [String: UInt32] = [
            "keyCode": shortcut.keyCode,
            "modifiers": shortcut.modifiers
        ]
        UserDefaults.standard.set(data, forKey: key)
        NotificationCenter.default.post(name: Self.shortcutDidChangeNotification, object: nil)
    }

    private static func loadShortcut(forKey key: String) -> KeyboardShortcut? {
        guard let data = UserDefaults.standard.dictionary(forKey: key) as? [String: UInt32],
              let keyCode = data["keyCode"],
              let modifiers = data["modifiers"] else {
            return nil
        }
        return KeyboardShortcut(keyCode: keyCode, modifiers: modifiers)
    }

    func saveColor(_ color: CodableColor, forKey key: String) {
        if let data = try? JSONEncoder().encode(color) {
            UserDefaults.standard.set(data, forKey: key)
        }
    }

    private static func loadColor(forKey key: String) -> CodableColor? {
        guard let data = UserDefaults.standard.data(forKey: key) else { return nil }
        return try? JSONDecoder().decode(CodableColor.self, from: data)
    }

    // MARK: - Keychain Helpers

    /// Load PaddleOCR cloud API key from Keychain synchronously
    private static func loadPaddleOCRAPIKeyFromKeychain() -> String {
        KeychainService.loadPaddleOCRAPIKeySynchronously()
    }

    /// Load VLM API key from Keychain synchronously
    private static func loadVLMAPIKeyFromKeychain() -> String {
        KeychainService.loadVLMAPIKeySynchronously()
    }

    /// Refreshes keychain-backed settings after this app session is authenticated.
    func reloadSecureCredentials() {
        let paddleKey = Self.loadPaddleOCRAPIKeyFromKeychain()
        if paddleOCRCloudAPIKey != paddleKey {
            paddleOCRCloudAPIKey = paddleKey
        }

        let vlmKey = vlmAPIKey(for: vlmProvider)
        if vlmAPIKey != vlmKey {
            vlmAPIKey = vlmKey
        }
    }

    // MARK: - Multi-Engine Persistence Helpers

    /// Explicitly save engine configs to UserDefaults.
    /// Call this after modifying `engineConfigs` to ensure persistence,
    /// since `@Observable` does not guarantee `didSet` fires for value-type subscript mutations.
    func saveEngineConfigs() {
        let configArray = Array(engineConfigs.values)
        Logger.settings.info("Saving engine configs: \(configArray.map { "\($0.id.rawValue)=\($0.isEnabled)" })")
        if let data = try? JSONEncoder().encode(configArray) {
            UserDefaults.standard.set(data, forKey: Keys.engineConfigs)
            Logger.settings.info("Engine configs saved (\(data.count) bytes)")
        } else {
            Logger.settings.error("Failed to encode engine configs")
        }
    }

    private static func loadEngineConfigs() -> [TranslationEngineType: TranslationEngineConfig] {
        // Start with defaults
        var result: [TranslationEngineType: TranslationEngineConfig] = [:]
        for type in TranslationEngineType.allCases {
            result[type] = .default(for: type)
        }

        // Load saved configs and merge
        let data = UserDefaults.standard.data(forKey: Keys.engineConfigs)
        Logger.settings.info("Loading engine configs from UserDefaults: data exists = \(data != nil), size = \(data?.count ?? 0)")
        guard let data,
              let configs = try? JSONDecoder().decode([TranslationEngineConfig].self, from: data) else {
            Logger.settings.warning("No saved engine configs found, using defaults")
            return result
        }
        Logger.settings.info("Decoded engine configs: \(configs.map { "\($0.id.rawValue)=\($0.isEnabled)" })")

        // Merge loaded configs over defaults
        result = configs.reduce(into: result) { dict, config in
            dict[config.id] = config
        }

        return result
    }

    private func savePromptConfig() {
        if let data = try? JSONEncoder().encode(promptConfig) {
            UserDefaults.standard.set(data, forKey: Keys.promptConfig)
        }
    }

    private static func loadPromptConfig(
        compatibleConfigs: [CompatibleTranslationProvider.CompatibleConfig]
    ) -> TranslationPromptConfig {
        guard let data = UserDefaults.standard.data(forKey: Keys.promptConfig) else {
            return TranslationPromptConfig()
        }

        if let config = try? JSONDecoder().decode(TranslationPromptConfig.self, from: data) {
            let migratedCompatiblePrompts = migrateCompatiblePromptKeys(
                config.compatibleEnginePrompts,
                compatibleConfigs: compatibleConfigs
            )
            if migratedCompatiblePrompts == config.compatibleEnginePrompts {
                return config
            }

            return TranslationPromptConfig(
                enginePrompts: config.enginePrompts,
                compatibleEnginePrompts: migratedCompatiblePrompts,
                scenePrompts: config.scenePrompts
            )
        }

        struct LegacyTranslationPromptConfig: Decodable {
            var enginePrompts: [TranslationEngineType: String]
            var compatibleEnginePrompts: [Int: String]
            var scenePrompts: [TranslationScene: String]
        }

        guard let legacyConfig = try? JSONDecoder().decode(LegacyTranslationPromptConfig.self, from: data) else {
            return TranslationPromptConfig()
        }

        let migratedCompatiblePrompts = legacyConfig.compatibleEnginePrompts.reduce(into: [String: String]()) {
            result,
            entry in
            let (index, prompt) = entry
            guard compatibleConfigs.indices.contains(index) else { return }
            result[compatibleConfigs[index].id.uuidString] = prompt
        }

        return TranslationPromptConfig(
            enginePrompts: legacyConfig.enginePrompts,
            compatibleEnginePrompts: migratedCompatiblePrompts,
            scenePrompts: legacyConfig.scenePrompts
        )
    }

    private static func migrateCompatiblePromptKeys(
        _ prompts: [String: String],
        compatibleConfigs: [CompatibleTranslationProvider.CompatibleConfig]
    ) -> [String: String] {
        prompts.reduce(into: [String: String]()) { result, entry in
            let (key, value) = entry
            if let index = Int(key), compatibleConfigs.indices.contains(index) {
                result[compatibleConfigs[index].id.uuidString] = value
            } else {
                result[key] = value
            }
        }
    }

    /// Explicitly save scene bindings to UserDefaults.
    func saveSceneBindings() {
        let bindingArray = Array(sceneBindings.values)
        if let data = try? JSONEncoder().encode(bindingArray) {
            UserDefaults.standard.set(data, forKey: Keys.sceneBindings)
        }
    }

    private static func loadSceneBindings() -> [TranslationScene: SceneEngineBinding] {
        // Start with defaults
        var result = SceneEngineBinding.allDefaults

        // Load saved bindings and merge
        guard let data = UserDefaults.standard.data(forKey: Keys.sceneBindings),
              let bindings = try? JSONDecoder().decode([SceneEngineBinding].self, from: data) else {
            return result
        }

        // Merge loaded bindings over defaults
        result = bindings.reduce(into: result) { dict, binding in
            dict[binding.scene] = binding
        }

        return result
    }

    /// Explicitly save parallel engines to UserDefaults.
    func saveParallelEngines() {
        let rawValues = parallelEngines.map { $0.rawValue }
        UserDefaults.standard.set(rawValues, forKey: Keys.parallelEngines)
    }

    private static func loadParallelEngines() -> [TranslationEngineType] {
        guard let rawValues = UserDefaults.standard.array(forKey: Keys.parallelEngines) as? [String] else {
            return [.apple]
        }
        let engines = rawValues.compactMap { TranslationEngineType(rawValue: $0) }
        // Return default if result is empty (dirty data case)
        return engines.isEmpty ? [.apple] : engines
    }

    /// Explicitly save compatible provider configs to UserDefaults.
    func saveCompatibleConfigs() {
        if let data = try? JSONEncoder().encode(compatibleProviderConfigs) {
            UserDefaults.standard.set(data, forKey: Keys.compatibleProviderConfigs)
        }
    }

    private static func loadCompatibleConfigs() -> [CompatibleTranslationProvider.CompatibleConfig] {
        guard let data = UserDefaults.standard.data(forKey: Keys.compatibleProviderConfigs),
              let configs = try? JSONDecoder().decode([CompatibleTranslationProvider.CompatibleConfig].self, from: data) else {
            return []
        }
        return configs
    }

    /// Resolves a security-scoped bookmark to a URL
    private static func resolveBookmark(_ bookmarkData: Data) -> URL? {
        var isStale = false
        do {
            let url = try URL(
                resolvingBookmarkData: bookmarkData,
                options: .withSecurityScope,
                relativeTo: nil,
                bookmarkDataIsStale: &isStale
            )

            // Start accessing the security-scoped resource
            if url.startAccessingSecurityScopedResource() {
                // Note: We don't call stopAccessingSecurityScopedResource()
                // because we need ongoing access throughout the app's lifetime
                return url
            }
            return url
        } catch {
            Logger.settings.error("Failed to resolve bookmark: \(error.localizedDescription)")
            return nil
        }
    }
}
