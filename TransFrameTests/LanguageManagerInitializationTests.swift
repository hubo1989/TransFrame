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
}
