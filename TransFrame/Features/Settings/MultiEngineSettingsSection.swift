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
    // Real health probe results (Phase 5)
    @State private var healthCache: [EngineHealthKey: EngineHealth] = [:]

    // Sheet state for compatible engine configuration
    struct CompatibleSheetState: Identifiable {
        let id = UUID()
        let config: CompatibleTranslationProvider.CompatibleConfig
        let index: Int
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            // Translation Strategy Card (Selection Mode & Primary/Fallback Configuration)
            VStack(alignment: .leading, spacing: 16) {
                selectionModeSection
                modeSpecificSection
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .macosGroupedCard()

            // Available Engines Card
            VStack(alignment: .leading, spacing: 16) {
                enginesSection
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .macosGroupedCard()
        }
        .onAppear {
            checkConfiguredEngines()
            refreshHealth()
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
                set: { newValue in
                    viewModel.settings.engineSelectionMode = newValue
                    // Keep the V2 mode in sync for the converged pipeline
                    viewModel.settings.engineModeV2 =
                        (newValue == .parallel) ? .compare : .ordered
                }
            )) {
                ForEach(EngineSelectionMode.allCases) { mode in
                    Label(mode.localizedName, systemImage: mode.iconName)
                        .tag(mode)
                }
            }
            .pickerStyle(.segmented)
            .labelsHidden()

            // V2 ordered chain editor (Phase 3): shown while legacy modes
            // remain; becomes the only UI at Phase 6 teardown.
            if viewModel.settings.engineModeV2 == .ordered {
                Divider()
                orderedChainSectionV2
            } else {
                Divider()
                compareEnginesSectionV2
            }
        }
    }

    // MARK: - Mode Specific Section

    @ViewBuilder
    private var modeSpecificSection: some View {
        // V2 compare owns engine selection when active; the legacy parallel
        // editor writes settings the new dispatch never reads (PR #93 review)
        if viewModel.settings.engineModeV2 == .compare {
            EmptyView()
        } else {
            innerModeSpecificSection
        }
    }

    @ViewBuilder
    private var innerModeSpecificSection: some View {
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

    // MARK: - V2 Ordered Chain Section (Phase 3)

    /// Engine chain editor for the converged ordered mode: ordered list,
    /// drag/move reordering, replace/remove per row, add from configured
    /// cards or compatible instances. Reuses the quickSwitch row pattern
    /// (composite IDs so row identity doesn't shift mid-delete).
    @ViewBuilder
    private var orderedChainSectionV2: some View {
        let enabledEngines = viewModel.settings.engineConfigs.values.filter { $0.isEnabled }
        let compatibleConfigs = viewModel.settings.compatibleProviderConfigs

        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(localized("engine.config.chain.title"))
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Spacer()
                Text("\(viewModel.settings.engineChainV2.count)/\(EngineSelectionLimits.orderedChainLimit)")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }

            ForEach(
                Array(viewModel.settings.engineChainV2.enumerated()),
                id: \.element.id
            ) { index, item in
                chainRowV2(
                    item: item,
                    index: index,
                    enabledEngines: enabledEngines,
                    compatibleConfigs: compatibleConfigs
                )
            }
            // Reordering is explicit up/down controls: .onMove inside a
            // VStack has no drag source, so it can never fire (PR #92 review).
            // Row identity stays composite so moves/deletes don't shift rows.

            if viewModel.settings.engineChainV2.count < EngineSelectionLimits.orderedChainLimit {
                Menu {
                    ForEach(enabledEngines.filter { $0.id != .custom }, id: \.id) { config in
                        Button(config.id.localizedName) {
                            appendChainEntry(.builtin(config.id))
                        }
                    }
                    if !compatibleConfigs.isEmpty {
                        Divider()
                        ForEach(compatibleConfigs) { config in
                            Button(config.displayName) {
                                appendChainEntry(.compatible(config.id))
                            }
                        }
                    }
                } label: {
                    Label(localized("engine.config.chain.add"), systemImage: "plus.circle")
                        .font(.caption)
                }
                .menuStyle(.borderlessButton)
            }
        }
    }

    private func chainRowV2(
        item: EngineChainItem,
        index: Int,
        enabledEngines: [TranslationEngineConfig],
        compatibleConfigs: [CompatibleTranslationProvider.CompatibleConfig]
    ) -> some View {
        HStack(spacing: 8) {
            Text("\(index + 1)")
                .font(.caption)
                .foregroundStyle(.white)
                .frame(width: 18, height: 18)
                .background(Color.accentColor)
                .cornerRadius(9)

            Text(chainItemDisplayName(item, compatibleConfigs: compatibleConfigs))
                .font(.subheadline)

            Spacer()

            Menu {
                ForEach(enabledEngines.filter { $0.id != .custom }, id: \.id) { config in
                    Button(config.id.localizedName) {
                        replaceChainEntry(at: index, with: .builtin(config.id))
                    }
                }
                if !compatibleConfigs.isEmpty {
                    Divider()
                    ForEach(compatibleConfigs) { config in
                        Button(config.displayName) {
                            replaceChainEntry(at: index, with: .compatible(config.id))
                        }
                    }
                }
            } label: {
                Image(systemName: "arrow.triangle.2.circlepath")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .menuStyle(.borderlessButton)

            Button {
                removeChainEntry(at: index)
            } label: {
                Image(systemName: "minus.circle")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.plain)
            .disabled(viewModel.settings.engineChainV2.count <= 1)

            Button {
                moveChainEntry(at: index, offset: -1)
            } label: {
                Image(systemName: "chevron.up")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.plain)
            .disabled(index == 0)

            Button {
                moveChainEntry(at: index, offset: 1)
            } label: {
                Image(systemName: "chevron.down")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.plain)
            .disabled(index == viewModel.settings.engineChainV2.count - 1)
        }
        .padding(.vertical, 2)
    }

    private func chainItemDisplayName(
        _ item: EngineChainItem,
        compatibleConfigs: [CompatibleTranslationProvider.CompatibleConfig]
    ) -> String {
        switch item.kind {
        case .builtin(let engine):
            return engine.localizedName
        case .compatible(let id):
            let name = compatibleConfigs.first(where: { $0.id == id })?.displayName
            return name ?? localized("engine.config.chain.entryDeleted")
        }
    }

    private func appendChainEntry(_ kind: EngineChainItem.Kind) {
        var chain = viewModel.settings.engineChainV2
        guard chain.count < EngineSelectionLimits.orderedChainLimit else { return }
        chain.append(EngineChainItem(kind: kind))
        viewModel.settings.setEngineChainV2(chain)
    }

    private func replaceChainEntry(at index: Int, with kind: EngineChainItem.Kind) {
        var chain = viewModel.settings.engineChainV2
        guard chain.indices.contains(index) else { return }
        chain[index] = EngineChainItem(kind: kind)
        viewModel.settings.setEngineChainV2(chain)
    }

    private func removeChainEntry(at index: Int) {
        var chain = viewModel.settings.engineChainV2
        guard chain.indices.contains(index), chain.count > 1 else { return }
        chain.remove(at: index)
        viewModel.settings.setEngineChainV2(chain)
    }

    private func moveChainEntry(at index: Int, offset: Int) {
        var chain = viewModel.settings.engineChainV2
        let target = index + offset
        guard chain.indices.contains(index), chain.indices.contains(target) else { return }
        chain.swapAt(index, target)
        viewModel.settings.setEngineChainV2(chain)
    }

    // MARK: - V2 Compare Engines Section (Phase 4)

    /// Multi-select editor for the compare set (2-4 entries): toggle chips
    /// over configured cards + compatible instances. Selection count is
    /// enforced at both ends (cannot drop below 2 while in compare mode,
    /// cannot exceed 4).
    @ViewBuilder
    private var compareEnginesSectionV2: some View {
        let enabledEngines = viewModel.settings.engineConfigs.values.filter { $0.isEnabled }
        let compatibleConfigs = viewModel.settings.compatibleProviderConfigs
        let selected = viewModel.settings.compareEnginesV2

        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(localized("engine.config.compare.title"))
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Spacer()
                Text("\(selected.count)/\(EngineSelectionLimits.compareEnginesLimit)")
                    .font(.caption2)
                    .foregroundStyle(
                        selected.count >= EngineSelectionLimits.compareEnginesMinimum
                            ? Color.secondary : Color.orange
                    )
            }

            FlowLayout(spacing: 6) {
                ForEach(enabledEngines.filter { $0.id != .custom }, id: \.id) { config in
                    compareChip(
                        title: config.id.localizedName,
                        isSelected: selected.contains { $0.kind == .builtin(config.id) },
                        toggle: { toggleCompareEntry(.builtin(config.id)) }
                    )
                }
                ForEach(compatibleConfigs) { config in
                    compareChip(
                        title: config.displayName,
                        isSelected: selected.contains { $0.kind == .compatible(config.id) },
                        toggle: { toggleCompareEntry(.compatible(config.id)) }
                    )
                }
            }

            if selected.count < EngineSelectionLimits.compareEnginesMinimum {
                Text(localized("engine.config.compare.needTwo"))
                    .font(.caption2)
                    .foregroundStyle(.orange)
            }
        }
    }

    private func compareChip(title: String, isSelected: Bool, toggle: @escaping () -> Void) -> some View {
        Button(action: toggle) {
            HStack(spacing: 4) {
                if isSelected {
                    Image(systemName: "checkmark")
                        .font(.caption2)
                }
                Text(title)
                    .font(.caption)
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(isSelected ? Color.accentColor.opacity(0.2) : Color(nsColor: .controlBackgroundColor))
            .overlay(
                RoundedRectangle(cornerRadius: 6)
                    .stroke(isSelected ? Color.accentColor : Color.clear, lineWidth: 1)
            )
            .cornerRadius(6)
        }
        .buttonStyle(.plain)
    }

    private func toggleCompareEntry(_ kind: EngineChainItem.Kind) {
        var compare = viewModel.settings.compareEnginesV2
        let isSelected = compare.contains { $0.kind == kind }
        if isSelected {
            // Cannot drop below the minimum while compare is active
            guard compare.count > EngineSelectionLimits.compareEnginesMinimum else { return }
            compare.removeAll { $0.kind == kind }
        } else {
            guard compare.count < EngineSelectionLimits.compareEnginesLimit else { return }
            compare.append(EngineChainItem(kind: kind))
        }
        viewModel.settings.setCompareEnginesV2(compare)
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
            // 快捷切换允许同一引擎出现多次（主/备同引擎）：组合 ID 既保证唯一，
            // 又让中间行删除后后续行身份不前移（避免删除动画/行内状态错位）
            ForEach(
                Array(viewModel.settings.parallelEngines.enumerated().map { (id: "\($0.offset)-\($0.element.rawValue)", offset: $0.offset, engine: $0.element) }),
                id: \.id
            ) { _, index, engine in
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
                .cornerRadius(8)
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

            // Group engines by category (inventory convergence: 8 cards,
            // ollama configured via the OpenAI-compatible preset proxy)
            ForEach(EngineCategory.allCases, id: \.self) { category in
                if category == .compatible {
                    // Special handling for compatible engines - dynamic cards
                    compatibleEnginesSection
                } else {
                    let enginesInCategory = TranslationEngineType.uiCards.filter { $0.category == category }
                    if !enginesInCategory.isEmpty {
                        VStack(alignment: .leading, spacing: 8) {
                            Text(category.localizedName)
                                .font(.subheadline)
                                .foregroundStyle(.secondary)

                            LazyVGrid(columns: [
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
        .sheet(item: $editingConfig, onDismiss: {
            checkConfiguredEngines()
            refreshHealth()
        }) { config in
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
                ),
                ollamaProxyConfig: viewModel.settings.savedOllamaConfig(),
                onOllamaProxySave: { ollamaConfig in
                    // Ollama preset save from the OpenAI-compatible card
                    var configs = viewModel.settings.engineConfigs
                    configs[.ollama] = ollamaConfig
                    viewModel.settings.engineConfigs = configs
                    viewModel.settings.saveEngineConfigs()
                }
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
            .cornerRadius(8)
            .overlay(
                RoundedRectangle(cornerRadius: 8)
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
            .cornerRadius(8)
            .overlay(
                RoundedRectangle(cornerRadius: 8)
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
                    // Purge the deleted instance from V2 selections, otherwise
                    // invisible entries keep consuming the 2-4 quota and fail
                    // every run (PR #93 review).
                    let deletedID = config.id
                    let chain = viewModel.settings.engineChainV2.filter { $0.kind != .compatible(deletedID) }
                    if chain.count < viewModel.settings.engineChainV2.count {
                        viewModel.settings.setEngineChainV2(chain.isEmpty ? [.builtin(.apple)] : chain)
                    }
                    let compare = viewModel.settings.compareEnginesV2.filter { $0.kind != .compatible(deletedID) }
                    if compare.count < viewModel.settings.compareEnginesV2.count {
                        // Keep the 2-entry minimum by padding with a builtin
                        var padded = compare
                        while padded.count < EngineSelectionLimits.compareEnginesMinimum {
                            padded.append(.builtin(.apple))
                        }
                        viewModel.settings.setCompareEnginesV2(padded)
                    }
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
        // Real availability probe (Phase 5): four-state health replaces the
        // key-presence dot. OpenAI-compatible card aggregates openai+ollama.
        let health: EngineHealth = {
            let primary = healthCache[.builtin(engine)] ?? .checking
            if engine == .openai {
                let ollamaHealth = healthCache[.builtin(.ollama)]
                // best of the two: available > unverified/misconfigured > unconfigured
                if case .unconfigured = primary, ollamaHealth != nil, ollamaHealth != .unconfigured {
                    return ollamaHealth!
                }
                return primary
            }
            return primary
        }()

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

                    // Four-state health dot (Phase 5): green=available,
                    // red=misconfigured(reason tooltip), orange=unconfigured,
                    // gray=unverified, secondary=checking
                    HStack(spacing: 4) {
                        Circle()
                            .fill(healthDotColor(health))
                            .frame(width: 6, height: 6)
                        Text(healthLabel(health))
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                            .help(healthTooltip(health))
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
            .cornerRadius(8)
            .overlay(
                RoundedRectangle(cornerRadius: 8)
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

    // MARK: - Health helpers (Phase 5)

    private func healthDotColor(_ health: EngineHealth) -> Color {
        switch health {
        case .available: return .green
        case .misconfigured: return .red
        case .unconfigured: return .orange
        case .unverified: return .gray
        case .checking: return .secondary
        }
    }

    private func healthLabel(_ health: EngineHealth) -> String {
        switch health {
        case .available: return localized("engine.health.available")
        case .misconfigured: return localized("engine.health.misconfigured")
        case .unconfigured: return localized("engine.status.unconfigured")
        case .unverified: return localized("engine.health.unverified")
        case .checking: return localized("engine.health.checking")
        }
    }

    private func healthTooltip(_ health: EngineHealth) -> String {
        switch health {
        case .misconfigured(let reason): return reason
        default: return healthLabel(health)
        }
    }

    /// Probe all rendered cards + compatible instances; async, non-blocking.
    private func refreshHealth() {
        Task {
            await EngineHealthChecker.shared.invalidateAll()
            var keys: [EngineHealthKey] = TranslationEngineType.uiCards
                .filter { $0 != .openai }
                .map { .builtin($0) }
            // OpenAI-compatible card aggregates openai + ollama
            keys.append(contentsOf: [.builtin(.openai), .builtin(.ollama)])
            for config in viewModel.settings.compatibleProviderConfigs {
                keys.append(.compatible(config.id))
            }
            var updated: [EngineHealthKey: EngineHealth] = [:]
            for key in keys {
                updated[key] = await EngineHealthChecker.shared.refresh(key)
            }
            await MainActor.run {
                self.healthCache.merge(updated) { _, new in new }
            }
        }
    }

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
                if engine == .ollama {
                    // Ollama is only "configured" when the user actually saved a
                    // preset config — loadEngineConfigs() prepopulates defaults
                    // for every engine, so a dictionary hit means nothing.
                    if let saved = viewModel.settings.savedOllamaConfig(),
                       saved.isEnabled || saved.options?.baseURL != TranslationEngineType.ollama.defaultBaseURL {
                        configured.insert(engine)
                    }
                } else if !engine.requiresAPIKey {
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
