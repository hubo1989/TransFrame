import Foundation
import AppKit
import CoreGraphics
import ImageIO
import UniformTypeIdentifiers

private enum HistoryThumbnailOptions {
    static let maxSize: CGFloat = 128
    static let maxDataSize = 10 * 1024
    static let quality: CGFloat = 0.7
}

/// Manages the translation history with thumbnail generation and persistence.
/// Runs on the main actor for UI integration.
@MainActor
final class HistoryStore: ObservableObject {
    // MARK: - Constants

    /// Maximum number of history entries to store
    private static let maxHistoryEntries = 50

    /// Default UserDefaults key for history data
    public static let defaultHistoryKey = "TransFrame.translationHistory"

    // MARK: - Properties

    private let userDefaults: UserDefaults
    private let historyKey: String

    /// The list of translation history entries (newest first)
    @Published private(set) var entries: [TranslationHistory] = []

    /// The current search query
    @Published private(set) var searchQuery: String = ""

    /// Active category filter (nil = all categories)
    @Published var selectedCategory: HistoryCategory? = nil {
        didSet { updateFilteredEntries() }
    }

    /// Whether to only show starred favorites
    @Published var onlyFavorites: Bool = false {
        didSet { updateFilteredEntries() }
    }

    /// Filtered entries based on search query and category
    @Published private(set) var filteredEntries: [TranslationHistory] = []

    /// Whether more entries can be loaded
    @Published private(set) var hasMoreEntries: Bool = false

    /// Number of entries currently displayed
    @Published private(set) var displayedCount: Int = 100

    /// Block-based notification observer tokens; removed on deinit because
    /// HistoryStore is recreated per window open and NotificationCenter
    /// otherwise keeps the closures (and duplicate handlers) alive forever.
    /// `nonisolated(unsafe)`: written only from the MainActor init path, and
    /// read in deinit when no concurrent writer can exist;
    /// `removeObserver` itself is thread-safe.
    nonisolated(unsafe) private var notificationTokens: [NSObjectProtocol] = []

    // MARK: - Initialization

    init(userDefaults: UserDefaults = .standard, historyKey: String = defaultHistoryKey) {
        self.userDefaults = userDefaults
        self.historyKey = historyKey
        loadHistory()
        updateFilteredEntries()
        setupNotificationObservers()
    }

    deinit {
        notificationTokens.forEach(NotificationCenter.default.removeObserver)
    }

    private func setupNotificationObservers() {
        notificationTokens.append(NotificationCenter.default.addObserver(
            forName: .colorDidPick,
            object: nil,
            queue: .main
        ) { [weak self] notif in
            guard let hex = notif.object as? String else { return }
            Task { @MainActor in
                let entry = TranslationHistory(
                    sourceText: "Color Picked",
                    translatedText: hex,
                    sourceLanguage: "RGB Hex",
                    targetLanguage: hex,
                    category: .color
                )
                self?.addEntry(entry)
            }
        })

        notificationTokens.append(NotificationCenter.default.addObserver(
            forName: .imageDidUpload,
            object: nil,
            queue: .main
        ) { [weak self] notif in
            guard let result = notif.object as? UploadResult else { return }
            Task { @MainActor in
                let entry = TranslationHistory(
                    sourceText: result.markdown,
                    translatedText: result.url,
                    sourceLanguage: result.provider.displayName,
                    targetLanguage: "Public URL",
                    category: .upload
                )
                self?.addEntry(entry)
            }
        })
    }

    /// Toggles favorite status for an entry
    func toggleFavorite(for id: UUID) {
        guard let index = entries.firstIndex(where: { $0.id == id }) else { return }
        entries[index].isFavorite.toggle()
        saveHistory()
        updateFilteredEntries()
    }

    /// Adds a general history entry
    func addEntry(_ entry: TranslationHistory) {
        // Deduplicate only on fully identical content, and carry over the
        // previous favorite flag — silently dropping a starred entry (the old
        // translatedText+category match) lost user state without notice.
        if let index = entries.firstIndex(where: {
            $0.sourceText == entry.sourceText &&
            $0.translatedText == entry.translatedText &&
            $0.category == entry.category
        }) {
            var merged = entry
            merged.isFavorite = entries[index].isFavorite
            entries[index] = merged
        } else {
            entries.insert(entry, at: 0)
        }
        if entries.count > Self.maxHistoryEntries {
            entries = Array(entries.prefix(Self.maxHistoryEntries))
        }
        saveHistory()
        updateFilteredEntries()
    }

    // MARK: - Public API

    /// Adds a new translation result to the history.
    /// - Parameters:
    ///   - result: The translation result to save
    ///   - image: Optional screenshot image for thumbnail generation
    func add(result: TranslationResult, image: CGImage? = nil) {
        guard let image else {
            add(result: result, thumbnailData: nil)
            return
        }

        Task.detached(priority: .utility) {
            let thumbnailData = Self.generateThumbnailData(from: image)
            await MainActor.run {
                self.add(result: result, thumbnailData: thumbnailData)
            }
        }
    }

    private func add(result: TranslationResult, thumbnailData: Data?) {
        let entry = TranslationHistory.from(result: result, thumbnailData: thumbnailData)

        // Remove existing entry with same content to avoid duplicates
        entries.removeAll { existing in
            existing.sourceText == result.sourceText &&
            existing.translatedText == result.translatedText
        }

        // Keep history ordered by the translation timestamp even when thumbnail work finishes out of order.
        let insertionIndex = entries.firstIndex { existing in
            existing.timestamp <= entry.timestamp
        } ?? entries.endIndex
        entries.insert(entry, at: insertionIndex)

        // Enforce maximum count
        if entries.count > Self.maxHistoryEntries {
            entries = Array(entries.prefix(Self.maxHistoryEntries))
        }

        saveHistory()
        updateFilteredEntries()
    }

