import AppKit
import SwiftUI
import PermissionFlow

struct GeneralSettingsContent: View {
    @Bindable var viewModel: SettingsViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            Label(localized("settings.section.permissions"), systemImage: "lock.shield")
                .font(.headline)
            PermissionRow(viewModel: viewModel)
        }
        .macos26LiquidGlass()

        VStack(alignment: .leading, spacing: 20) {
            Label(localized("settings.save.location"), systemImage: "folder")
                .font(.headline)
            SaveLocationPicker(viewModel: viewModel)
            Divider().opacity(0.1)
            AppLanguagePicker()
        }
        .macos26LiquidGlass()
    }
}

// MARK: - Permission Row

struct PermissionRow: View {
    @Bindable var viewModel: SettingsViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            PermissionItem(
                icon: "record.circle",
                title: localized("settings.permission.screen.recording"),
                hint: localized("settings.permission.screen.recording.hint"),
                isGranted: viewModel.hasScreenRecordingPermission,
                isChecking: viewModel.isCheckingPermissions,
                pane: .screenRecording,
                onGrant: { viewModel.requestScreenRecordingPermission() }
            )

            Divider()

            PermissionItem(
                icon: "figure.walk.circle",
                title: localized("settings.permission.accessibility"),
                hint: localized("settings.permission.accessibility.hint"),
                isGranted: viewModel.hasAccessibilityPermission,
                isChecking: viewModel.isCheckingPermissions,
                pane: .accessibility,
                onGrant: { viewModel.requestAccessibilityPermission() }
            )

            Divider()

            PermissionItem(
                icon: "folder",
                title: localized("settings.save.location"),
                hint: localized("settings.save.location.message"),
                isGranted: viewModel.hasFolderAccessPermission,
                isChecking: viewModel.isCheckingPermissions,
                onGrant: { viewModel.requestFolderAccess() }
            )

            HStack {
                Spacer()
                Button {
                    viewModel.checkPermissions()
                } label: {
                    Label(localized("action.reset"), systemImage: "arrow.clockwise")
                }
                .buttonStyle(.borderless)
            }
        }
        .onAppear {
            viewModel.checkPermissions()
        }
    }
}

struct PermissionItem: View {
    let icon: String
    let title: String
    let hint: String
    let isGranted: Bool
    let isChecking: Bool
    var pane: PermissionFlowPane? = nil
    let onGrant: () -> Void

    @StateObject private var controller = PermissionFlowController()
    @State private var buttonFrame: CGRect = .zero

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                HStack(spacing: 8) {
                    Image(systemName: icon)
                        .foregroundStyle(.secondary)
                        .frame(width: 20)
                    Text(title)
                }

                Spacer()

                if isChecking {
                    ProgressView()
                        .controlSize(.small)
                } else {
                    HStack(spacing: 8) {
                        if isGranted {
                            Image(systemName: "checkmark.circle.fill")
                                .foregroundStyle(.green)
                            Text(localized("settings.permission.granted"))
                                .foregroundStyle(.secondary)
                        } else {
                            Image(systemName: "xmark.circle.fill")
                                .foregroundStyle(.red)

                            Button {
                                if let pane {
                                    let screenRect = convertToScreen(buttonFrame)
                                    controller.authorize(
                                        pane: pane,
                                        suggestedAppURLs: [Bundle.main.bundleURL],
                                        sourceFrameInScreen: screenRect
                                    )
                                }
                                onGrant()
                            } label: {
                                Text(localized("settings.permission.grant"))
                            }
                            .buttonStyle(.borderedProminent)
                            .controlSize(.small)
                            .background(
                                GeometryReader { geo in
                                    Color.clear
                                        .onAppear {
                                            buttonFrame = geo.frame(in: .global)
                                        }
                                        .onChange(of: geo.frame(in: .global)) { _, newValue in
                                            buttonFrame = newValue
                                        }
                                }
                            )
                        }
                    }
                }
            }

            if !isGranted && !isChecking {
                Text(hint)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(
            Text("""
                \(title), \
                \(isGranted ? localized("settings.permission.granted") : localized("settings.permission.required"))
                """)
        )
        .onChange(of: isGranted) { _, newValue in
            if newValue {
                controller.closePanel()
            }
        }
    }

    private func convertToScreen(_ rect: CGRect) -> CGRect {
        guard let window = NSApp.keyWindow else { return rect }
        return window.convertToScreen(rect)
    }
}

// MARK: - Save Location Picker

struct SaveLocationPicker: View {
    @Bindable var viewModel: SettingsViewModel

    var body: some View {
        HStack {
            VStack(alignment: .leading, spacing: 4) {
                Text(viewModel.saveLocationPath)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .truncationMode(.middle)
            }

            Spacer()

            Button {
                viewModel.selectSaveLocation()
            } label: {
                Text(localized("settings.save.location.choose"))
            }

            Button {
                viewModel.revealSaveLocation()
            } label: {
                Image(systemName: "folder")
            }
            .help(localized("settings.save.location.reveal"))
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(Text("\(localized("settings.save.location")): \(viewModel.saveLocationPath)"))
    }
}

// MARK: - App Language Picker

struct AppLanguagePicker: View {
    @State private var selectedLanguage: AppLanguage = .system
    @State private var isInitialized = false

    var body: some View {
        HStack {
            Text(localized("settings.language"))

            Spacer()

            Picker("", selection: $selectedLanguage) {
                ForEach(AppLanguage.allCases) { language in
                    Text(language.displayName)
                        .tag(language)
                }
            }
            .pickerStyle(.menu)
            .labelsHidden()
            .frame(minWidth: 120)
            .onChange(of: selectedLanguage) { _, newValue in
                guard isInitialized else { return }
                Task { @MainActor in
                    LanguageManager.shared.currentLanguage = newValue
                }
            }
        }
        .onAppear {
            selectedLanguage = LanguageManager.shared.currentLanguage
            isInitialized = true
        }
    }
}
