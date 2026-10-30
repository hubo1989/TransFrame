//
//  EngineConfigSheet.swift
//  TransFrame
//
//  Configuration sheet for individual translation engines
//

import SwiftUI
import os

struct EngineConfigSheet: View {
    let engine: TranslationEngineType
    @Binding var config: TranslationEngineConfig

    /// The proxied .ollama configuration when the OpenAI-compatible card
    /// runs the Ollama preset. Loaded on appear, saved via onOllamaProxySave;
    /// the .openai binding stays untouched by the preset (Codex P2 fixes).
    @State var ollamaProxyConfig: TranslationEngineConfig? = nil

    @Environment(\.dismiss) private var dismiss
    @State private var showAPIKey = false
    @State private var apiKey: String = ""
    @State private var appID: String = ""
    @State private var secretKey: String = ""
    @State var baseURL: String = ""
    @State var modelName: String = ""
    @State private var isTesting = false
    @State private var testResult: String?
    @State private var testSuccess = false
    /// OpenAI-compatible preset: OpenAI official / Ollama local / custom.
    /// Only used when engine == .openai (inventory convergence, PRD US-003).
    @State var openAIPreset: OpenAICompatiblePreset = .openAI
    /// Receives the proxied save for the Ollama preset (owned by the parent).
    var onOllamaProxySave: ((TranslationEngineConfig) -> Void)? = nil
    private let logger = Logger.settings

    @State private var availableModels: [String] = []
    @State private var isFetchingModels = false
    @State private var showErrorAlert = false
    @State private var errorMessage = ""
    @State private var originalCredentials: (apiKey: String, additionalData: [String: String]?)?
    @State private var hasModifiedCredentialsForTesting = false
    @State private var isSaved = false

