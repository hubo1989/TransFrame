//
//  CompatibleEngineConfigSheet.swift
//  TransFrame
//
//  Configuration sheet for OpenAI-compatible translation engines
//

import SwiftUI

struct CompatibleEngineConfigSheet: View {
    let config: CompatibleTranslationProvider.CompatibleConfig
    let index: Int
    let isNew: Bool
    let onSave: (CompatibleTranslationProvider.CompatibleConfig) -> Void

    @Environment(\.dismiss) private var dismiss
    @State private var showAPIKey = false
    @State private var apiKey: String = ""
    @State private var displayName: String = ""
    @State private var baseURL: String = ""
    @State private var modelName: String = ""
    @State private var hasAPIKey: Bool = true
    @State private var isTesting = false
    @State private var testResult: String?
    @State private var testSuccess = false

    @State private var availableModels: [String] = []
    @State private var isFetchingModels = false
    @State private var showErrorAlert = false
    @State private var errorMessage = ""

    var body: some View {
        VStack(spacing: 20) {
            // Header
            HStack {
                Image(systemName: "gearshape.2")
                    .font(.title2)
                    .foregroundStyle(.tint)
                VStack(alignment: .leading) {
                    Text(isNew ? localized("engine.compatible.new") : displayName)
                        .font(.headline)
                    Text(localized("engine.compatible.description"))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Spacer()
            }

            Divider()

            // Configuration Form
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    // Display Name
                    VStack(alignment: .leading, spacing: 8) {
                        Text(localized("engine.compatible.displayName"))
                            .font(.subheadline)
                            .foregroundStyle(.secondary)

                        TextField(localized("engine.compatible.displayName.placeholder"), text: $displayName)
                            .textFieldStyle(.roundedBorder)
                    }

                    // Base URL
                    VStack(alignment: .leading, spacing: 8) {
                        Text(localized("engine.config.baseURL"))
                            .font(.subheadline)
                            .foregroundStyle(.secondary)

                        TextField("http://localhost:8000/v1", text: $baseURL)
                            .textFieldStyle(.roundedBorder)
                    }

                    // Model Name
                    VStack(alignment: .leading, spacing: 8) {
                        Text(localized("engine.config.model"))
                            .font(.subheadline)
                            .foregroundStyle(.secondary)

                        HStack {
                            TextField("gpt-4o-mini", text: $modelName)
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

                    // API Key Toggle
                    Toggle(isOn: $hasAPIKey) {
                        Text(localized("engine.compatible.requireApiKey"))
                    }
                    .toggleStyle(.switch)

                    // API Key (if required)
                    if hasAPIKey {
                        VStack(alignment: .leading, spacing: 8) {
                            Text(localized("engine.config.apiKey"))
                                .font(.subheadline)
                                .foregroundStyle(.secondary)

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
                    dismiss()
                }
                .buttonStyle(.bordered)

                Button(localized("button.save")) {
                    saveConfig()
                    dismiss()
                }
                .buttonStyle(.borderedProminent)
                .disabled(displayName.isEmpty || baseURL.isEmpty || modelName.isEmpty)
            }
        }
        .padding()
        .frame(width: 500, height: 480)
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

    // MARK: - Computed Properties

    private var canTest: Bool {
        !displayName.isEmpty && !baseURL.isEmpty && !modelName.isEmpty && (!hasAPIKey || !apiKey.isEmpty)
    }

    // MARK: - Actions

    private func loadConfig() {
        displayName = config.displayName
        baseURL = config.baseURL
        modelName = config.modelName
        hasAPIKey = config.hasAPIKey

        // Load credentials from keychain
        Task {
            let compositeId = config.keychainId
            if let credentials = try? await KeychainService.shared.getCredentials(forCompatibleId: compositeId) {
                await MainActor.run {
                    apiKey = credentials.apiKey
                }
            }
        }
    }

    private func saveConfig() {
        // For new engines, generate a new ID; for existing, keep the original
        let configId = isNew ? UUID() : config.id

        let savedConfig = CompatibleTranslationProvider.CompatibleConfig(
            id: configId,
            displayName: displayName,
            baseURL: baseURL,
            modelName: modelName,
            hasAPIKey: hasAPIKey
        )

        onSave(savedConfig)

        // Save credentials to keychain
        Task {
            let compositeId = savedConfig.keychainId
            if hasAPIKey && !apiKey.isEmpty {
                try? await KeychainService.shared.saveCredentials(apiKey: apiKey, forCompatibleId: compositeId)
            } else {
                try? await KeychainService.shared.deleteCredentials(forCompatibleId: compositeId)
            }
        }
    }

    private func testConnection() async {
        isTesting = true
        testResult = nil

        do {
            // Save credentials temporarily for testing
            if hasAPIKey {
                let compositeId = config.keychainId
                try await KeychainService.shared.saveCredentials(apiKey: apiKey, forCompatibleId: compositeId)
            }

            // Create a temporary config for testing
            let tempConfig = CompatibleTranslationProvider.CompatibleConfig(
                id: config.id,
                displayName: displayName,
                baseURL: baseURL,
                modelName: modelName,
                hasAPIKey: hasAPIKey
            )

            // Test by creating a provider and calling checkConnection
            let engineConfig = TranslationEngineConfig.default(for: .custom)
            let provider = try await CompatibleTranslationProvider(
                config: engineConfig,
                compatibleConfig: tempConfig,
                keychain: KeychainService.shared
            )

            try await provider.verifyConnection()

            await MainActor.run {
                testSuccess = true
                testResult = localized("engine.config.test.success")
                isTesting = false
            }
        } catch {
            await MainActor.run {
                testSuccess = false
                testResult = error.localizedDescription
                isTesting = false
            }
        }
    }

    @MainActor
    private func fetchModels() async {
        isFetchingModels = true
        errorMessage = ""

        let requestURL = baseURL.isEmpty ? "http://localhost:8000/v1" : baseURL

        do {
            let models = try await ModelDiscoveryService.fetchModels(
                baseURL: requestURL,
                apiKey: hasAPIKey ? apiKey : nil,
                engineType: "custom"
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
