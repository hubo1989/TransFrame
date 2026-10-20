//
//  CredentialAccessRow.swift
//  TransFrame
//

import SwiftUI

/// Reusable row component for displaying and unlocking Keychain credential access
struct CredentialAccessRow: View {
    @Bindable var viewModel: SettingsViewModel

    var body: some View {
        HStack(alignment: .center, spacing: 12) {
            Image(systemName: iconName)
                .foregroundStyle(iconColor)
                .frame(width: 22)

            VStack(alignment: .leading, spacing: 4) {
                Text(statusTitle)
                    .font(.subheadline)
                Text(statusHint)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Spacer()

            if case .authenticating = viewModel.credentialAuth.state {
                ProgressView()
                    .controlSize(.small)
            } else if shouldShowUnlockButton {
                Button {
                    viewModel.unlockCredentialAccess()
                } label: {
                    Label(localized("settings.credentials.unlock"), systemImage: "touchid")
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.small)
            }
        }
        .task {
            await viewModel.credentialAuth.refreshCredentialPresence()
        }
    }

    private var shouldShowUnlockButton: Bool {
        switch viewModel.credentialAuth.state {
        case .locked, .failed:
            return true
        case .notNeeded, .authenticating, .unlocked:
            return false
        }
    }

    private var iconName: String {
        switch viewModel.credentialAuth.state {
        case .notNeeded:
            return "key.slash"
        case .unlocked:
            return "checkmark.circle.fill"
        case .authenticating:
            return "touchid"
        case .locked, .failed:
            return "lock.fill"
        }
    }

    private var iconColor: Color {
        switch viewModel.credentialAuth.state {
        case .notNeeded:
            return .secondary
        case .unlocked:
            return .green
        case .authenticating:
            return .blue
        case .locked, .failed:
            return .orange
        }
    }

    private var statusTitle: String {
        switch viewModel.credentialAuth.state {
        case .notNeeded:
            return localized("settings.credentials.not_needed")
        case .locked:
            return localized("settings.credentials.locked")
        case .authenticating:
            return localized("settings.credentials.authenticating")
        case .unlocked:
            return localized("settings.credentials.unlocked")
        case .failed:
            return localized("settings.credentials.failed")
        }
    }

    private var statusHint: String {
        switch viewModel.credentialAuth.state {
        case .notNeeded:
            return localized("settings.credentials.not_needed.hint")
        case .locked:
            return localized("settings.credentials.locked.hint")
        case .authenticating:
            return localized("settings.credentials.authenticating.hint")
        case .unlocked:
            return localized("settings.credentials.unlocked.hint")
        case .failed(let message):
            return message
        }
    }
}
