import SwiftUI

struct ShortcutSettingsContent: View {
    @Bindable var viewModel: SettingsViewModel

    var body: some View {
        VStack(spacing: 16) {
            ShortcutRecorder(
                label: localized("settings.shortcut.fullscreen"),
                shortcut: viewModel.fullScreenShortcut,
                isRecording: viewModel.isRecordingFullScreenShortcut,
                onRecord: { viewModel.startRecordingFullScreenShortcut() },
                onReset: { viewModel.resetFullScreenShortcut() }
            )
            Divider().opacity(0.1)
            ShortcutRecorder(
                label: localized("settings.shortcut.selection"),
                shortcut: viewModel.selectionShortcut,
                isRecording: viewModel.isRecordingSelectionShortcut,
                onRecord: { viewModel.startRecordingSelectionShortcut() },
                onReset: { viewModel.resetSelectionShortcut() }
            )
            Divider().opacity(0.1)
            ShortcutRecorder(
                label: localized("settings.shortcut.translation.mode"),
                shortcut: viewModel.translationModeShortcut,
                isRecording: viewModel.isRecordingTranslationModeShortcut,
                onRecord: { viewModel.startRecordingTranslationModeShortcut() },
                onReset: { viewModel.resetTranslationModeShortcut() }
            )
            Divider().opacity(0.1)
            ShortcutRecorder(
                label: localized("settings.shortcut.text.selection.translation"),
                shortcut: viewModel.textSelectionTranslationShortcut,
                isRecording: viewModel.isRecordingTextSelectionTranslationShortcut,
                onRecord: { viewModel.startRecordingTextSelectionTranslationShortcut() },
                onReset: { viewModel.resetTextSelectionTranslationShortcut() }
            )
            Divider().opacity(0.1)
            ShortcutRecorder(
                label: localized("settings.shortcut.translate.and.insert"),
                shortcut: viewModel.translateAndInsertShortcut,
                isRecording: viewModel.isRecordingTranslateAndInsertShortcut,
                onRecord: { viewModel.startRecordingTranslateAndInsertShortcut() },
                onReset: { viewModel.resetTranslateAndInsertShortcut() }
            )
        }
        .macosGroupedCard()
    }
}

struct ShortcutRecorder: View {
    let label: String
    let shortcut: KeyboardShortcut
    let isRecording: Bool
    let onRecord: () -> Void
    let onReset: () -> Void

    var body: some View {
        HStack {
            Text(label)

            Spacer()

            if isRecording {
                Text(localized("settings.shortcut.recording"))
                    .foregroundStyle(.secondary)
                    .padding(.horizontal, 12)
                    .padding(.vertical, 6)
                    .background(Color.accentColor.opacity(0.2))
                    .clipShape(RoundedRectangle(cornerRadius: 8))
            } else {
                Button {
                    onRecord()
                } label: {
                    Text(shortcut.displayString)
                        .padding(.horizontal, 12)
                        .padding(.vertical, 6)
                        .background(Color.secondary.opacity(0.1))
                        .clipShape(RoundedRectangle(cornerRadius: 8))
                }
                .buttonStyle(.plain)
            }

            Button {
                onReset()
            } label: {
                Image(systemName: "arrow.counterclockwise")
            }
            .buttonStyle(.borderless)
            .help(localized("settings.shortcut.reset"))
            .disabled(isRecording)
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(Text("\(label): \(shortcut.displayString)"))
    }
}
