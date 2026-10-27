import SwiftUI
import AppKit

struct PreviewContentView: View {
    @Bindable var viewModel: PreviewViewModel
    @State private var imageDisplaySize: CGSize = .zero
    @State private var imageScale: CGFloat = 1.0
    @State private var isResultsPanelExpanded: Bool = false
    @FocusState private var isTextFieldFocused: Bool
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    /// Whether the right-side results drawer should occupy layout space
    private var showsResultsPanel: Bool {
        viewModel.hasOCRResults || viewModel.isPerformingOCR
            || viewModel.hasTranslationResults || viewModel.isTranslating
            || isResultsPanelExpanded
    }

    /// Canvas area + results drawer + floating toolbar (see body note).
    private var mainContent: some View {
        ZStack(alignment: .bottom) {
            // Main Canvas Area + Embedded Right Results Drawer
            HStack(spacing: 0) {
                ScrollView([.horizontal, .vertical], showsIndicators: false) {
                    PreviewAnnotatedImageView(
                        viewModel: viewModel,
                        imageDisplaySize: $imageDisplaySize,
                        imageScale: $imageScale,
                        isTextFieldFocused: $isTextFieldFocused
                    )
                    .clipShape(RoundedRectangle(cornerRadius: 8))
                    .padding(16)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)

                if showsResultsPanel && isResultsPanelExpanded {
                    PreviewResultsPanel(viewModel: viewModel, isExpanded: $isResultsPanelExpanded)
                        .frame(width: 300)
                        .transition(.move(edge: .trailing).combined(with: .opacity))
                } else if showsResultsPanel {
                    // Once collapsed, the drawer has no other way back:
                    // keep a slim handle so results stay reachable.
                    Button {
                        isResultsPanelExpanded = true
                    } label: {
                        Image(systemName: "sidebar.trailing")
                            .font(.system(size: 14, weight: .medium))
                            .foregroundStyle(.secondary)
                            .frame(width: 26)
                            .frame(maxHeight: .infinity)
                            .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .customTooltip(String(localized: "preview.panel.expandDrawer"), placement: .top)
                    .padding(.vertical, 8)
                }
            }
            .animation(reduceMotion ? nil : .easeInOut(duration: 0.25), value: isResultsPanelExpanded)

            // Attached Floating Pill Toolbar
            HStack(spacing: 12) {
                PreviewToolBar(viewModel: viewModel)
                Divider().frame(height: 16)
                PreviewActionButtons(viewModel: viewModel)
            }
            .cursor(.arrow)
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 10))
            .shadow(color: .black.opacity(0.25), radius: 8, y: 2)
            .padding(.bottom, 12)
        }
    }

    /// Top-right dismiss button (see body note).
    private var dismissButton: some View {
        Button {
            viewModel.dismiss()
        } label: {
            Image(systemName: "xmark.circle.fill")
                .font(.system(size: 20))
                .foregroundStyle(.secondary, Color.black.opacity(0.1))
                .padding(10)
        }
        .buttonStyle(.plain)
        .customTooltip(String(localized: "preview.tooltip.dismiss"), placement: .bottom)
    }

    var body: some View {
        // Body is split into sub-properties: the full inline chain exceeded
        // the type checker's expression budget (rdar-style "unable to
        // type-check in reasonable time").
        mainContent
            .overlay(alignment: .topTrailing) {
                dismissButton
            }
            .background(
                RoundedRectangle(cornerRadius: 12)
                    .fill(.ultraThinMaterial)
                    .shadow(color: .black.opacity(0.35), radius: 16, y: 6)
            )
            .onChange(of: viewModel.hasOCRResults) { _, hasResults in
                if hasResults {
                    isResultsPanelExpanded = true
                }
            }
            .onChange(of: viewModel.isTranslating) { _, translating in
                if translating {
                    isResultsPanelExpanded = true
                }
            }
            .overlay(alignment: .top) {
                if let message = viewModel.smartRedactionMessage {
                    SmartRedactionToast(message: message, isDetecting: viewModel.isDetectingPrivacy)
                }
            }
            .animation(.easeInOut(duration: 0.3), value: viewModel.smartRedactionMessage != nil)
            .alert(
                String(localized: "error.title"),
                isPresented: .constant(viewModel.errorMessage != nil),
                presenting: viewModel.errorMessage
            ) { _ in
                Button(String(localized: "button.ok")) {
                    viewModel.errorMessage = nil
                }
            } message: { message in
                Text(message)
            }
        .alert(
            String(localized: "save.success.title"),
            isPresented: .constant(viewModel.saveSuccessMessage != nil),
            presenting: viewModel.saveSuccessMessage
        ) { _ in
            Button(String(localized: "button.ok")) {
                viewModel.dismissSuccessMessage()
            }
        } message: { message in
            Text(message)
        }
        .overlay(alignment: .top) {
            if let message = viewModel.copySuccessMessage {
                HStack(spacing: 8) {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundColor(.green)
                    Text(message)
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 10)
                .background(.ultraThinMaterial)
                .cornerRadius(8)
                .shadow(radius: 4)
                .padding(.top, 20)
                .transition(.move(edge: .top).combined(with: .opacity))
                .onTapGesture {
                    viewModel.dismissCopySuccessMessage()
                }
            }
        }
        .animation(.easeInOut(duration: 0.3), value: viewModel.copySuccessMessage != nil)
    }
}

