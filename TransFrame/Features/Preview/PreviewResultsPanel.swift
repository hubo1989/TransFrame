import SwiftUI
import AppKit

struct PreviewResultsPanel: View {
    @Bindable var viewModel: PreviewViewModel
    @Binding var isExpanded: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Button {
                withAnimation(.easeInOut(duration: 0.2)) {
                    isExpanded.toggle()
                }
            } label: {
                HStack {
                    Image(systemName: isExpanded ? "chevron.down" : "chevron.right")
                        .frame(width: 12)
                    Text(String(localized: "preview.results.panel"))
                        .font(.caption)
                        .fontWeight(.medium)
                    Spacer()
                }
                .foregroundStyle(.secondary)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)

            if isExpanded {
                VStack(alignment: .leading, spacing: 12) {
                    if viewModel.hasOCRResults {
                        VStack(alignment: .leading, spacing: 4) {
                            HStack {
                                Text(String(localized: "preview.recognized.text"))
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                                Spacer()
                                Button {
                                    NSPasteboard.general.clearContents()
                                    NSPasteboard.general.setString(viewModel.combinedOCRText, forType: .string)
                                } label: {
                                    Image(systemName: "doc.on.doc")
                                        .font(.caption)
                                }
                                .buttonStyle(.plain)
                                .help(String(localized: "preview.copy.text"))
                            }
                            Text(viewModel.combinedOCRText)
                                .font(.body)
                                .textSelection(.enabled)
                        }
                    }
                }
                .padding(.top, 8)
            }
        }
    }
}
