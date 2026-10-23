import SwiftUI
import AppKit

/// Shared observable state for the in-place (selection overlay) results drawer.
@MainActor
@Observable
final class InPlaceResultsModel {
    enum Tab: Hashable {
        case ocr
        case translate
    }

    var isVisible: Bool = false
    var activeTab: Tab = .ocr
    var isLoading: Bool = false
    var ocrLines: [String] = []
    var segments: [BilingualSegment] = []
    var statusMessage: String?

    /// Re-run analysis for the active tab
    var onRerun: (() -> Void)?
    /// Close the drawer
    var onClose: (() -> Void)?
}

/// Right-side results drawer rendered inside the frozen selection overlay.
/// Shows OCR text and bilingual translation without leaving the capture window.
struct InPlaceResultsPanel: View {
    @Bindable var model: InPlaceResultsModel
    @State private var copiedFlash: String?

    private static let targetLanguages: [TranslationLanguage] = [
        .chineseSimplified, .english, .japanese, .korean,
        .french, .german, .spanish, .russian
    ]

    var body: some View {
        VStack(spacing: 0) {
            header
            Divider()
            Group {
                switch model.activeTab {
                case .ocr:
                    ocrContent
                case .translate:
                    translateContent
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        }
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 8))
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .stroke(Color.primary.opacity(0.12), lineWidth: 1)
        )
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
            Picker("", selection: $model.activeTab) {
                Text(String(localized: "preview.panel.ocrTab")).tag(InPlaceResultsModel.Tab.ocr)
                Text(String(localized: "preview.panel.translateTab")).tag(InPlaceResultsModel.Tab.translate)
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .controlSize(.small)

            Button {
                model.onRerun?()
            } label: {
                Image(systemName: "arrow.clockwise")
                    .font(.system(size: 11))
                    .frame(width: 20, height: 20)
            }
            .buttonStyle(.plain)
            .customTooltip(String(localized: "preview.panel.rerun"))

            Button {
                copy(currentTabText)
            } label: {
                Image(systemName: "doc.on.doc")
                    .font(.system(size: 11))
                    .frame(width: 20, height: 20)
            }
            .buttonStyle(.plain)
            .disabled(currentTabText.isEmpty)
            .customTooltip(String(localized: "preview.panel.copyAll"))

            Button {
                model.onClose?()
            } label: {
                Image(systemName: "xmark")
                    .font(.system(size: 11))
                    .frame(width: 20, height: 20)
            }
            .buttonStyle(.plain)
            .customTooltip(String(localized: "preview.panel.close"))
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 6)
    }

    // MARK: - OCR

    private var ocrContent: some View {
        ScrollView {
            if model.isLoading && model.activeTab == .ocr {
                loadingRows
            } else if !model.ocrLines.isEmpty {
                SelectableTextBlock(
                    attributedText: ResultsTextBuilder.ocrText(lines: model.ocrLines),
                    onLineCopied: { line in
                        copiedFlash = String(localized: "preview.panel.copyLine") + ": " + line
                        DispatchQueue.main.asyncAfter(deadline: .now() + 1.2) {
                            if copiedFlash?.hasPrefix(String(localized: "preview.panel.copyLine")) == true {
                                copiedFlash = nil
                            }
                        }
                    }
                )
                .padding(10)
            } else {
                emptyState
            }
        }
    }

    // MARK: - Translation

    private var translateContent: some View {
        ScrollView {
            if model.isLoading && model.activeTab == .translate {
                loadingRows
            } else if !model.segments.isEmpty {
                VStack(alignment: .leading, spacing: 8) {
                    targetLanguagePicker
                    SelectableTextBlock(
                        attributedText: ResultsTextBuilder.translationText(segments: model.segments),
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
                .padding(10)
            } else {
                VStack(spacing: 12) {
                    emptyState
                    targetLanguagePicker
                    Button {
                        model.onRerun?()
                    } label: {
                        Label(String(localized: "preview.panel.translate"), systemImage: "character.book.closed")
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.small)
                }
                .padding(10)
            }
        }
    }

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
                        model.onRerun?()
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
        switch model.activeTab {
        case .ocr:
            return model.ocrLines.joined(separator: "\n")
        case .translate:
            return model.segments.map(\.translated).joined(separator: "\n")
        }
    }

    private var loadingRows: some View {
        VStack(alignment: .leading, spacing: 8) {
            ForEach(0..<5, id: \.self) { _ in
                RoundedRectangle(cornerRadius: 3)
                    .fill(Color.primary.opacity(0.08))
                    .frame(width: .random(in: 100...220), height: 12)
            }
            Text(model.activeTab == .ocr
                 ? String(localized: "preview.panel.recognizing")
                 : String(localized: "preview.panel.translating"))
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(10)
    }

    private var emptyState: some View {
        Text(model.statusMessage ?? String(localized: "preview.panel.noResults"))
            .font(.caption)
            .foregroundStyle(.secondary)
            .frame(maxWidth: .infinity)
            .multilineTextAlignment(.center)
            .padding(20)
    }

    private func copy(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }
}
