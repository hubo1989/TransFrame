import SwiftUI

struct PreviewActionButtons: View {
    @Bindable var viewModel: PreviewViewModel
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        HStack(spacing: 8) {
            cropButton

            Divider()
                .frame(height: 16)
                .accessibilityHidden(true)

            pinButton

            Divider()
                .frame(height: 16)
                .accessibilityHidden(true)

            undoRedoButtons

            Divider()
                .frame(height: 16)
                .accessibilityHidden(true)

            saveButton

            Divider()
                .frame(height: 16)
                .accessibilityHidden(true)

            ocrButton

            Divider()
                .frame(height: 16)
                .accessibilityHidden(true)

            smartMosaicButton

            Divider()
                .frame(height: 16)
                .accessibilityHidden(true)

            confirmButton
        }
        .buttonStyle(.accessoryBar)
        .accessibilityElement(children: .contain)
        .accessibilityLabel(Text("Screenshot actions"))
    }

    private var cropButton: some View {
        Button {
            viewModel.toggleCropMode()
        } label: {
            Image(systemName: "crop")
        }
        .buttonStyle(.accessoryBar)
        .background(
            viewModel.isCropMode
                ? Color.accentColor.opacity(0.2)
                : Color.clear
        )
        .clipShape(RoundedRectangle(cornerRadius: 4))
        .customTooltip(String(localized: "preview.tooltip.crop"))
        .accessibilityLabel(Text("preview.crop"))
        .accessibilityHint(Text("Press C to toggle"))
    }

    private var pinButton: some View {
        Button {
            viewModel.pinScreenshot()
        } label: {
            Image(systemName: viewModel.isPinned ? "pin.fill" : "pin")
        }
        .buttonStyle(.accessoryBar)
        .background(
            viewModel.isPinned
                ? Color.accentColor.opacity(0.2)
                : Color.clear
        )
        .clipShape(RoundedRectangle(cornerRadius: 4))
        .customTooltip(String(localized: "preview.tooltip.pin"))
        .accessibilityLabel(Text("preview.pin"))
        .accessibilityHint(Text("Press P to pin"))
    }

    private var undoRedoButtons: some View {
        Group {
            Button {
                viewModel.undo()
            } label: {
                Image(systemName: "arrow.uturn.backward")
            }
            .disabled(!viewModel.canUndo)
            .customTooltip(String(localized: "preview.tooltip.undo"))
            .accessibilityLabel(Text("action.undo"))
            .accessibilityHint(Text("Command Z"))

            Button {
                viewModel.redo()
            } label: {
                Image(systemName: "arrow.uturn.forward")
            }
            .disabled(!viewModel.canRedo)
            .customTooltip(String(localized: "preview.tooltip.redo"))
            .accessibilityLabel(Text("action.redo"))
            .accessibilityHint(Text("Command Shift Z"))
        }
    }

    private var saveButton: some View {
        Button {
            viewModel.saveScreenshot()
        } label: {
            if viewModel.isSaving {
                loadingIndicator
            } else {
                Image(systemName: "square.and.arrow.down")
            }
        }
        .disabled(viewModel.isSaving)
        .customTooltip(String(localized: "preview.tooltip.save"))
        .accessibilityLabel(Text(String(localized: viewModel.isSaving ? "preview.accessibility.saving" : "preview.accessibility.save")))
        .accessibilityHint(Text(String(localized: "preview.accessibility.hint.commandS")))
    }

    private var ocrButton: some View {
        Button {
            viewModel.performOCR()
        } label: {
            if viewModel.isPerformingOCR {
                ProgressView()
                    .controlSize(.small)
                    .frame(width: 16, height: 16)
            } else {
                Image(systemName: "text.viewfinder")
            }
        }
        .disabled(viewModel.isPerformingOCR)
        .customTooltip(String(localized: "preview.tooltip.ocr"))
    }

    /// Smart mosaic: detects sensitive privacy regions and masks them in one batch
    private var smartMosaicButton: some View {
        Button {
            viewModel.performSmartRedaction()
        } label: {
            if viewModel.isDetectingPrivacy {
                ProgressView()
                    .controlSize(.small)
                    .frame(width: 16, height: 16)
            } else {
                Image(systemName: "camera.metering.unknown")
            }
        }
        .disabled(viewModel.isDetectingPrivacy)
        .customTooltip(String(localized: "preview.tooltip.smartMosaic"))
    }

    /// Confirm button: copies to clipboard and dismisses (only on success)
    /// Users who don't want to copy can close the window directly
    private var confirmButton: some View {
        Button {
            viewModel.copyToClipboard(dismissOnSuccess: true)
        } label: {
            if viewModel.isCopying {
                loadingIndicator
            } else {
                Text(String(localized: "button.confirm"))
                    .fontWeight(.medium)
            }
        }
        .disabled(viewModel.isCopying)
        .customTooltip(String(localized: "preview.tooltip.confirm"))
        .accessibilityLabel(Text(String(localized: viewModel.isCopying ? "preview.accessibility.copying" : "preview.accessibility.confirm")))
        .accessibilityHint(Text(String(localized: "preview.accessibility.hint.enter")))
    }

    @ViewBuilder
    private var loadingIndicator: some View {
        if reduceMotion {
            Image(systemName: "ellipsis")
                .frame(width: 16, height: 16)
        } else {
            ProgressView()
                .controlSize(.small)
                .frame(width: 16, height: 16)
        }
    }
}