    var body: some View {
        VStack(spacing: 20) {
            // Header
            HStack {
                Image(systemName: engineIcon)
                    .font(.title2)
                    .foregroundStyle(.tint)
                VStack(alignment: .leading) {
                    Text(engine.localizedName)
                        .font(.headline)
                    Text(engine.engineDescription)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Spacer()
            }

            Divider()

            // Configuration Form
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    // Preset picker for the converged OpenAI-compatible card
                    if engine == .openai {
                        openAIPresetSection
                    }

                    // Enable Toggle
                    Toggle(isOn: Binding(
                        get: { effectiveIsEnabled },
                        set: { effectiveIsEnabled = $0 }
                    )) {
                        Text(localized("engine.config.enabled"))
                    }
                    .toggleStyle(.switch)

                    // API Key (if required). Ollama preset and keyless local
                    // custom endpoints don't need one.
                    if engine.requiresAPIKey
                        && !(engine == .openai && openAIPreset == .ollamaLocal)
                        && !(engine == .openai && openAIPreset == .custom && Self.isLocalEndpoint(baseURL)) {
                        if engine == .baidu {
                            // Baidu requires AppID and Secret Key
                            baiduCredentialsSection
                        } else {
                            apiKeySection
                        }
                    }

                    // MTranServer URL configuration
                    if engine == .mtranServer {
                        mtranServerURLSection
                    }

                    // Base URL (if applicable)
                    if engine.defaultBaseURL != nil || engine == .custom {
                        baseURLSection
                    }

                    // Model Name (for LLM engines)
                    if engine.defaultModelName != nil || engine == .custom {
                        modelNameSection
                    }
                }
                .padding()
            }

            Divider()

            // Footer
            HStack {
                // Test Button
                Button {
                    Task { await testConnection() }
                } label: {
                    HStack(spacing: 6) {
                        if isTesting {
                            ProgressView()
                                .controlSize(.small)
                        }
                        Image(systemName: "bolt.fill")
                        Text(localized("engine.config.test"))
                    }
                }
                .buttonStyle(.bordered)
                .controlSize(.regular)
                .disabled(isTesting || !canTest)

                if let result = testResult {
                    HStack(spacing: 4) {
                        Image(systemName: testSuccess ? "checkmark.circle.fill" : "xmark.circle.fill")
                            .foregroundStyle(testSuccess ? Color.green : Color.red)
                        Text(result)
                            .font(.caption)
                            .foregroundStyle(testSuccess ? .secondary : Color.red)
                            .lineLimit(2)
                    }
                }

                Spacer()

                Button(localized("button.cancel")) {
                    rollbackTemporaryCredentialsIfNeeded()
                    dismiss()
                }
                .buttonStyle(.bordered)

                Button(localized("button.save")) {
                    isSaved = true
                    saveConfig()
                    dismiss()
                }
                .buttonStyle(.borderedProminent)
                .disabled(!config.isEnabled && !hasValidConfig)
            }
        }
        .padding()
        .frame(width: 500, height: 450)
        .onAppear {
            loadConfig()
        }
        .alert(isPresented: $showErrorAlert) {
            Alert(
                title: Text(localized("engine.config.fetchModels.failed")),
                message: Text(errorMessage),
                dismissButton: .default(Text(localized("button.ok")))
            )
        }
    }

    // MARK: - Effective Engine (preset proxy)

    /// Engine whose config the sheet actually edits under the active preset.
    private var effectiveEngine: TranslationEngineType {
        (engine == .openai && openAIPreset == .ollamaLocal) ? .ollama : engine
    }

    /// Effective enable state: Ollama preset routes to the proxy config so
    /// the .openai binding is never left enabled behind the preset.
    private var effectiveIsEnabled: Bool {
        get {
            if engine == .openai, openAIPreset == .ollamaLocal, let proxy = ollamaProxyConfig {
                return proxy.isEnabled
            }
            return config.isEnabled
        }
        nonmutating set {
            if engine == .openai, openAIPreset == .ollamaLocal {
                var proxy = ollamaProxyConfig ?? .default(for: .ollama)
                proxy.isEnabled = newValue
                ollamaProxyConfig = proxy
            } else {
                config.isEnabled = newValue
            }
        }
    }

    // MARK: - View Components

    /// Preset template section for the OpenAI-compatible card. Choosing the
    /// Ollama preset pre-fills the local endpoint and proxies the save to
    /// the .ollama engine config (provider behavior is keyed by engine type).
    private var openAIPresetSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(localized("engine.config.preset.title"))
                .font(.subheadline)
                .foregroundStyle(.secondary)

            Picker(localized("engine.config.preset.title"), selection: $openAIPreset) {
                ForEach(OpenAICompatiblePreset.allCases) { preset in
                    Text(preset.localizedName).tag(preset)
                }
            }
            .pickerStyle(.segmented)
            .onChange(of: openAIPreset) { _, newValue in
                newValue.apply(to: self)
            }
        }
    }

    @ViewBuilder
    private var apiKeySection: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(localized("engine.config.apiKey"))
                    .font(.subheadline)
                    .foregroundStyle(.secondary)

                Spacer()

                // Get API Key button
                if let url = engine.apiKeyURL {
                    Button {
                        NSWorkspace.shared.open(url)
                    } label: {
                        HStack(spacing: 4) {
                            Image(systemName: "link")
                            Text(localized("engine.config.getApiKey"))
                        }
                        .font(.caption)
                    }
                    .buttonStyle(.plain)
                    .foregroundStyle(.tint)
                }
            }

            HStack {
                if showAPIKey {
                    TextField(localized("engine.config.apiKey.placeholder"), text: $apiKey)
                        .textFieldStyle(.roundedBorder)
                } else {
                    SecureField(localized("engine.config.apiKey.placeholder"), text: $apiKey)
                        .textFieldStyle(.roundedBorder)
                }
                Button {
                    showAPIKey.toggle()
                } label: {
                    Image(systemName: showAPIKey ? "eye.slash" : "eye")
                }
                .buttonStyle(.borderless)
            }
        }
    }

    @ViewBuilder
    private var baiduCredentialsSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text(localized("engine.config.baidu.credentials"))
                    .font(.subheadline)
                    .foregroundStyle(.secondary)

                Spacer()

                // Get API Key button
                if let url = engine.apiKeyURL {
                    Button {
                        NSWorkspace.shared.open(url)
                    } label: {
                        HStack(spacing: 4) {
                            Image(systemName: "link")
                            Text(localized("engine.config.getApiKey"))
                        }
                        .font(.caption)
                    }
                    .buttonStyle(.plain)
                    .foregroundStyle(.tint)
                }
            }

            Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 8) {
                GridRow {
                    Text(localized("engine.config.baidu.appID"))
                        .gridColumnAlignment(.trailing)
                    TextField("App ID", text: $appID)
                        .textFieldStyle(.roundedBorder)
                }

                GridRow {
                    Text(localized("engine.config.baidu.secretKey"))
                        .gridColumnAlignment(.trailing)
                    SecureField("Secret Key", text: $secretKey)
                        .textFieldStyle(.roundedBorder)
                }
            }
        }
    }

    @ViewBuilder
    private var mtranServerURLSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(localized("engine.config.mtran.url"))
                .font(.subheadline)
                .foregroundStyle(.secondary)

            TextField("http://localhost:8989", text: $baseURL)
                .textFieldStyle(.roundedBorder)
        }
    }

    @ViewBuilder
    private var baseURLSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(localized("engine.config.baseURL"))
                .font(.subheadline)
                .foregroundStyle(.secondary)

            TextField(engine.defaultBaseURL ?? "", text: $baseURL)
                .textFieldStyle(.roundedBorder)
        }
    }

    @ViewBuilder
    private var modelNameSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(localized("engine.config.model"))
                .font(.subheadline)
                .foregroundStyle(.secondary)

            HStack {
                TextField(engine.defaultModelName ?? "", text: $modelName)
                    .textFieldStyle(.roundedBorder)

                if !availableModels.isEmpty {
                    Menu {
                        ForEach(availableModels, id: \.self) { model in
                            Button(model) {
                                modelName = model
                            }
                        }
                    } label: {
                        Image(systemName: "chevron.up.chevron.down")
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                    }
                    .accessibilityLabel(localized("settings.model.select"))
                    .menuStyle(.borderlessButton)
                    .fixedSize()
                }

                Button {
                    Task {
                        await fetchModels()
                    }
                } label: {
                    if isFetchingModels {
                        ProgressView()
                            .controlSize(.small)
                    } else {
                        Text(localized("engine.config.fetchModels"))
                    }
                }
                .buttonStyle(.bordered)
                .disabled(isFetchingModels)
            }
        }
    }

    // MARK: - Computed Properties

    private var engineIcon: String {
        switch engine {
        case .apple: return "apple.logo"
        case .mtranServer: return "server.rack"
        case .openai: return "brain.head.profile"
        case .claude: return "bubble.left.and.bubble.right"
        case .gemini: return "sparkles"
        case .ollama: return "cpu"
        case .google: return "globe"
        case .deepl: return "character.bubble"
        case .baidu: return "network"
        case .custom: return "gearshape.2"
        }
    }

    /// Whether the given base URL points at a local endpoint (localhost /
    /// loopback / *.local) that may run keyless.
    static func isLocalEndpoint(_ urlString: String) -> Bool {
        guard let host = URL(string: urlString)?.host else { return false }
        let lowered = host.lowercased()
        return ["localhost", "127.0.0.1", "::1", "0.0.0.0"].contains(lowered)
            || lowered.hasSuffix(".local")
    }

    private var canTest: Bool {
        // Keyless local custom endpoints test without an API key
        if engine == .openai, openAIPreset == .custom, Self.isLocalEndpoint(baseURL) {
            return effectiveIsEnabled
        }
        if !effectiveIsEnabled { return false }
        if effectiveEngine.requiresAPIKey {
            if effectiveEngine == .baidu {
                return !appID.isEmpty && !secretKey.isEmpty
            }
            return !apiKey.isEmpty
        }
        return true
    }

    private var hasValidConfig: Bool {
        if engine == .openai, openAIPreset == .custom, Self.isLocalEndpoint(baseURL) {
            return true
        }
        if effectiveEngine.requiresAPIKey {
            if effectiveEngine == .baidu {
                return !appID.isEmpty && !secretKey.isEmpty
            }
            return !apiKey.isEmpty
        }
        return true
    }

    // MARK: - Actions

    /// Pure function: the .ollama config produced by the Ollama preset
    /// proxy save (extracted so the mapping is testable without a view).
    static func makeOllamaProxyConfig(
        isEnabled: Bool,
        baseURL: String,
        modelName: String,
        options: EngineOptions?
    ) -> TranslationEngineConfig {
        var ollamaConfig = TranslationEngineConfig(id: .ollama, isEnabled: isEnabled)
        var resolved = options ?? EngineOptions.default(for: .ollama) ?? EngineOptions()
        resolved.baseURL = baseURL.isEmpty ? TranslationEngineType.ollama.defaultBaseURL : baseURL
        resolved.modelName = modelName.isEmpty ? TranslationEngineType.ollama.defaultModelName : modelName
        ollamaConfig.options = resolved
        return ollamaConfig
    }

    /// Testing hook: loadConfig without view lifecycle.
    func loadConfigForTesting() { loadConfig() }

    private func loadConfig() {
        // Ollama preset edits the proxied .ollama config (Codex P2): reopening
        // must show the saved Ollama URL/model, not OpenAI defaults.
        let source: TranslationEngineConfig =
            (engine == .openai && ollamaProxyConfig != nil) ? ollamaProxyConfig! : config
        baseURL = source.options?.baseURL ?? effectiveEngine.defaultBaseURL ?? ""
        modelName = source.options?.modelName ?? effectiveEngine.defaultModelName ?? ""
        if engine == .openai {
            openAIPreset = .inferred(fromBaseURL: baseURL)
        }

        // Load credentials from keychain
        Task {
            if let credentials = try? await KeychainService.shared.getCredentials(for: engine) {
                apiKey = credentials.apiKey
                appID = credentials.appID ?? ""
                secretKey = credentials.additional?["secretKey"] ?? ""
                originalCredentials = (credentials.apiKey, credentials.additional)
            }
        }
    }

    private func saveConfig() {
        saveConfigBody(persistCredentials: true)
    }

    /// Testing hook: same path as the Save button without touching the keychain.
    func saveConfigForTesting() {
        saveConfigBody(persistCredentials: false)
    }

    private func saveConfigBody(persistCredentials: Bool) {
        // Ollama preset: proxy the save into the .ollama engine config.
        // Provider behavior (endpoint shape, no-API-key) is keyed by engine
        // type, so a local-endpoint OpenAI config only works under .ollama.
        // Uses the proxy enable state — the .openai binding is never touched
        // under the preset (Codex P2: no phantom-enabled OpenAI entry).
        if engine == .openai, openAIPreset == .ollamaLocal {
            onOllamaProxySave?(Self.makeOllamaProxyConfig(
                isEnabled: effectiveIsEnabled,
                baseURL: baseURL,
                modelName: modelName,
                options: ollamaProxyConfig?.options
            ))
            return
        }

        // Update options
        var options = config.options ?? EngineOptions.default(for: engine) ?? EngineOptions()
        if !baseURL.isEmpty {
            options.baseURL = baseURL
        }
        if !modelName.isEmpty {
            options.modelName = modelName
        }
        config.options = options

        // Save credentials to keychain
        guard persistCredentials else { return }
        Task {
            do {
                if engine == .baidu {
                    try await KeychainService.shared.saveCredentials(
                        apiKey: secretKey,
                        for: engine,
                        additionalData: ["appID": appID, "secretKey": secretKey]
                    )
                } else if engine.requiresAPIKey && !apiKey.isEmpty {
                    try await KeychainService.shared.saveCredentials(
                        apiKey: apiKey,
                        for: engine
                    )
                }
            } catch {
                logger.error("Failed to save credentials: \(error.localizedDescription, privacy: .private(mask: .hash))")
            }
        }
    }

    private func testConnection() async {
        isTesting = true
        testResult = nil
        let targetEngine = effectiveEngine

        do {
            // Save credentials temporarily for testing (Ollama preset and
            // keyless local custom endpoints never reach this branch)
            if targetEngine.requiresAPIKey
                && !(engine == .openai && openAIPreset == .custom && Self.isLocalEndpoint(baseURL)) {
                hasModifiedCredentialsForTesting = true
                if targetEngine == .baidu {
                    try await KeychainService.shared.saveCredentials(
                        apiKey: secretKey,
                        for: targetEngine,
                        additionalData: ["appID": appID, "secretKey": secretKey]
                    )
                } else {
                    try await KeychainService.shared.saveCredentials(
                        apiKey: apiKey,
                        for: targetEngine
                    )
                }
            }

            // Test connection with the unsaved form snapshot, not the old engine config.
            // Empty form fields mean "keep the saved value" (same contract as
            // saveConfig), so the probe must not hit a different URL than the
            // one that will actually be persisted.
            let sourceConfig: TranslationEngineConfig =
                (targetEngine == .ollama && engine == .openai) ? (ollamaProxyConfig ?? config) : config
            var testConfig = sourceConfig
            var options = sourceConfig.options ?? EngineOptions.default(for: targetEngine) ?? EngineOptions()
            if !baseURL.isEmpty { options.baseURL = baseURL }
            if !modelName.isEmpty { options.modelName = modelName }
            testConfig.options = options
            try await TranslationService.shared.verifyConnection(for: targetEngine, config: testConfig)

            await MainActor.run {
                testSuccess = true
                testResult = localized("engine.config.test.success")
                isTesting = false
            }
        } catch {
            rollbackTemporaryCredentialsIfNeeded()
            await MainActor.run {
                testSuccess = false
                testResult = error.localizedDescription
                isTesting = false
            }
        }
    }

    private func rollbackTemporaryCredentialsIfNeeded() {
        guard hasModifiedCredentialsForTesting && !isSaved else { return }
        hasModifiedCredentialsForTesting = false

        Task {
            if let original = originalCredentials {
                _ = try? await KeychainService.shared.saveCredentials(
                    apiKey: original.apiKey,
                    for: engine,
                    additionalData: original.additionalData
                )
            } else if engine.requiresAPIKey {
                _ = try? await KeychainService.shared.deleteCredentials(for: engine)
            }
        }
    }

    @MainActor
    private func fetchModels() async {
        isFetchingModels = true
        errorMessage = ""

        let requestURL = baseURL.isEmpty ? (engine.defaultBaseURL ?? "") : baseURL

        do {
            let models = try await ModelDiscoveryService.fetchModels(
                baseURL: requestURL,
                apiKey: engine.requiresAPIKey ? apiKey : nil,
                engineType: engine.rawValue
            )
            self.availableModels = models
            self.isFetchingModels = false
        } catch {
            self.errorMessage = error.localizedDescription
            self.showErrorAlert = true
            self.isFetchingModels = false
        }
    }
}

