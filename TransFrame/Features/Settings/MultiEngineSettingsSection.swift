//
//  MultiEngineSettingsSection.swift
//  TransFrame
//
//  Multi-engine configuration section for settings
//

import SwiftUI
import os.log

struct MultiEngineSettingsSection: View {
    @Bindable var viewModel: SettingsViewModel
    @State private var selectedEngine: TranslationEngineType?
    @State private var showingConfigSheet = false
    @State private var editingConfig: TranslationEngineConfig?
    @State private var compatibleSheetState: CompatibleSheetState?
    
    @State private var configuredEngines: Set<TranslationEngineType> = []

    // Sheet state for compatible engine configuration
    struct CompatibleSheetState: Identifiable {
        let id = UUID()
        let config: CompatibleTranslationProvider.CompatibleConfig
        let index: Int
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            // Selection Mode (horizontal layout)
            selectionModeSection

            // Mode-specific configuration
            modeSpecificSection

            Divider()

            // Available Engines
            enginesSection
        }
        .padding()
        .background(Color(.controlBackgroundColor))
        .cornerRadius(8)
        .onAppear {
            checkConfiguredEngines()
        }
    }

    // MARK: - Selection Mode Section (Horizontal)

    @ViewBuilder
    private var selectionModeSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(localized("engine.config.title"))
                .font(.headline)

            // Segmented mode picker
            Picker(localized("engine.config.title"), selection: Binding(
                get: { viewModel.settings.engineSelectionMode },
                set: { viewModel.settings.engineSelectionMode = $0 }
            )) {
                ForEach(EngineSelectionMode.allCases) { mode in
                    Label(mode.localizedName, systemImage: mode.iconName)
                        .tag(mode)
                }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
        }
    }

    // MARK: - Mode Specific Section

    @ViewBuilder
    private var modeSpecificSection: some View {
        switch viewModel.settings.engineSelectionMode {
        case .primaryWithFallback:
            primaryFallbackSection
        case .parallel:
            parallelEnginesSection
        case .quickSwitch:
            quickSwitchSection
        case .sceneBinding:
            sceneBindingSection
        }
    }

    // MARK: - Primary/Fallback Section

    @ViewBuilder
    private var primaryFallbackSection: some View {
        let enabledEngines = viewModel.settings.engineConfigs.values.filter { $0.isEnabled }
        let compatibleConfigs = viewModel.settings.compatibleProviderConfigs

        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 24) {
                // Primary Engine
                VStack(alignment: .leading, spacing: 4) {
                    Text(localized("engine.config.primary"))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Picker(localized("engine.config.primary"), selection: Binding(
                        get: {
                            if let first = viewModel.settings.parallelEngines.first {
                                // Check if it's a compatible engine
                                if first == .custom, let customConfig = viewModel.settings.engineConfigs[.custom],
                                   let customNameStr = customConfig.customName,
                                   let jsonData = customNameStr.data(using: .utf8),
                                   let compatibleConfig = try? JSONDecoder().decode(CompatibleTranslationProvider.CompatibleConfig.self, from: jsonData) {
                                    return "compatible:\(compatibleConfig.id)"
                                }
                                return first.rawValue
                            }
                            return TranslationEngineType.apple.rawValue
                        },
                        set: { (newValue: String) in
                            let engine: TranslationEngineType
                            if newValue.hasPrefix("compatible:") {
                                // Set compatible config as custom engine
                                let configId = String(newValue.dropFirst("compatible:".count))
                                if let config = compatibleConfigs.first(where: { $0.id.uuidString == configId }) {
                                    setCompatibleAsEngine(config: config)
                                }
                                return
                            } else {
                                engine = TranslationEngineType(rawValue: newValue) ?? .apple
                            }
                            if viewModel.settings.parallelEngines.isEmpty {
                                viewModel.settings.parallelEngines = [engine]
                            } else {
                                viewModel.settings.parallelEngines[0] = engine
                            }
                            viewModel.settings.saveParallelEngines()
                        }
                    )) {
                        // Standard engines
                        ForEach(enabledEngines.filter { $0.id != .custom }, id: \.id) { config in
                            Text(config.id.localizedName).tag(config.id.rawValue)
                        }
                        // Compatible engines
                        ForEach(compatibleConfigs) { config in
                            Text(config.displayName).tag("compatible:\(config.id)")
                        }
                    }
                    .pickerStyle(.menu)
                    .labelsHidden()
                    .frame(width: 140)
                }

                // Fallback Engine
                VStack(alignment: .leading, spacing: 4) {
                    Text(localized("engine.config.fallback"))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Picker(localized("engine.config.fallback"), selection: Binding(
                        get: {
                            if viewModel.settings.parallelEngines.count > 1 {
                                let second = viewModel.settings.parallelEngines[1]
                                if second == .custom, let customConfig = viewModel.settings.engineConfigs[.custom],
                                   let customNameStr = customConfig.customName,
                                   let jsonData = customNameStr.data(using: .utf8),
                                   let compatibleConfig = try? JSONDecoder().decode(CompatibleTranslationProvider.CompatibleConfig.self, from: jsonData) {
                                    return "compatible:\(compatibleConfig.id)"
                                }
                                return second.rawValue
                            }
                            return (enabledEngines.first?.id ?? .apple).rawValue
                        },
                        set: { (newValue: String) in
                            let engine: TranslationEngineType
                            if newValue.hasPrefix("compatible:") {
                                let configId = String(newValue.dropFirst("compatible:".count))
                                if let config = compatibleConfigs.first(where: { $0.id.uuidString == configId }) {
                                    setCompatibleAsEngine(config: config)
                                }
                                return
                            } else {
                                engine = TranslationEngineType(rawValue: newValue) ?? .apple
                            }
                            if viewModel.settings.parallelEngines.count > 1 {
                                viewModel.settings.parallelEngines[1] = engine
                            } else {
                                viewModel.settings.parallelEngines.append(engine)
                            }
                            viewModel.settings.saveParallelEngines()
                        }
                    )) {
                        // Standard engines
                        ForEach(enabledEngines.filter { $0.id != .custom }, id: \.id) { config in
                            Text(config.id.localizedName).tag(config.id.rawValue)
                        }
                        // Compatible engines
                        ForEach(compatibleConfigs) { config in
                            Text(config.displayName).tag("compatible:\(config.id)")
                        }
                    }
                    .pickerStyle(.menu)
                    .labelsHidden()
                    .frame(width: 140)
                }
            }
        }
    }

    // MARK: - Quick Switch Section

    @ViewBuilder
    private var quickSwitchSection: some View {
        let enabledEngines = viewModel.settings.engineConfigs.values.filter { $0.isEnabled }
        let compatibleConfigs = viewModel.settings.compatibleProviderConfigs

        VStack(alignment: .leading, spacing: 8) {
            Text(localized("engine.config.switch.order"))
                .font(.caption)
                .foregroundStyle(.secondary)

            // Engine order list
            ForEach(Array(viewModel.settings.parallelEngines.enumerated()), id: \.element) { index, engine in
                HStack(spacing: 8) {
                    // Order number
                    Text("\(index + 1)")
                        .font(.caption)
                        .foregroundStyle(.white)
                        .frame(width: 18, height: 18)
                        .background(Color.accentColor)
                        .cornerRadius(9)

                    // Engine name
                    Text(engine.localizedName)
                        .font(.subheadline)

                    Spacer()

                    // Replace button
                    Menu {
                        ForEach(enabledEngines.filter { $0.id != .custom }, id: \.id) { config in
                            Button(config.id.localizedName) {
                                viewModel.settings.parallelEngines[index] = config.id
                                viewModel.settings.saveParallelEngines()
                            }
                        }
                        if !compatibleConfigs.isEmpty {
                            Divider()
                            ForEach(compatibleConfigs) { config in
                                Button(config.displayName) {
                                    setCompatibleAsEngineAtIndex(config: config, index: index)
                                }
                            }
                        }
                    } label: {
                        Image(systemName: "arrow.triangle.2.circlepath")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    .menuStyle(.borderlessButton)
                    .help(localized("engine.config.replace"))

                    // Remove button (if more than 1)
                    if viewModel.settings.parallelEngines.count > 1 {
                        Button {
                            viewModel.settings.parallelEngines.remove(at: index)
                            viewModel.settings.saveParallelEngines()
                        } label: {
                            Image(systemName: "xmark.circle.fill")
                                .font(.caption)
                                .foregroundStyle(.red)
                        }
                        .buttonStyle(.plain)
                        .help(localized("engine.config.remove"))
                    }
                }
                .padding(.horizontal, 8)
                .padding(.vertical, 6)
                .background(Color(.controlBackgroundColor))
                .cornerRadius(6)
            }

            // Add engine button if less than enabled engines
            if viewModel.settings.parallelEngines.count < enabledEngines.count + compatibleConfigs.count {
                Menu {
                    ForEach(enabledEngines.filter { $0.id != .custom }, id: \.id) { config in
                        if !viewModel.settings.parallelEngines.contains(config.id) {
                            Button {
                                viewModel.settings.parallelEngines.append(config.id)
                                viewModel.settings.saveParallelEngines()
                            } label: {
                                HStack {
                                    Image(systemName: engineIcon(config.id))
                                    Text(config.id.localizedName)
                                }
                            }
                        }
                    }
                    if !compatibleConfigs.isEmpty {
                        Divider()
                        ForEach(compatibleConfigs) { config in
                            Button {
                                appendCompatibleAsEngine(config: config)
                            } label: {
                                HStack {
                                    Image(systemName: "gearshape.2")
                                    Text(config.displayName)
                                }
                            }
                        }
                    }
                } label: {
                    HStack(spacing: 4) {
                        Image(systemName: "plus.circle")
                        Text(localized("engine.config.add"))
                    }
                    .font(.caption)
                    .foregroundStyle(Color.accentColor)
                }
                .menuStyle(.borderlessButton)
            }
        }
    }

    // MARK: - Engines Section

    @ViewBuilder
    private var enginesSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text(localized("engine.available.title"))
                    .font(.headline)
                Spacer()
            }

            // Group engines by category
            ForEach(EngineCategory.allCases, id: \.self) { category in
                if category == .compatible {
                    // Special handling for compatible engines - dynamic cards
                    compatibleEnginesSection
                } else {
                    let enginesInCategory = TranslationEngineType.allCases.filter { $0.category == category }
                    if !enginesInCategory.isEmpty {
                        VStack(alignment: .leading, spacing: 8) {
                            Text(category.localizedName)
                                .font(.subheadline)
                                .foregroundStyle(.secondary)

                            LazyVGrid(columns: [
                                GridItem(.flexible()),
                                GridItem(.flexible()),
                                GridItem(.flexible())
                            ], spacing: 8) {
                                ForEach(enginesInCategory, id: \.self) { engine in
                                    engineCard(engine)
                                }
                            }
                        }
                    }
                }
            }
        }
        .sheet(item: $editingConfig, onDismiss: { checkConfiguredEngines() }) { config in
            let engine = config.id
            EngineConfigSheet(
                engine: engine,
                config: Binding(
                    get: { viewModel.settings.engineConfigs[engine] ?? .default(for: engine) },
                    set: { newValue in
                        // Use full property assignment instead of subscript
                        // to guarantee @Observable persistence
                        Logger.settings.info("Engine config updated: \(engine.rawValue), isEnabled=\(newValue.isEnabled)")
                        var configs = viewModel.settings.engineConfigs
                        configs[engine] = newValue
                        viewModel.settings.engineConfigs = configs
                        viewModel.settings.saveEngineConfigs()
                    }
                )
            )
        }
        .sheet(item: $compatibleSheetState) { state in
            CompatibleEngineConfigSheet(
                config: state.config,
                index: state.index,
                isNew: state.index >= viewModel.settings.compatibleProviderConfigs.count,
                onSave: { savedConfig in
                    if state.index >= viewModel.settings.compatibleProviderConfigs.count {
                        viewModel.settings.compatibleProviderConfigs.append(savedConfig)
                    } else {
                        viewModel.settings.compatibleProviderConfigs[state.index] = savedConfig
                    }
                    viewModel.settings.saveCompatibleConfigs()

                    var customConfig = viewModel.settings.engineConfigs[.custom] ?? .default(for: .custom)
                    let isCurrentCustom: Bool = {
                        guard let customNameStr = customConfig.customName,
                              let jsonData = customNameStr.data(using: .utf8),
                              let current = try? JSONDecoder().decode(CompatibleTranslationProvider.CompatibleConfig.self, from: jsonData) else {
                            return true
                        }
                        return current.id == savedConfig.id
                    }()

                    if isCurrentCustom || viewModel.settings.compatibleProviderConfigs.count == 1 {
                        if let jsonData = try? JSONEncoder().encode(savedConfig),
                           let jsonString = String(data: jsonData, encoding: .utf8) {
                            customConfig.customName = jsonString
                            customConfig.isEnabled = true
                            viewModel.settings.engineConfigs[.custom] = customConfig
                            viewModel.settings.saveEngineConfigs()
                        }
                    }

                    Task {
                        await TranslationEngineRegistry.shared.unregister(.custom)
                        await TranslationEngineRegistry.shared.removeCompatibleProvider(for: savedConfig.keychainId)
                    }
                }
            )
        }
    }

    // MARK: - Compatible Engines Section

    @ViewBuilder
    private var compatibleEnginesSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(EngineCategory.compatible.localizedName)
                .font(.subheadline)
                .foregroundStyle(.secondary)

            // Dynamic compatible engine cards
            ForEach(Array(viewModel.settings.compatibleProviderConfigs.enumerated()), id: \.element.id) { index, config in
                compatibleEngineCard(config: config, index: index)
            }

            // Add button (max 5 engines)
            if viewModel.settings.compatibleProviderConfigs.count < 5 {
                addCompatibleEngineButton
            } else {
                Text(localized("engine.compatible.max.reached"))
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
    }

    @ViewBuilder
    private func compatibleEngineCard(config: CompatibleTranslationProvider.CompatibleConfig, index: Int) -> some View {
        let isSelected = isCompatibleEngineSelected(config)
        Button {
            compatibleSheetState = CompatibleSheetState(config: config, index: index)
        } label: {
            HStack(spacing: 8) {
                Image(systemName: "gearshape.2")
                    .font(.body)
                    .foregroundStyle(isSelected ? Color.accentColor : Color.secondary)

                VStack(alignment: .leading, spacing: 2) {
                    Text(config.displayName)
                        .font(.subheadline)
                        .lineLimit(1)

                    HStack(spacing: 4) {
                        Circle()
                            .fill(isSelected ? Color.green : Color.gray)
                            .frame(width: 6, height: 6)
                        Text(localized("engine.status.configured"))
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                    }
                }

                Spacer()

                // Use as engine button
                Button {
                    setCompatibleAsEngine(config: config)
                } label: {
                    Image(systemName: isSelected ? "checkmark.circle.fill" : "circle")
                        .foregroundStyle(isSelected ? Color.green : Color.accentColor)
                }
                .buttonStyle(.plain)
                .help(localized("engine.compatible.useAsEngine"))

                // Delete button
                Button {
                    deleteCompatibleEngine(at: index)
                } label: {
                    Image(systemName: "minus.circle.fill")
                        .foregroundStyle(.red)
                }
                .buttonStyle(.plain)
                .help(localized("engine.compatible.delete"))
            }
            .padding(8)
            .background(isSelected ? Color.accentColor.opacity(0.1) : Color.clear)
            .cornerRadius(6)
            .overlay(
                RoundedRectangle(cornerRadius: 6)
                    .stroke(isSelected ? Color.accentColor : Color.gray.opacity(0.3), lineWidth: 1)
            )
        }
        .buttonStyle(.plain)
    }

    @ViewBuilder
    private var addCompatibleEngineButton: some View {
        Button {
            compatibleSheetState = CompatibleSheetState(
                config: CompatibleTranslationProvider.CompatibleConfig.default,
                index: viewModel.settings.compatibleProviderConfigs.count
            )
        } label: {
            HStack(spacing: 6) {
                Image(systemName: "plus.circle")
                    .font(.body)
                Text(localized("engine.compatible.add"))
                    .font(.subheadline)
            }
            .foregroundStyle(Color.accentColor)
            .padding(8)
            .frame(maxWidth: .infinity)
            .background(Color(.controlBackgroundColor))
            .cornerRadius(6)
            .overlay(
                RoundedRectangle(cornerRadius: 6)
                    .stroke(style: SwiftUI.StrokeStyle(lineWidth: 1, dash: [4]))
                    .foregroundStyle(Color.gray.opacity(0.3))
            )
        }
        .buttonStyle(.plain)
    }

    private func deleteCompatibleEngine(at index: Int) {
        Task {
            do {
                let config = viewModel.settings.compatibleProviderConfigs[index]
                try await KeychainService.shared.deleteCredentials(forCompatibleId: config.keychainId)

                // Clear cached provider for this engine
                await TranslationEngineRegistry.shared.removeCompatibleProvider(for: config.keychainId)

                await MainActor.run {
                    viewModel.settings.compatibleProviderConfigs.remove(at: index)
                    viewModel.settings.saveCompatibleConfigs()
                }
            } catch {
                // Log error but don't remove config if credential migration fails
                Logger.settings.error("Failed to delete compatible engine: \(error.localizedDescription)")
                // Optionally show alert to user
            }
        }
    }

    private func setCompatibleAsPrimaryEngine(at index: Int) {
        guard index < viewModel.settings.compatibleProviderConfigs.count else { return }
        let config = viewModel.settings.compatibleProviderConfigs[index]
        setCompatibleAsEngine(config: config)
    }

    private func setCompatibleAsEngineAtIndex(config: CompatibleTranslationProvider.CompatibleConfig, index: Int) {
        Task {
            do {
                let provider = try await TranslationEngineRegistry.shared.createCompatibleProvider(
                    compatibleConfig: config
                )

                let isAvailable = await provider.isAvailable
                guard isAvailable else {
                    Logger.settings.warning("Compatible engine \(config.displayName) is not available")
                    return
                }

                await MainActor.run {
                    var customConfig = viewModel.settings.engineConfigs[.custom] ?? .default(for: .custom)
                    if let jsonData = try? JSONEncoder().encode(config),
                       let jsonString = String(data: jsonData, encoding: .utf8) {
                        customConfig.customName = jsonString
                        customConfig.isEnabled = true
                        viewModel.settings.engineConfigs[.custom] = customConfig
                        viewModel.settings.saveEngineConfigs()
                        viewModel.settings.parallelEngines[index] = .custom
                        viewModel.settings.saveParallelEngines()
                    }
                }
            } catch {
                Logger.settings.error("Failed to set compatible engine: \(error.localizedDescription)")
            }
        }
    }

    private func appendCompatibleAsEngine(config: CompatibleTranslationProvider.CompatibleConfig) {
        Task {
            do {
                let provider = try await TranslationEngineRegistry.shared.createCompatibleProvider(
                    compatibleConfig: config
                )

                let isAvailable = await provider.isAvailable
                guard isAvailable else {
                    Logger.settings.warning("Compatible engine \(config.displayName) is not available")
                    return
                }

                await MainActor.run {
                    var customConfig = viewModel.settings.engineConfigs[.custom] ?? .default(for: .custom)
                    if let jsonData = try? JSONEncoder().encode(config),
                       let jsonString = String(data: jsonData, encoding: .utf8) {
                        customConfig.customName = jsonString
                        customConfig.isEnabled = true
                        viewModel.settings.engineConfigs[.custom] = customConfig
                        viewModel.settings.saveEngineConfigs()
                        viewModel.settings.parallelEngines.append(.custom)
                        viewModel.settings.saveParallelEngines()
                    }
                }
            } catch {
                Logger.settings.error("Failed to append compatible engine: \(error.localizedDescription)")
            }
        }
    }

    private func setCompatibleAsEngine(config: CompatibleTranslationProvider.CompatibleConfig) {
        Task {
            do {
                // Create provider to verify configuration
                let provider = try await TranslationEngineRegistry.shared.createCompatibleProvider(
                    compatibleConfig: config
                )

                // Verify availability
                let isAvailable = await provider.isAvailable
                guard isAvailable else {
                    Logger.settings.warning("Compatible engine \(config.displayName) is not available")
                    return
                }

                // Update custom engine config with this compatible config
                await MainActor.run {
                    var customConfig = viewModel.settings.engineConfigs[.custom] ?? .default(for: .custom)
                    if let jsonData = try? JSONEncoder().encode(config),
                       let jsonString = String(data: jsonData, encoding: .utf8) {
                        customConfig.customName = jsonString
                        customConfig.isEnabled = true
                        viewModel.settings.engineConfigs[.custom] = customConfig
                        viewModel.settings.saveEngineConfigs()

                        // Set as primary engine
                        if viewModel.settings.parallelEngines.isEmpty {
                            viewModel.settings.parallelEngines = [.custom]
                        } else {
                            viewModel.settings.parallelEngines[0] = .custom
                        }
                        viewModel.settings.saveParallelEngines()
                    }
                }
            } catch {
                Logger.settings.error("Failed to set compatible engine: \(error.localizedDescription)")
            }
        }
    }

    @ViewBuilder
    private func engineCard(_ engine: TranslationEngineType) -> some View {
        let config = viewModel.settings.engineConfigs[engine] ?? .default(for: engine)
        let _ = Logger.settings.info("engineCard \(engine.rawValue): isEnabled=\(config.isEnabled), fromDefault=\(viewModel.settings.engineConfigs[engine] == nil)")
        // Built-in engines (apple, mtranServer) and Ollama don't need API keys
        // For others, we check if they require API key (simplified check - in real use would check keychain)
        let isConfigured = configuredEngines.contains(engine)

        Button {
            editingConfig = config
        } label: {
            HStack(spacing: 8) {
                Image(systemName: engineIcon(engine))
                    .font(.body)
                    .foregroundStyle(config.isEnabled ? Color.accentColor : Color.secondary)

                VStack(alignment: .leading, spacing: 2) {
                    Text(engine.localizedName)
                        .font(.subheadline)
                        .lineLimit(1)

                    // Show status for all engines
                    HStack(spacing: 4) {
                        Circle()
                            .fill(isConfigured ? (config.isEnabled ? Color.green : Color.gray) : Color.orange)
                            .frame(width: 6, height: 6)
                        Text(isConfigured ? localized("engine.status.configured") : localized("engine.status.unconfigured"))
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                    }
                }

                Spacer()

                if config.isEnabled {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundStyle(.green)
                }
            }
            .padding(8)
            .background(config.isEnabled ? Color.accentColor.opacity(0.1) : Color.clear)
            .cornerRadius(6)
            .overlay(
                RoundedRectangle(cornerRadius: 6)
                    .stroke(config.isEnabled ? Color.accentColor : Color.gray.opacity(0.3), lineWidth: 1)
            )
        }
        .buttonStyle(.plain)
    }

    // MARK: - Parallel Engines Section (Select which engines to run)

    @ViewBuilder
    private var parallelEnginesSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(localized("engine.config.parallel.select"))
                .font(.caption)
                .foregroundStyle(.secondary)

            let enabledEngines = viewModel.settings.engineConfigs.values.filter { $0.isEnabled }
            let compatibleConfigs = viewModel.settings.compatibleProviderConfigs

            FlowLayout(spacing: 8) {
                ForEach(enabledEngines.filter { $0.id != .custom }, id: \.id) { config in
                    HStack(spacing: 4) {
                        Image(systemName: engineIcon(config.id))
                            .font(.caption)
                        Text(config.id.localizedName)
                            .font(.caption)
                    }
                    .padding(.horizontal, 8)
                    .padding(.vertical, 4)
                    .background(viewModel.settings.parallelEngines.contains(config.id) ? Color.accentColor.opacity(0.2) : Color.clear)
                    .cornerRadius(4)
                    .overlay(
                        RoundedRectangle(cornerRadius: 4)
                            .stroke(viewModel.settings.parallelEngines.contains(config.id) ? Color.accentColor : Color.gray.opacity(0.3), lineWidth: 1)
                    )
                    .onTapGesture {
                        if viewModel.settings.parallelEngines.contains(config.id) {
                            viewModel.settings.parallelEngines.removeAll { $0 == config.id }
                        } else {
                            viewModel.settings.parallelEngines.append(config.id)
                        }
                        viewModel.settings.saveParallelEngines()
                    }
                }
                
                // Compatible engines
                ForEach(compatibleConfigs) { config in
                    HStack(spacing: 4) {
                        Image(systemName: "gearshape.2")
                            .font(.caption)
                        Text(config.displayName)
                            .font(.caption)
                    }
                    .padding(.horizontal, 8)
                    .padding(.vertical, 4)
                    .background(isCompatibleEngineSelected(config) ? Color.accentColor.opacity(0.2) : Color.clear)
                    .cornerRadius(4)
                    .overlay(
                        RoundedRectangle(cornerRadius: 4)
                            .stroke(isCompatibleEngineSelected(config) ? Color.accentColor : Color.gray.opacity(0.3), lineWidth: 1)
                    )
                    .onTapGesture {
                        toggleCompatibleEngineInParallel(config: config)
                    }
                }
            }
        }
    }

    private func isCompatibleEngineSelected(_ config: CompatibleTranslationProvider.CompatibleConfig) -> Bool {
        guard viewModel.settings.parallelEngines.contains(.custom),
              let customConfig = viewModel.settings.engineConfigs[.custom],
              let customNameStr = customConfig.customName,
              let jsonData = customNameStr.data(using: .utf8),
              let selectedConfig = try? JSONDecoder().decode(CompatibleTranslationProvider.CompatibleConfig.self, from: jsonData) else {
            return false
        }
        return selectedConfig.id == config.id
    }

    private func toggleCompatibleEngineInParallel(config: CompatibleTranslationProvider.CompatibleConfig) {
        if isCompatibleEngineSelected(config) {
            viewModel.settings.parallelEngines.removeAll { $0 == .custom }
            viewModel.settings.saveParallelEngines()
        } else {
            // Set this compatible config as custom and add to parallel engines
            var customConfig = viewModel.settings.engineConfigs[.custom] ?? .default(for: .custom)
            if let jsonData = try? JSONEncoder().encode(config),
               let jsonString = String(data: jsonData, encoding: .utf8) {
                customConfig.customName = jsonString
                customConfig.isEnabled = true
                viewModel.settings.engineConfigs[.custom] = customConfig
                viewModel.settings.saveEngineConfigs()
                if !viewModel.settings.parallelEngines.contains(.custom) {
                    viewModel.settings.parallelEngines.append(.custom)
                    viewModel.settings.saveParallelEngines()
                }
            }
        }
    }

    // MARK: - Scene Binding Section

    @ViewBuilder
    private var sceneBindingSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            let enabledEngines = viewModel.settings.engineConfigs.values.filter { $0.isEnabled }
            let compatibleConfigs = viewModel.settings.compatibleProviderConfigs

            ForEach(TranslationScene.allCases) { scene in
                HStack {
                    Image(systemName: scene.iconName)
                        .frame(width: 20)
                    Text(scene.localizedName)
                        .frame(width: 100, alignment: .leading)

                    Spacer()

                    // Primary engine picker
                    Picker(scene.localizedName, selection: Binding(
                        get: {
                            let engine = viewModel.settings.sceneBindings[scene]?.primaryEngine ?? .apple
                            if engine == .custom, let customConfig = viewModel.settings.engineConfigs[.custom],
                               let customNameStr = customConfig.customName,
                               let jsonData = customNameStr.data(using: .utf8),
                               let compatibleConfig = try? JSONDecoder().decode(CompatibleTranslationProvider.CompatibleConfig.self, from: jsonData) {
                                return "compatible:\(compatibleConfig.id.uuidString)"
                            }
                            return engine.rawValue
                        },
                        set: { (newValue: String) in
                            var binding = viewModel.settings.sceneBindings[scene] ?? .default(for: scene)
                            if newValue.hasPrefix("compatible:") {
                                let configId = String(newValue.dropFirst("compatible:".count))
                                if let config = compatibleConfigs.first(where: { $0.id.uuidString == configId }) {
                                    // Set compatible config as custom engine
                                    var customConfig = viewModel.settings.engineConfigs[.custom] ?? .default(for: .custom)
                                    if let jsonData = try? JSONEncoder().encode(config),
                                       let jsonString = String(data: jsonData, encoding: .utf8) {
                                        customConfig.customName = jsonString
                                        customConfig.isEnabled = true
                                        viewModel.settings.engineConfigs[.custom] = customConfig
                                        viewModel.settings.saveEngineConfigs()
                                        binding.primaryEngine = .custom
                                    }
                                }
                            } else {
                                binding.primaryEngine = TranslationEngineType(rawValue: newValue) ?? .apple
                            }
                            viewModel.settings.sceneBindings[scene] = binding
                            viewModel.settings.saveSceneBindings()
                        }
                    )) {
                        ForEach(enabledEngines.filter { $0.id != .custom }, id: \.id) { config in
                            Text(config.id.localizedName).tag(config.id.rawValue)
                        }
                        ForEach(compatibleConfigs) { config in
                            Text(config.displayName).tag("compatible:\(config.id.uuidString)")
                        }
                    }
                    .pickerStyle(.menu)
                    .labelsHidden()
                    .frame(width: 130)
                }
            }
        }
    }

    // MARK: - Helpers

    private func engineIcon(_ engine: TranslationEngineType) -> String {
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

    private func checkConfiguredEngines() {
        Task {
            var configured = Set<TranslationEngineType>()
            for engine in TranslationEngineType.allCases {
                if !engine.requiresAPIKey {
                    configured.insert(engine)
                } else {
                    let hasCreds = await KeychainService.shared.hasCredentials(for: engine)
                    if hasCreds {
                        configured.insert(engine)
                    }
                }
            }
            let finalConfigured = configured
            await MainActor.run {
                self.configuredEngines = finalConfigured
            }
        }
    }
}

