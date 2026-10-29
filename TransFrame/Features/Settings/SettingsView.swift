import AppKit
import SwiftUI

struct SettingsView: View {
    @Bindable var viewModel: SettingsViewModel
    @State private var refreshID = UUID()
    @State private var selectedTab: SettingsTab = .general

    var body: some View {
        NavigationSplitView {
            List(SettingsTab.allCases, selection: $selectedTab) { tab in
                NavigationLink(value: tab) {
                    Label {
                        Text(tab.displayName)
                    } icon: {
                        Image(systemName: tab.icon)
                    }
                }
                .padding(.vertical, 4)
            }
            .listStyle(.sidebar)
            .navigationSplitViewColumnWidth(min: 180, ideal: 200)
        } detail: {
            ScrollView {
                VStack(spacing: 20) {
                    switch selectedTab {
                    case .general:
                        GeneralSettingsContent(viewModel: viewModel)
                    case .engines:
                        EngineSettingsContent(viewModel: viewModel)
                    case .prompts:
                        PromptSettingsContent(viewModel: viewModel)
                    case .languages:
                        LanguageSettingsContent(viewModel: viewModel)
                    case .shortcuts:
                        ShortcutSettingsContent(viewModel: viewModel)
                    case .advanced:
                        AdvancedSettingsContent(viewModel: viewModel)
                    }
                }
                .padding(.horizontal, 28)
                .padding(.top, 24)
                .padding(.bottom, 36)
            }
            .navigationTitle(selectedTab.displayName)
            .background(Color(.windowBackgroundColor))
        }
        .frame(width: 800, height: 600)
        .background(Color(.windowBackgroundColor))
        .id(refreshID)
        .onReceive(
            NotificationCenter.default.publisher(for: LanguageManager.languageDidChangeNotification)
        ) { _ in
            refreshID = UUID()
        }
        .alert(localized("error.title"), isPresented: $viewModel.showErrorAlert) {
            Button(localized("button.ok")) {
                viewModel.errorMessage = nil
            }
        } message: {
            if let message = viewModel.errorMessage {
                Text(message)
            }
        }
    }
}

extension View {
    @ViewBuilder
    func `if`<Content: View>(
        _ condition: Bool,
        transform: (Self) -> Content
    ) -> some View {
        if condition {
            transform(self)
        } else {
            self
        }
    }
}

#if DEBUG
    #Preview {
        SettingsView(viewModel: SettingsViewModel())
            .frame(width: 500, height: 600)
    }
#endif
