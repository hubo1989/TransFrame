import SwiftUI

struct EngineSettingsContent: View {
    @Bindable var viewModel: SettingsViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            // Credential authorization (Keychain access)
            CredentialAccessRow(viewModel: viewModel)
                .frame(maxWidth: .infinity, alignment: .leading)
                .macosGroupedCard()

            // VLM Configuration (for image analysis)
            VLMConfigurationSection(viewModel: viewModel)
                .frame(maxWidth: .infinity, alignment: .leading)
                .macosGroupedCard()

            // Multi-Engine Translation Configuration
            MultiEngineSettingsSection(viewModel: viewModel)
        }
    }
}

struct VLMConfigurationSection: View {
    @Bindable var viewModel: SettingsViewModel
    @State private var showAPIKey = false

    @State private var availableVLMModels: [String] = []
    @State private var isFetchingVLMModels = false
    @State private var showErrorAlert = false
    @State private var errorMessage = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(localized("settings.vlm.title"))
                .font(.headline)

            // Provider selection row (full width segmented picker)
            VStack(alignment: .leading, spacing: 8) {
                Text(localized("settings.vlm.provider"))
                    .font(.subheadline)
                    .foregroundStyle(.secondary)

                Picker(localized("settings.vlm.provider"), selection: $viewModel.vlmProvider) {
                    ForEach(VLMProviderType.allCases) { provider in
                        Text(provider.localizedName).tag(provider)
                    }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
            }

            // PaddleOCR specific section
            if viewModel.vlmProvider == .paddleocr {
                PaddleOCRStatusSection(viewModel: viewModel)
            } else {
                VStack(alignment: .leading, spacing: 12) {
                    // OpenAI specific API mode selection
                    if viewModel.vlmProvider == .openai {
                        HStack(alignment: .center, spacing: 12) {
                            Text(localized("settings.vlm.apiMode"))
                                .font(.body)
                                .foregroundStyle(.secondary)
                                .frame(width: 84, alignment: .trailing)

                            Picker(localized("settings.vlm.apiMode"), selection: $viewModel.vlmEditingOpenAIMode) {
                                ForEach(OpenAIAPIMode.allCases) { mode in
                                    Text(mode.displayName).tag(mode)
                                }
                            }
                            .pickerStyle(.segmented)
                            .labelsHidden()
                            .fixedSize()

                            Text(viewModel.vlmEditingOpenAIMode.endpointHint)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }

                    // API Key
                    HStack(alignment: .center, spacing: 12) {
                        Text(localized("settings.vlm.apiKey"))
                            .font(.body)
                            .foregroundStyle(.secondary)
                            .frame(width: 84, alignment: .trailing)

                        HStack(spacing: 6) {
                            if showAPIKey {
                                TextField("", text: $viewModel.vlmEditingAPIKey)
                                    .textFieldStyle(.roundedBorder)
                                    .frame(width: 280)
                            } else {
                                SecureField("", text: $viewModel.vlmEditingAPIKey)
                                    .textFieldStyle(.roundedBorder)
                                    .frame(width: 280)
                            }
                            Button {
                                showAPIKey.toggle()
                            } label: {
                                Image(systemName: showAPIKey ? "eye.slash" : "eye")
                            }
                            .buttonStyle(.borderless)
                        }

                        if !viewModel.vlmProvider.typicallyRequiresAPIKey {
                            Text(localized("settings.vlm.apiKey.optional"))
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }

                    // Base URL
                    HStack(alignment: .center, spacing: 12) {
                        Text(localized("settings.vlm.baseURL"))
                            .font(.body)
                            .foregroundStyle(.secondary)
                            .frame(width: 84, alignment: .trailing)

                        HStack(spacing: 6) {
                            TextField("", text: $viewModel.vlmEditingBaseURL)
                                .textFieldStyle(.roundedBorder)
                                .frame(width: 280)

                            if viewModel.vlmProvider == .ollama {
                                Menu {
                                    ForEach(VLMProviderType.localEndpointPresets) { preset in
                                        Button("\(preset.name) (\(preset.url))") {
                                            viewModel.vlmEditingBaseURL = preset.url
                                        }
                                    }
                                } label: {
                                    Image(systemName: "list.bullet")
                                }
                                .menuStyle(.borderlessButton)
                                .help(localized("settings.vlm.preset.help"))
                            }
                        }
                    }

                    // Model Name
                    HStack(alignment: .center, spacing: 12) {
                        Text(localized("settings.vlm.model"))
                            .font(.body)
                            .foregroundStyle(.secondary)
                            .frame(width: 84, alignment: .trailing)

                        HStack(spacing: 6) {
                            TextField("", text: $viewModel.vlmEditingModelName)
                                .textFieldStyle(.roundedBorder)
                                .frame(width: 280)

                            if !availableVLMModels.isEmpty {
                                Menu {
                                    ForEach(availableVLMModels, id: \.self) { model in
                                        Button(model) {
                                            viewModel.vlmEditingModelName = model
                                        }
                                    }
                                } label: {
                                    Image(systemName: "chevron.up.chevron.down")
                                        .font(.caption2)
                                }
                                .menuStyle(.borderlessButton)
                                .fixedSize()
                            }

                            Button {
                                Task {
                                    await fetchVLMModels()
                                }
                            } label: {
                                if isFetchingVLMModels {
                                    ProgressView()
                                        .controlSize(.small)
                                } else {
                                    Text(localized("engine.config.fetchModels"))
                                }
                            }
                            .buttonStyle(.bordered)
                            .disabled(isFetchingVLMModels)
                        }
                    }

                    Text(viewModel.currentVLMProviderDescription)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .padding(.leading, 96)

                    // Action buttons: Save & Test Connection
                    HStack(spacing: 12) {
                        Spacer().frame(width: 84)

                        Button {
                            viewModel.saveVLMConfig()
                        } label: {
                            HStack(spacing: 4) {
                                Image(systemName: "square.and.arrow.down")
                                Text(localized("settings.vlm.saveConfig"))
                            }
                        }
                        .buttonStyle(.borderedProminent)
                        .controlSize(.small)

                        Button {
                            viewModel.testVLMAPI()
                        } label: {
                            HStack(spacing: 6) {
                                if viewModel.isTestingVLM {
                                    ProgressView()
                                        .controlSize(.small)
                                }
                                Image(systemName: "bolt.fill")
                                Text(localized("settings.vlm.test.button"))
                            }
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                        .disabled(viewModel.isTestingVLM)

                        if viewModel.vlmJustSaved {
                            HStack(spacing: 4) {
                                Image(systemName: "checkmark.circle.fill")
                                    .foregroundStyle(.green)
                                Text(localized("settings.vlm.saved"))
                                    .font(.caption)
                                    .foregroundStyle(.green)
                            }
                        } else if viewModel.hasUnsavedVLMChanges {
                            Text(localized("settings.vlm.unsavedChanges"))
                                .font(.caption)
                                .foregroundStyle(.orange)
                        }

                        Spacer()

                        if let result = viewModel.vlmTestResult {
                            HStack(spacing: 4) {
                                Image(systemName: viewModel.vlmTestSuccess ? "checkmark.circle.fill" : "xmark.circle.fill")
                                    .foregroundStyle(viewModel.vlmTestSuccess ? Color.green : Color.red)
                                Text(result)
                                    .font(.caption)
                                    .foregroundStyle(viewModel.vlmTestSuccess ? .secondary : Color.red)
                                    .lineLimit(2)
                            }
                        }
                    }
                    .padding(.top, 4)
                }
            }
        }
        .alert(isPresented: $showErrorAlert) {
            Alert(
                title: Text(localized("engine.config.fetchModels.failed")),
                message: Text(errorMessage),
                dismissButton: .default(Text(localized("button.ok")))
            )
        }
        .onChange(of: viewModel.vlmProvider) { _, _ in
            availableVLMModels = []
        }
    }

    @MainActor
    private func fetchVLMModels() async {
        isFetchingVLMModels = true
        errorMessage = ""

        let provider = viewModel.vlmProvider
        let baseURL = viewModel.vlmEditingBaseURL.isEmpty ? provider.defaultBaseURL : viewModel.vlmEditingBaseURL
        let apiKey = viewModel.vlmEditingAPIKey

        do {
            let models = try await ModelDiscoveryService.fetchModels(
                baseURL: baseURL,
                apiKey: apiKey.isEmpty ? nil : apiKey,
                engineType: provider.rawValue
            )
            self.availableVLMModels = models
            self.isFetchingVLMModels = false
        } catch {
            self.errorMessage = error.localizedDescription
            self.showErrorAlert = true
            self.isFetchingVLMModels = false
        }
    }
}

// MARK: - PaddleOCR Status Section

struct PaddleOCRStatusSection: View {
    @Bindable var viewModel: SettingsViewModel

    @State private var availablePaddleModels: [String] = []
    @State private var isFetchingPaddleModels = false
    @State private var showErrorAlert = false
    @State private var errorMessage = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            // Status
            HStack {
                Image(systemName: viewModel.isPaddleOCRInstalled ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                    .foregroundStyle(viewModel.isPaddleOCRInstalled ? .green : .orange)

                if viewModel.isPaddleOCRInstalled {
                    Text(localized("settings.paddleocr.ready"))
                        .foregroundStyle(.secondary)
                    if let version = viewModel.paddleOCRVersion, !version.isEmpty {
                        Text("(\(version))")
                            .font(.caption)
                            .foregroundStyle(.tertiary)
                    }
                } else {
                    Text(localized("settings.paddleocr.not.installed.message"))
                        .foregroundStyle(.secondary)
                }
            }

            // Mode selection
            Grid(alignment: .leading, horizontalSpacing: 16, verticalSpacing: 12) {
                GridRow {
                    Text(localized("settings.paddleocr.mode"))
                        .foregroundStyle(.secondary)
                        .gridColumnAlignment(.trailing)
                    Picker(localized("settings.paddleocr.mode"), selection: $viewModel.paddleOCRMode) {
                        ForEach(PaddleOCRMode.allCases, id: \.self) { mode in
                            VStack(alignment: .leading) {
                                Text(mode.localizedName)
                            }.tag(mode)
                        }
                    }
                    .pickerStyle(.segmented)
                    .labelsHidden()
                    .frame(maxWidth: 300)
                }

                // Mode description
                GridRow {
                    Color.clear.gridCellUnsizedAxes([.horizontal, .vertical])
                    Text(viewModel.paddleOCRMode.description)
                        .font(.caption)
                        .foregroundStyle(.tertiary)
                }

                // Cloud API toggle
                GridRow {
                    Text(localized("settings.paddleocr.useCloud"))
                        .foregroundStyle(.secondary)
                        .gridColumnAlignment(.trailing)
                    Toggle(localized("settings.paddleocr.useCloud"), isOn: $viewModel.paddleOCRUseCloud)
                        .toggleStyle(.checkbox)
                        .labelsHidden()
                }

                // Cloud API settings (only show when useCloud is true)
                if viewModel.paddleOCRUseCloud {
                    GridRow {
                        Text(localized("settings.paddleocr.cloudBaseURL"))
                            .foregroundStyle(.secondary)
                            .gridColumnAlignment(.trailing)
                        TextField(localized("settings.paddleocr.cloudBaseURL"), text: $viewModel.paddleOCRCloudBaseURL)
                            .textFieldStyle(.roundedBorder)
                            .frame(maxWidth: 300)
                    }

                    GridRow {
                        Text(localized("settings.paddleocr.cloudAPIKey"))
                            .foregroundStyle(.secondary)
                            .gridColumnAlignment(.trailing)
                        SecureField(localized("settings.paddleocr.cloudAPIKey"), text: $viewModel.paddleOCRCloudAPIKey)
                            .textFieldStyle(.roundedBorder)
                            .frame(maxWidth: 300)
                    }

                    GridRow {
                        Text(localized("settings.paddleocr.cloudModelId"))
                            .foregroundStyle(.secondary)
                            .gridColumnAlignment(.trailing)
                        HStack {
                            TextField(localized("settings.paddleocr.cloudModelId"), text: $viewModel.paddleOCRCloudModelId)
                                .textFieldStyle(.roundedBorder)

                            if !availablePaddleModels.isEmpty {
                                Menu {
                                    ForEach(availablePaddleModels, id: \.self) { model in
                                        Button(model) {
                                            viewModel.paddleOCRCloudModelId = model
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
                                    await fetchPaddleModels()
                                }
                            } label: {
                                if isFetchingPaddleModels {
                                    ProgressView()
                                        .controlSize(.small)
                                } else {
                                    Text(localized("engine.config.fetchModels"))
                                }
                            }
                            .buttonStyle(.bordered)
                            .disabled(isFetchingPaddleModels)
                        }
                        .frame(maxWidth: 300)
                    }
                }

                // Local model directory for vllm backend (only show when mode is precise and not using cloud)
                if viewModel.paddleOCRMode == .precise && !viewModel.paddleOCRUseCloud {
                    Divider()
                        .gridCellUnsizedAxes(.horizontal)

                    GridRow {
                        Text(localized("settings.paddleocr.localVLModelDir"))
                            .foregroundStyle(.secondary)
                            .gridColumnAlignment(.trailing)
                        VStack(alignment: .leading, spacing: 4) {
                            TextField("", text: $viewModel.paddleOCRLocalVLModelDir)
                                .textFieldStyle(.roundedBorder)
                                .frame(maxWidth: 300)
                            Text(localized("settings.paddleocr.localVLModelDir.hint"))
                                .font(.caption)
                                .foregroundStyle(.tertiary)
                        }
                    }
                }
            }

            // Description
            Text(localized("settings.paddleocr.description"))
                .font(.caption)
                .foregroundStyle(.tertiary)

            // Install instructions or button
            if !viewModel.isPaddleOCRInstalled {
                VStack(alignment: .leading, spacing: 8) {
                    if viewModel.isInstallingPaddleOCR {
                        HStack {
                            ProgressView()
                                .controlSize(.small)
                            Text(localized("settings.paddleocr.installing"))
                                .foregroundStyle(.secondary)
                        }
                    } else {
                        HStack(spacing: 12) {
                            Button(localized("settings.paddleocr.install.button")) {
                                viewModel.installPaddleOCR()
                            }
                            .buttonStyle(.bordered)
                            .controlSize(.small)

                            Button(localized("settings.paddleocr.copy.command.button")) {
                                viewModel.copyPaddleOCRInstallCommand()
                            }
                            .buttonStyle(.borderless)
                            .controlSize(.small)
                        }

                        if let error = viewModel.paddleOCRInstallError {
                            Text(error)
                                .font(.caption)
                                .foregroundStyle(.red)
                        }
                    }
                }
            }
        }
        .padding(.top, 8)
        .alert(isPresented: $showErrorAlert) {
            Alert(
                title: Text(localized("engine.config.fetchModels.failed")),
                message: Text(errorMessage),
                dismissButton: .default(Text(localized("button.ok")))
            )
        }
        .onChange(of: viewModel.paddleOCRUseCloud) { _, _ in
            availablePaddleModels = []
        }
    }

    @MainActor
    private func fetchPaddleModels() async {
        isFetchingPaddleModels = true
        errorMessage = ""

        let baseURL = viewModel.paddleOCRCloudBaseURL
        let apiKey = viewModel.paddleOCRCloudAPIKey

        do {
            let models = try await ModelDiscoveryService.fetchModels(
                baseURL: baseURL,
                apiKey: apiKey.isEmpty ? nil : apiKey,
                engineType: nil
            )
            self.availablePaddleModels = models
            self.isFetchingPaddleModels = false
        } catch {
            self.errorMessage = error.localizedDescription
            self.showErrorAlert = true
            self.isFetchingPaddleModels = false
        }
    }
}
