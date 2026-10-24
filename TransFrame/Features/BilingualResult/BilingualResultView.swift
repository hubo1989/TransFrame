import SwiftUI
import AppKit

struct BilingualResultView: View {
    @Bindable var viewModel: BilingualResultViewModel
    @State private var imageScale: CGFloat = 1.0

    var body: some View {
        ZStack(alignment: .bottom) {
            // Main Canvas Scroll Area
            VStack(spacing: 0) {
                ScrollView([.horizontal, .vertical], showsIndicators: false) {
                    Image(decorative: viewModel.image, scale: viewModel.displayScaleFactor)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                        .scaleEffect(viewModel.scale)
                        .frame(
                            width: viewModel.imagePointWidth * viewModel.scale,
                            height: viewModel.imagePointHeight * viewModel.scale
                        )
                        .clipShape(RoundedRectangle(cornerRadius: 8))
                        .padding(16)
                        .onScrollWheelZoom { delta in
                            if delta > 0 {
                                viewModel.zoomIn()
                            } else {
                                viewModel.zoomOut()
                            }
                        }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }

            // Attached Floating Pill Toolbar
            toolBar
                .cursor(.arrow)
                .padding(.horizontal, 12)
                .padding(.vertical, 6)
                .background(.regularMaterial)
                .clipShape(RoundedRectangle(cornerRadius: 10))
                .shadow(color: .black.opacity(0.25), radius: 8, y: 2)
                .padding(.bottom, 12)
        }
        .overlay(alignment: .topTrailing) {
            Button {
                BilingualResultWindowController.shared.close()
            } label: {
                Image(systemName: "xmark.circle.fill")
                    .font(.system(size: 20))
                    .foregroundStyle(.secondary, Color.black.opacity(0.1))
                    .padding(10)
            }
            .buttonStyle(.plain)
            .help("Close (Esc)")
        }
        .background(
            RoundedRectangle(cornerRadius: 12)
                .fill(.ultraThinMaterial)
                .shadow(color: .black.opacity(0.35), radius: 16, y: 6)
        )
        .onKeyPress(.escape) {
            BilingualResultWindowController.shared.close()
            return .handled
        }
        .overlay(alignment: .top) {
            if let message = viewModel.copySuccessMessage {
                successToast(message: message, icon: "doc.on.clipboard.fill")
            }
            if let message = viewModel.saveSuccessMessage {
                successToast(message: message, icon: "checkmark.circle.fill")
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
    }

    private var toolBar: some View {
        HStack(spacing: 12) {
            if viewModel.isLoading {
                HStack(spacing: 8) {
                    ProgressView()
                        .controlSize(.small)
                    Text(viewModel.loadingMessage)
                        .font(.system(.callout, design: .default))
                        .foregroundStyle(.secondary)
                }

                Spacer()
            } else {
                Text(viewModel.dimensionsText)
                    .font(.system(.caption, design: .monospaced))
                    .foregroundStyle(.secondary)

                Divider()
                    .frame(height: 16)

                HStack(spacing: 4) {
                    Button(action: viewModel.zoomOut) {
                        Image(systemName: "minus.magnifyingglass")
                    }
                    .buttonStyle(.borderless)
                    .help(String(localized: "bilingualResult.zoomOut"))

                    Button(action: viewModel.resetZoom) {
                        Text("\(Int(viewModel.scale * 100))%")
                            .font(.system(.caption, design: .monospaced))
                            .frame(minWidth: 45)
                    }
                    .buttonStyle(.borderless)
                    .help(String(localized: "bilingualResult.resetZoom"))

                    Button(action: viewModel.zoomIn) {
                        Image(systemName: "plus.magnifyingglass")
                    }
                    .buttonStyle(.borderless)
                    .help(String(localized: "bilingualResult.zoomIn"))
                }

                Spacer()

                HStack(spacing: 8) {
                    Button(action: viewModel.copyImageToClipboard) {
                        Label(String(localized: "bilingualResult.copyImage"), systemImage: "photo")
                    }
                    .buttonStyle(.bordered)

                    Button(action: viewModel.copyTextToClipboard) {
                        Label(String(localized: "bilingualResult.copyText"), systemImage: "doc.on.clipboard")
                    }
                    .buttonStyle(.bordered)
                    .disabled(viewModel.translatedText.isEmpty)

                    Button(action: viewModel.saveImage) {
                        Label(String(localized: "bilingualResult.save"), systemImage: "square.and.arrow.down")
                    }
                    .buttonStyle(.bordered)

                    Button(action: {
                        let nsImage = NSImage(cgImage: viewModel.image, size: NSSize(width: viewModel.image.width, height: viewModel.image.height))
                        PinnedWindowsManager.shared.pin(image: nsImage)
                        BilingualResultWindowController.shared.close()
                    }) {
                        Label("钉图", systemImage: "pin")
                    }
                    .buttonStyle(.bordered)
                }
            }
        }
    }

    private func successToast(message: String, icon: String) -> some View {
        HStack(spacing: 8) {
            Image(systemName: icon)
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
        .animation(.easeInOut(duration: 0.3), value: message)
    }

}

#if DEBUG
#Preview {
    let testImage: CGImage = {
        let width = 800
        let height = 400
        guard let colorSpace = CGColorSpace(name: CGColorSpace.sRGB),
              let context = CGContext(
                  data: nil,
                  width: width,
                  height: height,
                  bitsPerComponent: 8,
                  bytesPerRow: 0,
                  space: colorSpace,
                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
              ),
              let image = context.makeImage() else {
            fatalError("Unable to create test image")
        }
        context.setFillColor(NSColor.systemBlue.cgColor)
        context.fill(CGRect(x: 0, y: 0, width: width, height: height))
        return image
    }()

    let viewModel = BilingualResultViewModel(image: testImage)
    return BilingualResultView(viewModel: viewModel)
        .frame(width: 600, height: 400)
}
#endif
