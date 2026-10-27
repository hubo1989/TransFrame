import Foundation

/// Supported history entry category types
public enum HistoryCategory: String, Codable, Sendable, CaseIterable {
    case translation = "translation"
    case screenshot = "screenshot"
    case color = "color"
    case upload = "upload"

    public var displayName: String {
        switch self {
        case .translation: return "Translation"
        case .screenshot: return "Screenshot"
        case .color: return "Color"
        case .upload: return "Upload"
        }
    }

    public var systemImage: String {
        switch self {
        case .translation: return "character.book.closed"
        case .screenshot: return "camera.viewfinder"
        case .color: return "eyedropper"
        case .upload: return "icloud.and.arrow.up"
        }
    }
}

/// A single unified history entry.
/// Contains the source text, translated text, screenshot thumbnail, category, favorite state, and metadata.
struct TranslationHistory: Identifiable, Codable, Sendable {
    // MARK: - Types

    /// Coding keys for custom encoding/decoding
    private enum CodingKeys: String, CodingKey {
        case id
        case sourceText
        case translatedText
        case sourceLanguage
        case targetLanguage
        case timestamp
        case thumbnailData
        case category
        case isFavorite
    }

    // MARK: - Properties

    /// Unique identifier
    let id: UUID

    /// Original source text or primary description
    let sourceText: String

    /// Translated text, hex code, or public URL
    let translatedText: String

    /// Source language name or provider name
    let sourceLanguage: String

    /// Target language name
    let targetLanguage: String

    /// When the entry was performed
    let timestamp: Date

    /// JPEG thumbnail data (max 10KB, 128px on longest edge)
    let thumbnailData: Data?

    /// Category of history entry
    var category: HistoryCategory

    /// Starred favorite state
    var isFavorite: Bool

    // MARK: - Initialization

    init(
        id: UUID = UUID(),
        sourceText: String,
        translatedText: String,
        sourceLanguage: String,
        targetLanguage: String,
        timestamp: Date = Date(),
        thumbnailData: Data? = nil,
        category: HistoryCategory = .translation,
        isFavorite: Bool = false
    ) {
        self.id = id
        self.sourceText = sourceText
        self.translatedText = translatedText
        self.sourceLanguage = sourceLanguage
        self.targetLanguage = targetLanguage
        self.timestamp = timestamp
        self.thumbnailData = thumbnailData
        self.category = category
        self.isFavorite = isFavorite
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        self.id = try container.decode(UUID.self, forKey: .id)
        self.sourceText = try container.decode(String.self, forKey: .sourceText)
        self.translatedText = try container.decode(String.self, forKey: .translatedText)
        self.sourceLanguage = try container.decode(String.self, forKey: .sourceLanguage)
        self.targetLanguage = try container.decode(String.self, forKey: .targetLanguage)
        self.timestamp = try container.decode(Date.self, forKey: .timestamp)
        self.thumbnailData = try container.decodeIfPresent(Data.self, forKey: .thumbnailData)
        self.category = try container.decodeIfPresent(HistoryCategory.self, forKey: .category) ?? .translation
        self.isFavorite = try container.decodeIfPresent(Bool.self, forKey: .isFavorite) ?? false
    }

    // MARK: - Computed Properties

    /// A formatted description of the translation
    var description: String {
        "\(sourceLanguage) → \(targetLanguage)"
    }

    /// Whether the history entry has a thumbnail
    var hasThumbnail: Bool {
        thumbnailData != nil && !(thumbnailData?.isEmpty ?? true)
    }

    /// Truncated source text for preview (max 500 characters)
    var sourcePreview: String {
        String(sourceText.prefix(500))
    }

    /// Truncated translated text for preview (max 500 characters)
    var translatedPreview: String {
        String(translatedText.prefix(500))
    }

    /// Whether the source text is longer than preview
    var isSourceTruncated: Bool {
        sourceText.count > 500
    }

    /// Whether the translated text is longer than preview
    var isTranslatedTruncated: Bool {
        translatedText.count > 500
    }

    /// Formatted timestamp string
    var formattedTimestamp: String {
        let formatter = RelativeDateTimeFormatter()
        formatter.unitsStyle = .short
        return formatter.localizedString(for: timestamp, relativeTo: Date())
    }

    /// Full date string
    var fullDateString: String {
        let formatter = DateFormatter()
        formatter.dateStyle = .medium
        formatter.timeStyle = .short
        return formatter.string(from: timestamp)
    }
}

// MARK: - Search Match

extension TranslationHistory {
    /// Checks if the history entry matches the search query.
    /// - Parameter query: The search string to match against
    /// - Returns: True if the query is found in source or translated text
    func matches(_ query: String) -> Bool {
        guard !query.isEmpty else { return true }
        let lowercaseQuery = query.lowercased()
        return sourceText.lowercased().contains(lowercaseQuery) ||
               translatedText.lowercased().contains(lowercaseQuery)
    }
}

// MARK: - Factory from TranslationResult

extension TranslationHistory {
    /// Creates a history entry from a translation result.
    /// - Parameters:
    ///   - result: The translation result to convert
    ///   - thumbnailData: Optional thumbnail data
    /// - Returns: A new TranslationHistory entry
    static func from(
        result: TranslationResult,
        thumbnailData: Data? = nil
    ) -> TranslationHistory {
        TranslationHistory(
            sourceText: result.sourceText,
            translatedText: result.translatedText,
            sourceLanguage: result.sourceLanguage,
            targetLanguage: result.targetLanguage,
            timestamp: result.timestamp,
            thumbnailData: thumbnailData
        )
    }
}