// MARK: - Flow Layout

struct FlowLayout: Layout {
    var spacing: CGFloat = 8

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let rows = computeRows(proposal: proposal, subviews: subviews)
        let height = rows.reduce(0) { $0 + $1.height + spacing } - spacing
        return CGSize(width: proposal.width ?? 0, height: height > 0 ? height : 0)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        let rows = computeRows(proposal: proposal, subviews: subviews)
        var y = bounds.minY
        for row in rows {
            var x = bounds.minX
            for item in row.items {
                subviews[item.index].place(at: CGPoint(x: x, y: y), proposal: .unspecified)
                x += item.size.width + spacing
            }
            y += row.height + spacing
        }
    }

    private func computeRows(proposal: ProposedViewSize, subviews: Subviews) -> [Row] {
        var rows: [Row] = []
        var currentRow = Row()
        var currentX: CGFloat = 0
        let maxWidth = proposal.width ?? .infinity

        for (index, subview) in subviews.enumerated() {
            let size = subview.sizeThatFits(.unspecified)

            if currentX + size.width > maxWidth, !currentRow.items.isEmpty {
                rows.append(currentRow)
                currentRow = Row()
                currentX = 0
            }

            currentRow.items.append(RowItem(index: index, size: size))
            currentRow.height = max(currentRow.height, size.height)
            currentX += size.width + spacing
        }

        if !currentRow.items.isEmpty {
            rows.append(currentRow)
        }

        return rows
    }

    struct Row {
        var items: [RowItem] = []
        var height: CGFloat = 0
    }

    struct RowItem {
        let index: Int
        let size: CGSize
    }
}
