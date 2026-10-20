import SwiftUI
import AppKit

struct PreviewContentView: View {
    @Bindable var viewModel: PreviewViewModel
    @State private var imageDisplaySize: CGSize = .zero
    @State private var imageScale: CGFloat = 1.0
    @State private var isResultsPanelExpanded: Bool = false
    @FocusState private var isTextFieldFocused: Bool
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        VStack(spacing: 0) {
            ScrollView([.horizontal, .vertical], showsIndicators: true) {
                PreviewAnnotatedImageView(
                    viewModel: viewModel,
                    imageDisplaySize: $imageDisplaySize,
                    imageScale: $imageScale,
                    isTextFieldFocused: $isTextFieldFocused
                )
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Color(nsColor: .windowBackgroundColor))

            Divider()

            infoBar
                .padding(.horizontal, 16)
                .padding(.vertical, 10)
                .background(.bar)

            if viewModel.hasOCRResults {
                Divider()
                PreviewResultsPanel(viewModel: viewModel, isExpanded: $isResultsPanelExpanded)
                    .padding(.horizontal, 16)
                    .padding(.vertical, 10)
                    .background(.bar)
            }
        }
        .onChange(of: viewModel.hasOCRResults) { _, hasResults in
            if hasResults {
                isResultsPanelExpanded = true
            }
        }
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

    private var infoBar: some View {
        HStack(spacing: 12) {
            HStack(spacing: 8) {
                Text(viewModel.dimensionsText)
                    .font(.system(.caption, design: .monospaced))
                    .foregroundStyle(.secondary)
                    .help(String(localized: "preview.image.dimensions"))

                Text("•")
                    .foregroundStyle(.tertiary)

                Text(viewModel.fileSizeText)
                    .font(.system(.caption, design: .monospaced))
                    .foregroundStyle(.secondary)
                    .help(String(localized: "preview.estimated.size"))
            }
            .fixedSize()

            Divider()
                .frame(height: 16)

            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    PreviewToolBar(viewModel: viewModel)
                }
                .padding(.horizontal, 4)
            }
            .frame(minWidth: 100)

            Divider()
                .frame(height: 16)

            PreviewActionButtons(viewModel: viewModel)
                .fixedSize()
        }
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
