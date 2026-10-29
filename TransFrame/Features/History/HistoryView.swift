import SwiftUI
import AppKit

/// Main view for browsing and managing translation history.
struct HistoryView: View {
    @ObservedObject var store: HistoryStore

    /// Scroll position for detecting load more
    @Namespace private var scrollNamespace

    var body: some View {
        VStack(spacing: 0) {
            // Search bar
            SearchBar(store: store)

            // Category & Favorites Filter Bar
            CategoryFilterBar(store: store)

            Divider()

            // History list
            if store.filteredEntries.isEmpty {
                EmptyStateView(store: store)
            } else {
                ScrollView {
                    LazyVStack(spacing: 0, pinnedViews: [.sectionHeaders]) {
                        // Load more trigger at top
                        if store.hasMoreEntries && store.searchQuery.isEmpty {
                            LoadMoreTrigger()
                                .onAppear {
                                    store.loadMore()
                                }
                        }

                        // History entries
                        ForEach(store.filteredEntries) { entry in
                            HistoryEntryRow(entry: entry, store: store)
                        }
                    }
                }
            }
        }
        .frame(minWidth: 500, minHeight: 400)
    }
}

// MARK: - Search Bar

/// Search bar for filtering history entries.
private struct SearchBar: View {
    @ObservedObject var store: HistoryStore
    @FocusState private var isFocused: Bool
    @State private var showClearAlert = false

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: "magnifyingglass")
                .foregroundStyle(.secondary)

            TextField(String(localized: "history.search.placeholder"), text: Binding(
                get: { store.searchQuery },
                set: { store.search($0) }
            ))
            .focused($isFocused)
            .textFieldStyle(.plain)
            .onExitCommand {
                isFocused = false
            }

            if !store.searchQuery.isEmpty {
                Button {
                    store.search("")
                    isFocused = true
                } label: {
                    Image(systemName: "xmark.circle.fill")
                        .foregroundStyle(.secondary)
                }
                .buttonStyle(.plain)
            }

            Spacer()

            // Clear all button
            if !store.entries.isEmpty {
                Button(role: .destructive) {
                    showClearAlert = true
                } label: {
                    Image(systemName: "trash")
                        .foregroundStyle(.secondary)
                }
                .buttonStyle(.plain)
                .help(String(localized: "history.clear.all"))
            }
        }
        .padding(12)
        .background(Color(nsColor: .controlBackgroundColor))
        .alert(
            NSLocalizedString("history.clear.alert.title", comment: "Clear History"),
            isPresented: $showClearAlert
        ) {
            Button(NSLocalizedString("button.cancel", comment: "Cancel"), role: .cancel) {}
            Button(NSLocalizedString("button.clear", comment: "Clear"), role: .destructive) {
                store.clear()
            }
        } message: {
            Text(NSLocalizedString("history.clear.alert.message", comment: "Are you sure you want to clear all translation history?"))
        }
    }
}

// MARK: - Category Filter Bar

private struct CategoryFilterBar: View {
    @ObservedObject var store: HistoryStore

    var body: some View {
        HStack(spacing: 6) {
            filterButton(title: String(localized: "history.filter.all"), category: nil)

            ForEach(HistoryCategory.allCases, id: \.self) { cat in
                filterButton(title: cat.displayName, category: cat, icon: cat.systemImage)
            }

            Spacer()

            Button {
                store.onlyFavorites.toggle()
            } label: {
                HStack(spacing: 3) {
                    Image(systemName: store.onlyFavorites ? "star.fill" : "star")
                        .foregroundColor(store.onlyFavorites ? .yellow : .secondary)
                    Text(String(localized: "history.filter.starred"))
                        .font(.system(size: 11, weight: store.onlyFavorites ? .semibold : .regular))
                }
                .padding(.horizontal, 8)
                .padding(.vertical, 4)
                .background(store.onlyFavorites ? Color.yellow.opacity(0.15) : Color.clear)
                .cornerRadius(12)
            }
            .buttonStyle(.plain)
        }
        .padding(.horizontal, 12)
        .padding(.bottom, 8)
        .background(Color(nsColor: .controlBackgroundColor))
    }

    private func filterButton(title: String, category: HistoryCategory?, icon: String? = nil) -> some View {
        let isSelected = store.selectedCategory == category
        return Button {
            store.selectedCategory = category
        } label: {
            HStack(spacing: 3) {
                if let icon {
                    Image(systemName: icon)
                        .font(.system(size: 10))
                }
                Text(title)
                    .font(.system(size: 11, weight: isSelected ? .semibold : .regular))
            }
            .foregroundColor(isSelected ? .white : .primary)
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(isSelected ? Color.accentColor : Color.secondary.opacity(0.12))
            .cornerRadius(12)
        }
        .buttonStyle(.plain)
    }
}

// MARK: - Empty State