// MARK: - OpenAI-Compatible Preset

/// Preset templates for the converged OpenAI-compatible card (PRD US-003).
enum OpenAICompatiblePreset: String, CaseIterable, Identifiable {
    case openAI
    case ollamaLocal
    case custom

    var id: String { rawValue }

    var localizedName: String {
        switch self {
        case .openAI:
            return NSLocalizedString("engine.preset.openai", comment: "OpenAI official")
        case .ollamaLocal:
            return NSLocalizedString("engine.preset.ollama", comment: "Ollama local")
        case .custom:
            return NSLocalizedString("engine.preset.custom", comment: "Custom")
        }
    }

    /// Pre-fill the sheet's editable fields for this preset.
    func apply(to sheet: EngineConfigSheet) {
        switch self {
        case .openAI:
            sheet.baseURL = TranslationEngineType.openai.defaultBaseURL ?? ""
            sheet.modelName = TranslationEngineType.openai.defaultModelName ?? ""
        case .ollamaLocal:
            sheet.baseURL = TranslationEngineType.ollama.defaultBaseURL ?? ""
            sheet.modelName = TranslationEngineType.ollama.defaultModelName ?? ""
        case .custom:
            break // keep whatever the user typed
        }
    }

    /// Best-effort preset detection when reopening the sheet.
    static func inferred(fromBaseURL baseURL: String) -> OpenAICompatiblePreset {
        switch baseURL {
        case TranslationEngineType.openai.defaultBaseURL:
            return .openAI
        case TranslationEngineType.ollama.defaultBaseURL:
            return .ollamaLocal
        default:
            return .custom
        }
    }
}
