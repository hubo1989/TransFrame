//
//  EngineHealthChecker.swift
//  TransFrame
//
//  Real availability probing for engine cards (PRD US-004, plan Phase 5).
//  Status dots reflect actual engine reachability, not just key presence.
//

import Foundation
import os

/// Four-state engine health for settings cards.
enum EngineHealth: Equatable, Sendable {
    /// Probe succeeded (green)
    case available
    /// Configured but the probe failed (red); payload carries a short reason
    case misconfigured(String)
    /// Missing credentials or required fields (orange) — no probe sent
    case unconfigured
    /// Configured but no free probe endpoint exists (gray)
    case unverified
    /// Probe in flight (UI transition state)
    case checking
}

/// Keys for health cache: builtin engines by type, compatible instances by UUID.
enum EngineHealthKey: Hashable, Sendable {
    case builtin(TranslationEngineType)
    case compatible(UUID)
}

/// Probe timeout — distinct from CancellationError so cooperative
/// cancellation of the caller's task is never misreported as a timeout
/// (which would cache a wrong red dot for 60s).
struct EngineHealthTimeoutError: Error, Sendable {}

/// Actor-based health probe with a 60s cache.
/// Probes use the providers' existing verifyConnection endpoints
/// (models/usage lists — free, no tokens consumed).
actor EngineHealthChecker {
    static let shared = EngineHealthChecker()

    private var cache: [EngineHealthKey: (health: EngineHealth, at: Date)] = [:]
    private let ttl: TimeInterval
    private let now: @Sendable () -> Date

    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "TransFrame",
        category: "EngineHealthChecker"
    )

    init(ttl: TimeInterval = 60, now: @escaping @Sendable () -> Date = Date.init) {
        self.ttl = ttl
        self.now = now
    }

    /// Cached health; refreshes when stale or absent.
    func health(for key: EngineHealthKey) async -> EngineHealth {
        if let entry = cache[key], now().timeIntervalSince(entry.at) < ttl {
            return entry.health
        }
        return await refresh(key)
    }

    /// Force a probe bypassing the cache.
    func refresh(_ key: EngineHealthKey) async -> EngineHealth {
        let result = await probe(key)
        cache[key] = (result, now())
        return result
    }

    /// Drop all cached entries (e.g. after a config save).
    func invalidateAll() {
        cache.removeAll()
    }

    private func probe(_ key: EngineHealthKey) async -> EngineHealth {
        switch key {
        case .builtin(.apple):
            // Local framework: always available
            return .available

        case .builtin(.mtranServer):
            // Probe the CONFIGURED endpoint: Configuration.default reads the
            // persisted host/port from UserDefaults (a remote server must
            // not be judged by localhost's fate, PR #94 review).
            let reachable = (try? await MTranServerChecker.checkAvailability(
                configuration: .default
            )) ?? false
            return reachable
                ? .available
                : .misconfigured(NSLocalizedString(
                    "engine.health.mtran.unreachable",
                    comment: "MTranServer endpoint unreachable"
                ))

        case .builtin(.google), .builtin(.baidu):
            // No free probe endpoint. unconfigured when credentials are
            // absent (fresh install must not read as configured-but-
            // unverifiable); unverified once the user has saved keys.
            let hasCreds = await KeychainService.shared.hasCredentials(for: keyEngine(key))
            return hasCreds ? .unverified : .unconfigured

        case .builtin(.claude), .builtin(.gemini), .builtin(.deepl):
            // Their verifyConnection actually translates "1" (PAID call) —
            // automatic probes must not consume quota. Same policy as
            // google/baidu: credentials gate, then unverified.
            let hasCreds = await KeychainService.shared.hasCredentials(for: keyEngine(key))
            return hasCreds ? .unverified : .unconfigured

        case .builtin(let engine):
            // openai / ollama / custom: free models-list probe behind a
            // credentials gate
            if engine.requiresAPIKey {
                let hasCreds = await KeychainService.shared.hasCredentials(for: engine)
                if !hasCreds {
                    return .unconfigured
                }
            }
            return await probeViaService(engine)

        case .compatible(let instanceID):
            let configs = await MainActor.run { AppSettings.shared.compatibleProviderConfigs }
            guard let config = configs.first(where: { $0.id == instanceID }) else {
                return .misconfigured(NSLocalizedString(
                    "engine.health.instance.deleted",
                    comment: "Compatible instance deleted"
                ))
            }
            if config.hasAPIKey {
                let hasCreds = await KeychainService.shared.hasCredentials(
                    forCompatibleId: config.keychainId
                )
                if !hasCreds {
                    return .unconfigured
                }
            }
            do {
                let provider = try await TranslationEngineRegistry.shared
                    .createCompatibleProvider(compatibleConfig: config)
                try await withTimeout(seconds: 5) {
                    try await provider.verifyConnection()
                }
                return .available
            } catch is EngineHealthTimeoutError {
                return .misconfigured(NSLocalizedString(
                    "engine.health.timeout",
                    comment: "Probe timed out"
                ))
            } catch {
                return .misconfigured(shortReason(error))
            }
        }
    }

    /// Extract the builtin engine from a key (nil-safe helper for the
    /// credentials gate in the unverified branches).
    nonisolated private func keyEngine(_ key: EngineHealthKey) -> TranslationEngineType {
        if case .builtin(let engine) = key { return engine }
        return .custom
    }

    private func probeViaService(_ engine: TranslationEngineType) async -> EngineHealth {
        do {
            try await withTimeout(seconds: 5) {
                try await TranslationService.shared.verifyConnection(for: engine)
            }
            return .available
        } catch is EngineHealthTimeoutError {
            return .misconfigured(NSLocalizedString(
                "engine.health.timeout",
                comment: "Probe timed out"
            ))
        } catch {
            return .misconfigured(shortReason(error))
        }
    }

    private func shortReason(_ error: Error) -> String {
        let text = (error as? LocalizedError)?.errorDescription
            ?? error.localizedDescription
        return String(text.prefix(120))
    }

    /// Probe several keys concurrently; publishes each result as it arrives
    /// via the handler (UI updates incrementally instead of waiting for the
    /// slowest engine, PR #94 review).
    func probeAll(
        _ keys: [EngineHealthKey],
        onResult: @Sendable @MainActor (EngineHealthKey, EngineHealth) -> Void
    ) async {
        await withTaskGroup(of: (EngineHealthKey, EngineHealth).self) { group in
            for key in keys {
                group.addTask {
                    let health = await self.refresh(key)
                    return (key, health)
                }
            }
            for await (key, health) in group {
                await onResult(key, health)
                self.cache[key] = (health, self.now())
            }
        }
    }

    /// Cancellation-based timeout wrapper.
    private func withTimeout<T: Sendable>(
        seconds: TimeInterval,
        operation: @escaping @Sendable () async throws -> T
    ) async throws -> T {
        try await withThrowingTaskGroup(of: T.self) { group in
            group.addTask { try await operation() }
            group.addTask {
                try await Task.sleep(nanoseconds: UInt64(seconds * 1_000_000_000))
                throw EngineHealthTimeoutError()
            }
            guard let first = try await group.next() else {
                throw EngineHealthTimeoutError()
            }
            group.cancelAll()
            return first
        }
    }
}
