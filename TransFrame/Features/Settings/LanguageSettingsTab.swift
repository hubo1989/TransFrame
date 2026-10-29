import SwiftUI

struct LanguageSettingsContent: View {
    @Bindable var viewModel: SettingsViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 24) {
            // Global Translation Languages
            VStack(alignment: .leading, spacing: 8) {
                Text(localized("settings.section.languages"))
                    .font(.headline)
                HStack(spacing: 16) {
                    VStack(alignment: .leading) {
                        Text(localized("translation.language.source"))
                            .font(.caption).foregroundStyle(.secondary)
                        SourceLanguagePicker(viewModel: viewModel)
                    }
                    Image(systemName: "arrow.right.circle.fill").font(.title2).foregroundStyle(
                        .secondary.opacity(0.5))
                    VStack(alignment: .leading) {
                        Text(localized("translation.language.target"))
                            .font(.caption).foregroundStyle(.secondary)
                        TargetLanguagePicker(viewModel: viewModel)
                    }
                }
            }

            Divider().opacity(0.3)

            // Translate and Insert Languages
            VStack(alignment: .leading, spacing: 8) {
                Text(localized("settings.translateAndInsert.language.section"))
                    .font(.headline)
                HStack(spacing: 16) {
                    VStack(alignment: .leading) {
                        Text(localized("settings.translateAndInsert.language.source"))
                            .font(.caption).foregroundStyle(.secondary)
                        TranslateAndInsertSourceLanguagePicker(viewModel: viewModel)
                    }
                    Image(systemName: "arrow.right.circle.fill").font(.title2).foregroundStyle(
                        .secondary.opacity(0.5))
                    VStack(alignment: .leading) {
                        Text(localized("settings.translateAndInsert.language.target"))
                            .font(.caption).foregroundStyle(.secondary)
                        TranslateAndInsertTargetLanguagePicker(viewModel: viewModel)
                    }
                }
            }
        }
        .macosGroupedCard()
    }
}

struct SourceLanguagePicker: View {
    @Bindable var viewModel: SettingsViewModel

    var body: some View {
        Picker(localized("translation.language.source"), selection: $viewModel.translationSourceLanguage) {
            ForEach(viewModel.availableSourceLanguages, id: \.rawValue) { language in
                Text(language.localizedName)
                    .tag(language)
            }
        }
        .pickerStyle(.menu)
        .help(localized("translation.language.source.hint"))
    }
}

struct TargetLanguagePicker: View {
    @Bindable var viewModel: SettingsViewModel

    var body: some View {
        HStack {
            Text(localized("translation.language.target"))

            Spacer()

            Picker(localized("translation.language.target"), selection: Binding(
                get: { viewModel.translationTargetLanguage },
                set: { viewModel.translationTargetLanguage = $0 }
            )) {
                Text(localized("translation.language.follow.system")).tag(Optional<TranslationLanguage>.none)
                Divider()
                ForEach(viewModel.availableTargetLanguages, id: \.rawValue) { language in
                    Text(language.localizedName).tag(Optional(language))
                }
            }
            .pickerStyle(.menu)
            .labelsHidden()
            .frame(minWidth: 140)
        }
        .help(localized("translation.language.target.hint"))
    }
}

// MARK: - Translate and Insert Language Pickers

struct TranslateAndInsertSourceLanguagePicker: View {
    @Bindable var viewModel: SettingsViewModel

    var body: some View {
        Picker(localized("settings.translateAndInsert.language.source"), selection: $viewModel.translateAndInsertSourceLanguage) {
            ForEach(viewModel.availableSourceLanguages, id: \.rawValue) { language in
                Text(language.localizedName)
                    .tag(language)
            }
        }
        .pickerStyle(.menu)
    }
}

struct TranslateAndInsertTargetLanguagePicker: View {
    @Bindable var viewModel: SettingsViewModel

    var body: some View {
        HStack {
            Text(localized("settings.translateAndInsert.language.target"))

            Spacer()

            Picker(localized("settings.translateAndInsert.language.target"), selection: Binding(
                get: { viewModel.translateAndInsertTargetLanguage },
                set: { viewModel.translateAndInsertTargetLanguage = $0 }
            )) {
                Text(localized("translation.language.follow.system")).tag(Optional<TranslationLanguage>.none)
                Divider()
                ForEach(viewModel.availableTargetLanguages, id: \.rawValue) { language in
                    Text(language.localizedName).tag(Optional(language))
                }
            }
            .pickerStyle(.menu)
            .labelsHidden()
            .frame(minWidth: 140)
        }
    }
}
