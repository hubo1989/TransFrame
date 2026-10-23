import SwiftUI
import AppKit

/// Embedded right-side results drawer shown inside the preview window.
/// Hosts the OCR text tab and the bilingual translation tab.
struct PreviewResultsPanel: View {
    @Bindable var viewModel: PreviewViewModel
    @Binding var isExpanded: Bool
    @State private var selectedTab: Tab = .ocr
    @State private var copiedFlash: String?

    enum Tab: Hashable {
        case ocr
        case translate
    }

    /// Common target languages for the quick switcher
    private static let targetLanguages: [TranslationLanguage] = [
        .chineseSimplified, .english, .japanese, .korean,
        .french, .german, .spanish, .russian
    ]

    var body: some View {
        VStack(spacing: 0) {
            header

            Divider()

            Group {
                switch selectedTab {
                case .ocr:
                    ocrContent
                case .translate:
                    translateContent
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .background(.regularMaterial)
    }

    // MARK: - Header

    private var header: some View {
        HStack(spacing: 8) {
            if let flash = copiedFlash {
                Text(flash)
                    .font(.caption2)
                    .foregroundStyle(.green)
                    .lineLimit(1)
                    .transition(.opacity)
            }
            Picker(String(localized: "preview.results.panel"), selection: $selectedTab) {
                Text(String(localized: "preview.panel.ocrTab")).tag(Tab.ocr)
                Text(String(localized: "preview.panel.translateTab")).tag(Tab.translate)
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .controlSize(.small)
            .frame(maxWidth: .infinity)

            rerunButton
            copyAllButton
            closeButton
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 8)
    }

    /// Re-runs the action for the active tab
    private var rerunButton: some View {
        Button {
            switch selectedTab {
            case .ocr:
                viewModel.performOCR()
            case .translate:
                viewModel.performTranslation()
            }
        } label: {
            Image(systemName: "arrow.clockwise")
                .font(.system(size: 11))
                .frame(width: 22, height: 22)
        }
        .buttonStyle(.plain)
        .customTooltip(String(localized: "preview.panel.rerun"))
    }

    private var copyAllButton: some View {
        Button {
            copyToClipboard(currentTabText)
        } label: {
            Image(systemName: "doc.on.doc")
                .font(.system(size: 11))
                .frame(width: 22, height: 22)
        }
        .buttonStyle(.plain)
        .disabled(currentTabText.isEmpty)
        .customTooltip(String(localized: "preview.panel.copyAll"))
    }

    private var closeButton: some View {
        Button {
            withAnimation(.easeInOut(duration: 0.2)) {
                isExpanded = false
            }
        } label: {
            Image(systemName: "xmark")
                .font(.system(size: 11))
                .frame(width: 22, height: 22)
        }
        .buttonStyle(.plain)
        .customTooltip(String(localized: "preview.panel.close"))
    }

    // MARK: - OCR Tab

    private var ocrContent: some View {
        ScrollView {
            if viewModel.isPerformingOCR {
                VStack(alignment: .leading, spacing: 8) {
                    ForEach(0..<5, id: \.self) { _ in
                        RoundedRectangle(cornerRadius: 3)
                            .fill(Color.primary.opacity(0.08))
                            .frame(width: .random(in: 120...260), height: 12)
                    }
                    Text(String(localized: "preview.panel.recognizing"))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(12)
            } else if let result = viewModel.ocrResult, !result.observations.isEmpty {
                SelectableTextBlock(
                    attributedText: ResultsTextBuilder.ocrText(
                        lines: sortedObservations(result).map(\.text)
                    ),
                    onLineCopied: { line in
                        copiedFlash = String(localized: "preview.panel.copyLine") + ": " + line
                        DispatchQueue.main.asyncAfter(deadline: .now() + 1.2) {
                            if copiedFlash?.hasPrefix(String(localized: "preview.panel.copyLine")) == true {
                                copiedFlash = nil
                            }
                        }
                    }
                )
                .padding(12)
            } else {
                emptyState(text: String(localized: "preview.panel.noResults"))
            }
        }
    }

    private func sortedObservations(_ result: OCRResult) -> [OCRText] {
        result.observations.sorted { $0.boundingBox.minY < $1.boundingBox.minY }
    }

    // MARK: - Translation Tab

    private var translateContent: some View {
        ScrollView {
            if viewModel.isTranslating {
                VStack(alignment: .leading, spacing: 8) {
                    ForEach(0..<4, id: \.self) { _ in
                        RoundedRectangle(cornerRadius: 3)
                            .fill(Color.primary.opacity(0.08))
                            .frame(width: .random(in: 120...240), height: 12)
                    }
                    Text(String(localized: "preview.panel.translating"))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(12)
            } else if viewModel.hasTranslationResults {
                VStack(alignment: .leading, spacing: 8) {
                    targetLanguagePicker
                    SelectableTextBlock(
                        attributedText: ResultsTextBuilder.translationText(segments: viewModel.bilingualSegments),
                        onLineCopied: { line in
                            copiedFlash = String(localized: "preview.panel.copyLine") + ": " + line
                            DispatchQueue.main.asyncAfter(deadline: .now() + 1.2) {
                                if copiedFlash?.hasPrefix(String(localized: "preview.panel.copyLine")) == true {
                                    copiedFlash = nil
                                }
                            }
                        }
                    )
                }
                .padding(12)
            } else {
                VStack(spacing: 12) {
                    emptyState(text: String(localized: "preview.panel.noResults"))
                    targetLanguagePicker
                    Button {
                        viewModel.performTranslation()
                    } label: {
                        Label(String(localized: "preview.panel.translate"), systemImage: "character.book.closed")
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.small)
                }
                .padding(12)
            }
        }
    }

    /// Quick target-language switcher; changing it re-runs the translation
    private var targetLanguagePicker: some View {
        HStack(spacing: 6) {
            Text(String(localized: "preview.panel.targetLanguage"))
                .font(.caption)
                .foregroundStyle(.secondary)
            Menu {
                ForEach(Self.targetLanguages, id: \.self) { language in
                    Button {
                        guard AppSettings.shared.translationTargetLanguage != language else { return }
                        AppSettings.shared.translationTargetLanguage = language
                        if viewModel.hasTranslationResults || viewModel.ocrResult != nil {
                            viewModel.performTranslation()
                        }
                    } label: {
                        if AppSettings.shared.translationTargetLanguage == language {
                            Label(language.localizedName, systemImage: "checkmark")
                        } else {
                            Text(language.localizedName)
                        }
                    }
                }
            } label: {
                Text(AppSettings.shared.translationTargetLanguage?.localizedName ?? "—")
                    .font(.caption)
            }
            .controlSize(.small)
            .fixedSize()
        }
    }

    // MARK: - Helpers

    private var currentTabText: String {
        switch selectedTab {
        case .ocr:
            return viewModel.combinedOCRText
        case .translate:
            return viewModel.bilingualSegments.map(\.translated).joined(separator: "\n")
        }
    }

    private func emptyState(text: String) -> some View {
        Text(text)
            .font(.caption)
            .foregroundStyle(.secondary)
            .frame(maxWidth: .infinity)
            .multilineTextAlignment(.center)
            .padding(24)
    }

    private func copyToClipboard(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }
}
