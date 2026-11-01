import XCTest
@testable import TransFrame

/// Tests for the color history feature (spec §5.3 shared acceptance
/// vectors): HEX formatting, search filtering, favorites stacking,
/// color-only clearing, and capacity eviction of the oldest entry.
final class ColorHistoryStoreTests: XCTestCase {
    // MARK: - Helpers

    @MainActor
    private func makeStore() -> HistoryStore {
        let suiteName = "test-color-history-\(UUID().uuidString)"
        let testDefaults = UserDefaults(suiteName: suiteName)!
        addTeardownBlock {
            testDefaults.removePersistentDomain(forName: suiteName)
        }
        return HistoryStore(userDefaults: testDefaults)
    }

    private func colorEntry(
        _ hex: String,
        favorite: Bool = false,
        timestamp: Date = Date()
    ) -> TranslationHistory {
        TranslationHistory(
            sourceText: "Color Picked",
            translatedText: hex,
            sourceLanguage: "RGB Hex",
            targetLanguage: hex,
            timestamp: timestamp,
            category: .color,
            isFavorite: favorite
        )
    }

    private func translationEntry(_ text: String) -> TranslationHistory {
        TranslationHistory(
            sourceText: text,
            translatedText: text + "-translated",
            sourceLanguage: "English",
            targetLanguage: "Chinese",
            category: .translation
        )
    }

    // MARK: - HEX Formatting (spec §5.3)

    func testNormalizeLowercaseToUppercase() {
        XCTAssertEqual(ColorHex.normalize("#1a2b3c"), "#1A2B3C")
        XCTAssertEqual(ColorHex.normalize("1a2b3c"), "#1A2B3C")
        XCTAssertEqual(ColorHex.normalize("#ABCDEF"), "#ABCDEF")
    }

    func testNormalizeExpandsThreeDigitShorthand() {
        XCTAssertEqual(ColorHex.normalize("#abc"), "#AABBCC")
        XCTAssertEqual(ColorHex.normalize("f0e"), "#FF00EE")
    }

    func testNormalizeRejectsInvalidInput() {
        XCTAssertNil(ColorHex.normalize("#12"))
        XCTAssertNil(ColorHex.normalize("#12345"))
        XCTAssertNil(ColorHex.normalize("#1234567"))
        XCTAssertNil(ColorHex.normalize("#GGGGGG"))
        XCTAssertNil(ColorHex.normalize(""))
        XCTAssertNil(ColorHex.normalize("   "))
    }

    func testRGBComponentsAndString() {
        let tuple = ColorHex.rgbComponents("#1A2B3C")
        XCTAssertEqual(tuple?.r, 0x1A)
        XCTAssertEqual(tuple?.g, 0x2B)
        XCTAssertEqual(tuple?.b, 0x3C)

        XCTAssertEqual(ColorHex.rgbString("#1A2B3C"), "RGB(26, 43, 60)")
        XCTAssertEqual(ColorHex.rgbString("#FF0000"), "RGB(255, 0, 0)")
    }

    // MARK: - Search Filtering (spec §4.3 / §5.3)

    @MainActor
    func testColorEntriesSearchIsCaseInsensitive() {
        let store = makeStore()
        store.addEntry(colorEntry("#1A2B3C"))
        store.addEntry(colorEntry("#4D5E6F"))
        store.addEntry(colorEntry("#AABBCC"))

        // spec §5.3 vector: `1a2b` → hits `#1A2B3C`
        let hits = store.colorEntries(matching: "1a2b", favoritesOnly: false)
        XCTAssertEqual(hits.map(\.translatedText), ["#1A2B3C"])

        // Uppercase fragment matches the same entry
        XCTAssertEqual(store.colorEntries(matching: "1A2B", favoritesOnly: false).count, 1)

        // Fragment `1A2` hits `#1A2B3C` (spec §6 US-CH4 vector)
        XCTAssertEqual(store.colorEntries(matching: "1A2", favoritesOnly: false).count, 1)

        // Empty / nil fragment = all colors
        XCTAssertEqual(store.colorEntries(matching: nil, favoritesOnly: false).count, 3)
        XCTAssertEqual(store.colorEntries(matching: "", favoritesOnly: false).count, 3)

        // No match
        XCTAssertTrue(store.colorEntries(matching: "ZZZZ", favoritesOnly: false).isEmpty)
    }

