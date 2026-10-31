import XCTest
@testable import TransFrame

@MainActor
final class LanguageManagerInitializationTests: XCTestCase {
    func testInitializationDoesNotNotifyBeforeSingletonIsReady() {
        var notifications = 0
        let observer = NotificationCenter.default.addObserver(
            forName: LanguageManager.languageDidChangeNotification,
            object: nil,
            queue: .main
        ) { _ in
            MainActor.assumeIsolated {
                _ = LanguageManager.shared.effectiveLocaleIdentifier
                notifications += 1
            }
        }
        defer { NotificationCenter.default.removeObserver(observer) }

        _ = LanguageManager.shared
        XCTAssertEqual(notifications, 0)
    }

    func testSystemModeNeverPersistsAppleLanguages() {
        // The self-lock regression: with language = .system, applyLanguage must
        // not write AppleLanguages. AppKit may seed the test host's domain
        // with the global preference list (e.g. [zh-Hans-US, en-US]) — that
        // is NOT our write; our write is a single resolved locale id. Assert
        // the value stays untouched (no single-resolved-locale overwrite).
        UserDefaults.standard.removeObject(forKey: "AppleLanguages")
        let seededByHost = UserDefaults.standard.array(forKey: "AppleLanguages")
        let baseline = seededByHost ?? []
        defer { UserDefaults.standard.removeObject(forKey: "AppleLanguages") }

        let manager = LanguageManager.shared
        let original = manager.currentLanguage
        defer { manager.currentLanguage = original }

        manager.currentLanguage = .system
        let after = UserDefaults.standard.array(forKey: "AppleLanguages") ?? []
        if baseline.isEmpty {
            XCTAssertTrue(after.isEmpty, "system mode must not write AppleLanguages, got \(after)")
        } else {
            XCTAssertEqual(after as? [String], baseline as? [String],
                           "system mode must not overwrite AppleLanguages")
        }
    }

    func testExplicitChoicePersistsAppleLanguages() {
        UserDefaults.standard.removeObject(forKey: "AppleLanguages")
        defer { UserDefaults.standard.removeObject(forKey: "AppleLanguages") }

        let manager = LanguageManager.shared
        let original = manager.currentLanguage
        defer { manager.currentLanguage = original }

        manager.currentLanguage = .simplifiedChinese
        XCTAssertEqual(
            UserDefaults.standard.array(forKey: "AppleLanguages") as? [String],
            ["zh-Hans"]
        )
    }
}
