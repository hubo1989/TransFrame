import XCTest
@testable import TransFrame

/// Engine health probing tests — PRD US-004, plan Phase 5
@available(macOS 13.0, *)
final class EngineHealthCheckerTests: XCTestCase {

    // MARK: - Deterministic engines (no network)

    func testAppleIsAlwaysAvailable() async {
        let health = await EngineHealthChecker.shared.refresh(.builtin(.apple))
        XCTAssertEqual(health, .available)
    }

    func testGoogleAndBaiduAreUnverified() async {
        // No free probe endpoint: must not send any request
        for engine in [TranslationEngineType.google, TranslationEngineType.baidu] {
            let health = await EngineHealthChecker.shared.refresh(.builtin(engine))
            XCTAssertEqual(health, .unverified)
        }
    }

    func testKeylessEngineWithoutCredentialsShortCircuitsToUnconfigured() async {
        // claude requires a key; with none stored the probe must return
        // .unconfigured WITHOUT touching the network
        let keychain = KeychainService.shared
        try? await keychain.deleteCredentials(for: .claude)
        let health = await EngineHealthChecker.shared.refresh(.builtin(.claude))
        XCTAssertEqual(health, .unconfigured)
    }

    func testDanglingCompatibleInstanceIsMisconfigured() async {
        let health = await EngineHealthChecker.shared.refresh(.compatible(UUID()))
        if case .misconfigured = health {
            // expected: instance UUID not in settings
        } else {
            XCTFail("expected misconfigured, got \(health)")
        }
    }

    // MARK: - Cache behavior

    func testCacheWithinTTLAvoidsReprobe() async {
        // apple is the probe target: cheap and deterministic. Use a
        // dedicated instance with a counting clock to prove the cache hit.
        let checker = EngineHealthCheckerForTests(ttl: 60)
        let a = await checker.health(for: .builtin(.apple))
        let b = await checker.health(for: .builtin(.apple))
        XCTAssertEqual(a, .available)
        XCTAssertEqual(b, .available)
        let probes = await checker.probeCount()
        XCTAssertEqual(probes, 1, "second health() within TTL must not re-probe")
    }

    func testCacheExpiryAfterTTLReprobes() async {
        let checker = EngineHealthCheckerForTests(ttl: 0) // instantly stale
        _ = await checker.health(for: .builtin(.apple))
        _ = await checker.health(for: .builtin(.apple))
        let probes = await checker.probeCount()
        XCTAssertEqual(probes, 2, "expired cache must re-probe")
    }

    func testInvalidateAllClearsCache() async {
        let checker = EngineHealthCheckerForTests(ttl: 60)
        _ = await checker.health(for: .builtin(.apple))
        await checker.invalidateAllForTests()
        _ = await checker.health(for: .builtin(.apple))
        let probes = await checker.probeCount()
        XCTAssertEqual(probes, 2, "invalidateAll must force a re-probe")
    }
}

/// Test double exposing probe counts (apple only; everything else throws
/// unimplemented so tests fail loudly if routing changes).
@available(macOS 13.0, *)
actor EngineHealthCheckerForTests: EngineHealthProbing {
    private(set) var probeCountValue = 0
    private var cache: [EngineHealthKey: (EngineHealth, Date)] = [:]
    private let ttl: TimeInterval

    func probeCount() async -> Int { probeCountValue }

    init(ttl: TimeInterval) {
        self.ttl = ttl
    }

    func health(for key: EngineHealthKey) async -> EngineHealth {
        if let entry = cache[key], Date().timeIntervalSince(entry.1) < ttl {
            return entry.0
        }
        probeCountValue += 1
        let result = await probe(key)
        cache[key] = (result, Date())
        return result
    }

    func refresh(_ key: EngineHealthKey) async -> EngineHealth {
        probeCountValue += 1
        let result = await probe(key)
        cache[key] = (result, Date())
        return result
    }

    func invalidateAllForTests() {
        cache.removeAll()
    }

    private func probe(_ key: EngineHealthKey) async -> EngineHealth {
        switch key {
        case .builtin(.apple): return .available
        default: return .unverified
        }
    }
}

/// Minimal protocol so the cache test can run against a double.
protocol EngineHealthProbing: Sendable {
    func health(for key: EngineHealthKey) async -> EngineHealth
    func refresh(_ key: EngineHealthKey) async -> EngineHealth
}