    /// Removes a history entry.
    /// - Parameter entry: The entry to remove
    func remove(_ entry: TranslationHistory) {
        entries.removeAll { $0.id == entry.id }
        saveHistory()
        updateFilteredEntries()
    }

    /// Removes the entry at the specified index.
    /// - Parameter index: The index of the entry to remove
    func remove(at index: Int) {
        guard index >= 0 && index < filteredEntries.count else { return }
        let entry = filteredEntries[index]
        remove(entry)
    }

    /// Clears all history entries.
    func clear() {
        entries.removeAll()
        saveHistory()
        updateFilteredEntries()
    }

    /// Sets the search query and updates filtered entries.
    /// - Parameter query: The search string
    func search(_ query: String) {
        searchQuery = query
        updateFilteredEntries()
    }

    /// Loads more entries for scrolling.
    func loadMore() {
        displayedCount += 50
        updateFilteredEntries()
    }

    /// Copies the translated text to clipboard.
    /// - Parameter entry: The history entry whose translation to copy
    func copyTranslation(_ entry: TranslationHistory) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(entry.translatedText, forType: .string)
    }

    /// Copies the source text to clipboard.
    /// - Parameter entry: The history entry whose source to copy
    func copySource(_ entry: TranslationHistory) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(entry.sourceText, forType: .string)
    }

    /// Copies both source and translation to clipboard.
    /// - Parameter entry: The history entry to copy
    func copyBoth(_ entry: TranslationHistory) {
        let text = "\(entry.sourceText)\n\n--- \(entry.description) ---\n\n\(entry.translatedText)"
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }

    // MARK: - Persistence

    /// Loads history from UserDefaults
    private func loadHistory() {
        guard let data = userDefaults.data(forKey: historyKey) else {
            entries = []
            return
        }

        if let decoded = try? JSONDecoder().decode([TranslationHistory].self, from: data) {
            entries = decoded
        } else {
            entries = []
        }
    }

    /// Saves history to UserDefaults
    private func saveHistory() {
        if let data = try? JSONEncoder().encode(entries) {
            userDefaults.set(data, forKey: historyKey)
        }
    }

    // MARK: - Filter Management

    /// Updates filtered entries based on search query, category, and favorite state
    private func updateFilteredEntries() {
        var result = entries

        // Category filter
        if let cat = selectedCategory {
            result = result.filter { $0.category == cat }
        }

        // Favorites filter
        if onlyFavorites {
            result = result.filter { $0.isFavorite }
        }

        // Search query filter
        if !searchQuery.isEmpty {
            result = result.filter { $0.matches(searchQuery) }
        }

        let count = min(displayedCount, result.count)
        filteredEntries = Array(result.prefix(count))
        hasMoreEntries = filteredEntries.count < result.count && searchQuery.isEmpty
    }

    // MARK: - Thumbnail Generation

    /// Generates a JPEG thumbnail from a CGImage.
    /// - Parameter image: The source image
    /// - Returns: JPEG data for the thumbnail, or nil if generation fails
    nonisolated static func generateThumbnailData(from image: CGImage) -> Data? {
        let width = CGFloat(image.width)
        let height = CGFloat(image.height)

        // Calculate scaled size maintaining aspect ratio
        let scale: CGFloat
        if width > height {
            scale = HistoryThumbnailOptions.maxSize / width
        } else {
            scale = HistoryThumbnailOptions.maxSize / height
        }

        // Only scale down, not up
        let finalScale = min(scale, 1.0)
        let newWidth = Int(width * finalScale)
        let newHeight = Int(height * finalScale)

        // Create thumbnail context
        guard let context = CGContext(
            data: nil,
            width: newWidth,
            height: newHeight,
            bitsPerComponent: 8,
            bytesPerRow: 0,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ) else {
            return nil
        }

        // Draw scaled image
        context.interpolationQuality = .high
        context.draw(image, in: CGRect(x: 0, y: 0, width: newWidth, height: newHeight))

        // Get thumbnail image
        guard let thumbnailImage = context.makeImage() else {
            return nil
        }

        guard let jpegData = encodeJPEG(thumbnailImage, quality: HistoryThumbnailOptions.quality) else { return nil }

        // Check size and reduce quality if needed
        if jpegData.count > HistoryThumbnailOptions.maxDataSize {
            // Try with lower quality
            let lowerQuality: CGFloat = 0.5
            if let reducedData = encodeJPEG(thumbnailImage, quality: lowerQuality),
               reducedData.count <= HistoryThumbnailOptions.maxDataSize {
                return reducedData
            }
            // If still too large, return nil
            return nil
        }

        return jpegData
    }

    private nonisolated static func encodeJPEG(_ image: CGImage, quality: CGFloat) -> Data? {
        let data = NSMutableData()
        guard let destination = CGImageDestinationCreateWithData(
            data,
            UTType.jpeg.identifier as CFString,
            1,
            nil
        ) else {
            return nil
        }

        let options = [kCGImageDestinationLossyCompressionQuality: quality] as CFDictionary
        CGImageDestinationAddImage(destination, image, options)
        guard CGImageDestinationFinalize(destination) else { return nil }
        return data as Data
    }
}