/// Smart-redaction status toast. Split out of the main body because the
/// inline ternary + modifier chain exceeded the type checker's budget.
private struct SmartRedactionToast: View {
    let message: String
    let isDetecting: Bool

    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: isDetecting ? "camera.metering.unknown" : "checkmark.circle.fill")
                .foregroundColor(isDetecting ? Color.secondary : Color.green)
            if isDetecting {
                ProgressView().controlSize(.mini)
            }
            Text(message)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
        .background(.ultraThinMaterial)
        .cornerRadius(8)
        .shadow(radius: 4)
        .padding(.top, 20)
        .transition(.move(edge: .top).combined(with: .opacity))
    }
}

#if DEBUG
#Preview {
    let testImage: CGImage = {
        let width = 800
        let height = 600
        guard let colorSpace = CGColorSpace(name: CGColorSpace.sRGB),
              let context = CGContext(
                  data: nil,
                  width: width,
                  height: height,
                  bitsPerComponent: 8,
                  bytesPerRow: 0,
                  space: colorSpace,
                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
               ) else {
            // Fallback: create a 1x1 placeholder image
            let placeholder = NSImage(size: CGSize(width: 1, height: 1))
            if let cgImage = placeholder.cgImage(forProposedRect: nil, context: nil, hints: nil) {
                return cgImage
            }
            // Ultimate fallback: create a 1x1 white CGImage
            let fallbackColorSpace = CGColorSpaceCreateDeviceRGB()
            guard let fallbackContext = CGContext(
                data: nil,
                width: 1,
                height: 1,
                bitsPerComponent: 8,
                bytesPerRow: 4,
                space: fallbackColorSpace,
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
            ), let fallbackImage = fallbackContext.makeImage() else {
                fatalError("Unable to create fallback image")
            }
            return fallbackImage
        }

        context.setFillColor(NSColor.systemBlue.cgColor)
        context.fill(CGRect(x: 0, y: 0, width: width, height: height))

        let fallbackImage = NSImage(size: CGSize(width: 1, height: 1))
        let cgFallback: CGImage
        if let img = fallbackImage.cgImage(forProposedRect: nil, context: nil, hints: nil) {
            cgFallback = img
        } else if let contextImage = context.makeImage() {
            cgFallback = contextImage
        } else {
            fatalError("Unable to create fallback CGImage")
        }
        return context.makeImage() ?? cgFallback
    }()

    let display = DisplayInfo(
        id: 1,
        name: "Built-in Display",
        frame: CGRect(x: 0, y: 0, width: 1920, height: 1080),
        scaleFactor: 2.0,
        isPrimary: true
    )

    let screenshot = Screenshot(
        image: testImage,
        sourceDisplay: display
    )

    let viewModel = PreviewViewModel(screenshot: screenshot)

    return PreviewContentView(viewModel: viewModel)
        .frame(width: 600, height: 400)
}
#endif
