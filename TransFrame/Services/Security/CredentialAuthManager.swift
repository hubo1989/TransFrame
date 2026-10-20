import Foundation
import LocalAuthentication
import os

@MainActor
@Observable
final class CredentialAuthManager {
    static let shared = CredentialAuthManager()

    enum AuthState: Equatable {
        case notNeeded
        case locked
        case authenticating
        case unlocked
        case failed(String)
    }

    private let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "TransFrame", category: "CredentialAuthManager")

    private(set) var state: AuthState = .locked
    private(set) var hasStoredCredentials = false

    var isUnlocked: Bool {
        switch state {
        case .notNeeded, .unlocked:
            return true
        case .locked, .authenticating, .failed:
            return false
        }
    }

    private init() {}

    #if DEBUG
    func setAuthStateForTesting(_ newState: AuthState) {
        state = newState
    }
    #endif

    func refreshCredentialPresence() async {
        hasStoredCredentials = await KeychainService.shared.hasAnyCredentials()
        if !hasStoredCredentials {
            state = .notNeeded
        } else if case .notNeeded = state {
            state = .locked
        }
    }

    func authenticateAtLaunchIfNeeded() async {
        await refreshCredentialPresence()
        guard hasStoredCredentials else { return }
        _ = await authenticate(reason: localized("credential.auth.reason.startup"))
    }

    @discardableResult
    func authenticate(reason: String = localized("credential.auth.reason.settings")) async -> Bool {
        guard hasStoredCredentials else {
            state = .notNeeded
            return true
        }
        if case .unlocked = state {
            return true
        }

        state = .authenticating

        let context = LAContext()
        context.localizedCancelTitle = localized("button.cancel")

        do {
            try await context.evaluatePolicy(.deviceOwnerAuthentication, localizedReason: reason)
            state = .unlocked
            AppSettings.shared.reloadSecureCredentials()
            logger.info("Credential access unlocked for this app session")
            return true
        } catch {
            let message = Self.message(for: error)
            state = .failed(message)
            logger.error("Credential authentication failed: \(message, privacy: .private(mask: .hash))")
            return false
        }
    }

    private static func message(for error: Error) -> String {
        guard let laError = error as? LAError else {
            return error.localizedDescription
        }

        switch laError.code {
        case .userCancel, .appCancel, .systemCancel:
            return localized("credential.auth.error.cancelled")
        case .authenticationFailed:
            return localized("credential.auth.error.failed")
        case .biometryNotAvailable, .biometryNotEnrolled:
            return localized("credential.auth.error.biometry_unavailable")
        case .passcodeNotSet:
            return localized("credential.auth.error.passcode_not_set")
        default:
            return laError.localizedDescription
        }
    }
}