    @MainActor
    func testColorEntriesSearchOnlyMatchesColorCategory() {
        let store = makeStore()
        store.addEntry(colorEntry("#1A2B3C"))
        store.addEntry(translationEntry("#1A2B3C-like"))

        // A translation entry carrying hex-ish text must not leak into the
        // color grid results.
        let hits = store.colorEntries(matching: "1A2B", favoritesOnly: false)
        XCTAssertEqual(hits.count, 1)
        XCTAssertEqual(hits.first?.category, .color)
    }

    // MARK: - Favorites Filter (spec §4.3)

    @MainActor
    func testFavoritesOnlyStacksWithSearch() {
        let store = makeStore()
        let favorite = colorEntry("#1A2B3C", favorite: true)
        let plain = colorEntry("#1A2B99", favorite: false)
        store.addEntry(plain)
        store.addEntry(favorite)

        // Favorites alone
        let favorites = store.colorEntries(matching: nil, favoritesOnly: true)
        XCTAssertEqual(favorites.map(\.translatedText), ["#1A2B3C"])

        // Favorites stacked with a shared fragment
        let stacked = store.colorEntries(matching: "1A2B", favoritesOnly: true)
        XCTAssertEqual(stacked.map(\.translatedText), ["#1A2B3C"])

        // Search finds both when favorites are off
        XCTAssertEqual(store.colorEntries(matching: "1A2B", favoritesOnly: false).count, 2)
    }

    @MainActor
    func testNewPicksAppearAtTop() {
        let store = makeStore()
        store.addEntry(colorEntry("#111111", timestamp: Date(timeIntervalSince1970: 100)))
        store.addEntry(colorEntry("#222222", timestamp: Date(timeIntervalSince1970: 200)))

        // spec §4.5: newest first — `addEntry` inserts at index 0
        XCTAssertEqual(
            store.colorEntries(matching: nil, favoritesOnly: false).map(\.translatedText),
            ["#222222", "#111111"]
        )
    }

    // MARK: - Clear Only Colors (spec §4.3 / US-CH5)

    @MainActor
    func testClearColorHistoryLeavesOtherCategoriesUntouched() {
        let store = makeStore()
        store.addEntry(colorEntry("#1A2B3C"))
        store.addEntry(colorEntry("#4D5E6F", favorite: true))
        store.addEntry(translationEntry("hello"))
        store.addEntry(translationEntry("world"))

        let translationCountBefore = store.entries.filter { $0.category == .translation }.count
        XCTAssertEqual(translationCountBefore, 2)

        store.clearColorHistory()

        // Colors gone
        XCTAssertTrue(store.colorEntries(matching: nil, favoritesOnly: false).isEmpty)
        XCTAssertTrue(store.entries.filter { $0.category == .color }.isEmpty)

        // Translation count unchanged (spec §6 US-CH5)
        let translationCountAfter = store.entries.filter { $0.category == .translation }.count
        XCTAssertEqual(translationCountAfter, translationCountBefore)
    }

    // MARK: - Capacity Limit (spec §5.3)

    @MainActor
    func testCapacityEvictsOldestEntry() {
        let store = makeStore()

        // 51 sequential picks, oldest first inserted
        for index in 0..<51 {
            let hex = String(format: "#%02X%02X%02X", index, index, index)
            store.addEntry(colorEntry(hex, timestamp: Date(timeIntervalSince1970: Double(index))))
        }

        // spec §5.3: 51 picks → oldest 1 evicted → 50 remain, oldest
        // (#000000, index 0) gone, newest (#323232, index 50) at top
        let remaining = store.colorEntries(matching: nil, favoritesOnly: false)
        XCTAssertEqual(remaining.count, 50)
        XCTAssertFalse(remaining.contains { $0.translatedText == "#000000" })
        XCTAssertEqual(remaining.first?.translatedText, "#323232")
    }

    @MainActor
    func testCapacityKeepsFavoriteFlagOnDuplicatePick() {
        let store = makeStore()
        let starred = colorEntry("#1A2B3C", favorite: true)
        store.addEntry(starred)

        // Re-picking the same color must keep the favorite flag
        store.addEntry(colorEntry("#1A2B3C"))

        let hits = store.colorEntries(matching: "1A2B3C", favoritesOnly: false)
        XCTAssertEqual(hits.count, 1)
        XCTAssertTrue(hits[0].isFavorite)
    }
}
