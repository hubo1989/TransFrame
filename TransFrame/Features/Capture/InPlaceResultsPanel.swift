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

    /// Notifies the hosting overlay when tab/content changes affect the
    /// in-selection bilingual overlay rendering.
    var visualInvalidator: (() -> Void)?

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
    @State private var flashGeneration = 0

    /// Fixed widths so skeleton rows don't jitter on every re-render.
    private static let skeletonWidths: [CGFloat] = [220, 170, 200, 140, 210]

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
        .onChange(of: model.activeTab) { model.visualInvalidator?() }
        .onChange(of: model.segments) { model.visualInvalidator?() }
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
            Spacer(minLength: 0)
            // 无 tab 切换：内容随工具栏触发动作而定，要另一个结果从工具栏重新点。

            Button {
                model.onRerun?()
            } label: {
                Image(systemName: "arrow.clockwise")
                    .font(.system(size: 11))
                    .frame(width: 20, height: 20)
            }
            .buttonStyle(.plain)
            .customTooltip(String(localized: "preview.panel.rerun"))

            if model.activeTab == .translate {
                translationCopyButtons
            } else {
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
            }

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
        Group {
            if model.isLoading && model.activeTab == .ocr {
                loadingRows
            } else if !model.ocrLines.isEmpty {
                SelectableTextBlock(
                    attributedText: ResultsTextBuilder.ocrText(lines: model.ocrLines),
                    onLineCopied: flashCopy
                )
                .padding(10)
            } else {
                emptyState
            }
        }
    }

    // MARK: - Translation

    private var translateContent: some View {
        Group {
            if model.isLoading && model.activeTab == .translate {
                loadingRows
            } else if !model.segments.isEmpty {
                VStack(alignment: .leading, spacing: 8) {
                    targetLanguagePicker
                    SelectableTextBlock(
                        attributedText: ResultsTextBuilder.translationText(segments: model.segments),
                        onLineCopied: flashCopy
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

    /// 国旗横排一键切换目标语言，避免下拉二次点击。
    private var targetLanguagePicker: some View {
        HStack(spacing: 4) {
            ForEach(Self.targetLanguages, id: \.self) { language in
                languageFlagButton(for: language)
            }
        }
    }

    private func languageFlagButton(for language: TranslationLanguage) -> some View {
        let isSelected = AppSettings.shared.translationTargetLanguage == language
        return Button {
            guard AppSettings.shared.translationTargetLanguage != language else { return }
            AppSettings.shared.translationTargetLanguage = language
            model.onRerun?()
        } label: {
            Text(language.flagEmoji)
                .font(.system(size: 16))
                .frame(width: 27, height: 22)
        }
        .buttonStyle(.plain)
        .background(
            isSelected
                ? Color.accentColor.opacity(0.28)
                : Color.primary.opacity(0.06)
        )
        .clipShape(RoundedRectangle(cornerRadius: 4))
        .overlay(
            RoundedRectangle(cornerRadius: 4)
                .strokeBorder(isSelected ? Color.accentColor : Color.clear, lineWidth: 1)
        )
        .customTooltip(language.localizedName)
        .accessibilityLabel(Text(language.localizedName))
        .accessibilityAddTraits(isSelected ? [.isSelected] : [])
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
            ForEach(Self.skeletonWidths, id: \.self) { width in
                RoundedRectangle(cornerRadius: 4)
                    .fill(Color.primary.opacity(0.08))
                    .frame(width: width, height: 12)
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

    /// 翻译结果三键复制：原文 / 译文 / 全部（原文译文交替，与面板显示一致）。
    private var translationCopyButtons: some View {
        HStack(spacing: 3) {
            panelCopyButton(
                title: String(localized: "preview.panel.copySource"),
                text: model.segments.map(\.sourceText).joined(separator: "\n")
            )
            panelCopyButton(
                title: String(localized: "preview.panel.copyTranslation"),
                text: model.segments.map(\.translated).joined(separator: "\n")
            )
            panelCopyButton(
                title: String(localized: "preview.panel.copyAll"),
                text: ResultsTextBuilder.translationText(segments: model.segments).string
            )
        }
    }

    private func panelCopyButton(title: String, text: String) -> some View {
        Button {
            copy(text)
            flashMessage(title)
        } label: {
            HStack(spacing: 3) {
                Image(systemName: "doc.on.doc")
                    .font(.system(size: 10))
                Text(title)
                    .font(.caption2)
            }
            .padding(.horizontal, 6)
            .frame(height: 20)
        }
        .buttonStyle(.plain)
        .background(Color.primary.opacity(0.06), in: RoundedRectangle(cornerRadius: 4))
        .disabled(text.isEmpty)
        .customTooltip(title)
    }

    /// Shows the copy feedback; a newer flash invalidates older timers, so
    /// rapid line clicks don't clear each other's message early.
    private func flashCopy(line: String) {
        flashMessage(String(localized: "preview.panel.copyLine") + ": " + line)
    }

    private func flashMessage(_ message: String) {
        copiedFlash = message
        flashGeneration += 1
        let generation = flashGeneration
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.2) {
            if flashGeneration == generation {
                copiedFlash = nil
            }
        }
    }
}