/// View shown when no history entries exist.
private struct EmptyStateView: View {
    @ObservedObject var store: HistoryStore

    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: "clock.arrow.circlepath")
                .font(.system(size: 48))
                .foregroundStyle(.secondary)

            if store.searchQuery.isEmpty {
                Text("history.empty.title")
                    .font(.headline)
                    .foregroundStyle(.secondary)

                Text("history.empty.message")
                    .font(.body)
                    .foregroundStyle(.tertiary)
            } else {
                Text("history.no.results.title")
                    .font(.headline)
                    .foregroundStyle(.secondary)

                Text("history.no.results.message")
                    .font(.body)
                    .foregroundStyle(.tertiary)

                Button {
                    store.search("")
                } label: {
                    Text("history.clear.search")
                }
                .buttonStyle(.borderedProminent)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

// MARK: - Load More Trigger

/// Invisible view that triggers loading more entries when visible.
private struct LoadMoreTrigger: View {
    var body: some View {
        Color.clear
            .frame(height: 1)
    }
}

// MARK: - History Entry Row

/// Row displaying a single history entry with source and translated text.
/// Layout adapts based on text shape: wide text → vertical (top/bottom), tall text → horizontal (side-by-side).
private struct HistoryEntryRow: View {
    let entry: TranslationHistory
    @ObservedObject var store: HistoryStore

    /// Determines if the text content is "wide" (few lines, long characters per line).
    /// Wide content uses vertical layout (top/bottom), tall content uses horizontal layout (side-by-side).
    private var isWideContent: Bool {
        let text = entry.sourceText
        let lines = text.components(separatedBy: .newlines)
        let lineCount = lines.count
        let maxLineLength = lines.map(\.count).max() ?? 0
        // Wide: few lines with long content, or single line
        return lineCount <= 3 || maxLineLength > 40
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            // Header with category icon, description, star, and timestamp
            HStack(spacing: 6) {
                Image(systemName: entry.category.systemImage)
                    .font(.caption)
                    .foregroundStyle(.secondary)

                Text(entry.description)
                    .font(.caption)
                    .foregroundStyle(.secondary)

                Spacer()

                Button {
                    store.toggleFavorite(for: entry.id)
                } label: {
                    Image(systemName: entry.isFavorite ? "star.fill" : "star")
                        .font(.caption)
                        .foregroundColor(entry.isFavorite ? .yellow : .secondary.opacity(0.4))
                }
                .buttonStyle(.plain)

                Text(entry.formattedTimestamp)
                    .font(.caption2)
                    .foregroundStyle(.tertiary)
            }

            if isWideContent {
                verticalLayout
            } else {
                horizontalLayout
            }
        }
        .padding(12)
        .background(Color(nsColor: .controlBackgroundColor))
        .contextMenu {
            EntryContextMenu(entry: entry, store: store)
        }
        .help(entry.fullDateString)
    }

    /// Vertical layout: source on top, translation below (for wide/short text)
    private var verticalLayout: some View {
        VStack(alignment: .leading, spacing: 0) {
            TextSection(
                text: entry.sourceText,
                label: String(localized: "history.source")
            )

            HStack(spacing: 4) {
                Rectangle()
                    .fill(.secondary.opacity(0.3))
                    .frame(width: 20, height: 1)
                Image(systemName: "arrow.down")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                Rectangle()
                    .fill(.secondary.opacity(0.3))
                    .frame(width: 20, height: 1)
            }
            .padding(.vertical, 4)

            TextSection(
                text: entry.translatedText,
                label: String(localized: "history.translation")
            )
        }
    }

    /// Horizontal layout: source on left, translation on right (for tall/narrow text)
    private var horizontalLayout: some View {
        HStack(alignment: .top, spacing: 0) {
            TextSection(
                text: entry.sourceText,
                label: String(localized: "history.source")
            )
            .frame(maxWidth: .infinity, alignment: .leading)

            VStack(spacing: 4) {
                Rectangle()
                    .fill(.secondary.opacity(0.3))
                    .frame(width: 1, height: 20)
                Image(systemName: "arrow.right")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                Rectangle()
                    .fill(.secondary.opacity(0.3))
                    .frame(width: 1, height: 20)
            }
            .padding(.horizontal, 8)

            TextSection(
                text: entry.translatedText,
                label: String(localized: "history.translation")
            )
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }
}

// MARK: - Text Section

/// Displays a section of text.
private struct TextSection: View {
    let text: String
    let label: String

    var body: some View {
        Text(text)
            .font(.system(.body, design: .rounded))
            .foregroundStyle(.primary)
            .textSelection(.enabled)
            .accessibilityLabel(label)
    }
}

// MARK: - Entry Context Menu

/// Context menu for history entries.
private struct EntryContextMenu: View {
    let entry: TranslationHistory
    @ObservedObject var store: HistoryStore

    var body: some View {
        Group {
            Button {
                store.copyTranslation(entry)
            } label: {
                Label(String(localized: "history.copy.translation"), systemImage: "doc.on.doc")
            }

            Button {
                store.copySource(entry)
            } label: {
                Label(String(localized: "history.copy.source"), systemImage: "doc.on.doc")
            }

            Button {
                store.copyBoth(entry)
            } label: {
                Label(String(localized: "history.copy.both"), systemImage: "doc.on.clipboard")
            }

            Divider()

            Button(role: .destructive) {
                store.remove(entry)
            } label: {
                Label(String(localized: "history.delete"), systemImage: "trash")
            }
        }
    }
}

// MARK: - Preview

#if DEBUG
#Preview {
    HistoryView(store: HistoryStore())
        .frame(width: 700, height: 500)
}
#endif
