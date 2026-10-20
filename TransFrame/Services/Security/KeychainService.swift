//
//  KeychainService.swift
//  TransFrame
//
//  Secure storage for API keys and credentials using macOS Keychain
//

import Foundation
import Security
import os.log

// MARK: - Keychain Service

/// Actor-based service for secure credential storage using macOS Keychain
actor KeychainService {
    /// Shared singleton instance
    static let shared = KeychainService()

    /// Service identifier for Keychain items
    static let serviceIdentifier = "com.transframe.credentials"

    /// PaddleOCR cloud account identifier
    static let paddleOCRAccount = "paddleocr_cloud"

    /// Logger instance
    private let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "TransFrame", category: "KeychainService")

    /// Internal service property for instance methods
    private var service: String { Self.serviceIdentifier }

    private init() {}

    // MARK: - Public API

    /// Save credentials for a translation engine
    /// - Parameters:
    ///   - apiKey: The API key to store
    ///   - engine: The engine type these credentials are for
    ///   - additionalData: Optional additional data (e.g., appID for Baidu)
    func saveCredentials(
        apiKey: String,
        for engine: TranslationEngineType,
        additionalData: [String: String]? = nil
    ) throws {
        let credentials = StoredCredentials(
            apiKey: apiKey,
            appID: additionalData?["appID"],
            additional: additionalData
        )

        guard let encodedData = try? JSONEncoder().encode(credentials) else {
            throw KeychainError.invalidData
        }

        try saveRaw(data: encodedData, account: engine.rawValue)
    }

    /// Retrieve stored credentials for an engine
    /// - Parameter engine: The engine type to get credentials for
    /// - Returns: The stored credentials, or nil if not found
    func getCredentials(for engine: TranslationEngineType) async throws -> StoredCredentials? {
        guard let data = try await loadRaw(account: engine.rawValue) else {
            return nil
        }
        return try JSONDecoder().decode(StoredCredentials.self, from: data)
    }

    /// Delete stored credentials for an engine
    /// - Parameter engine: The engine type to delete credentials for
    func deleteCredentials(for engine: TranslationEngineType) throws {
        try deleteRaw(account: engine.rawValue)
    }

    /// Check if credentials exist for an engine
    /// - Parameter engine: The engine type to check
    /// - Returns: True if credentials exist
    func hasCredentials(for engine: TranslationEngineType) -> Bool {
        return existsRaw(account: engine.rawValue)
    }

    /// Get only the API key for an engine (convenience method)
    /// - Parameter engine: The engine type
    /// - Returns: The API key, or nil if not found
    func getAPIKey(for engine: TranslationEngineType) async -> String? {
        do {
            return try await getCredentials(for: engine)?.apiKey
        } catch {
            logger.error("Error getting API key for \(engine.rawValue): \(error.localizedDescription)")
            return nil
        }
    }

    // MARK: - Compatible Engine Methods (String-based identifiers)

    /// Save credentials for a compatible engine instance
    /// - Parameters:
    ///   - apiKey: The API key to store
    ///   - compatibleId: The compatible engine identifier (e.g., "custom:0", "custom:1")
    func saveCredentials(apiKey: String, forCompatibleId compatibleId: String) throws {
        let credentials = StoredCredentials(apiKey: apiKey)
        guard let encodedData = try? JSONEncoder().encode(credentials) else {
            throw KeychainError.invalidData
        }
        try saveRaw(data: encodedData, account: compatibleId)
    }

    /// Retrieve stored credentials for a compatible engine instance
    /// - Parameter compatibleId: The compatible engine identifier
    /// - Returns: The stored credentials, or nil if not found
    func getCredentials(forCompatibleId compatibleId: String) async throws -> StoredCredentials? {
        guard let data = try await loadRaw(account: compatibleId) else {
            return nil
        }
        return try JSONDecoder().decode(StoredCredentials.self, from: data)
    }

    /// Check if credentials exist for a compatible engine instance
    /// - Parameter compatibleId: The compatible engine identifier
    /// - Returns: True if credentials exist
    func hasCredentials(forCompatibleId compatibleId: String) -> Bool {
        return existsRaw(account: compatibleId)
    }

    /// Delete stored credentials for a compatible engine instance
    /// - Parameter compatibleId: The compatible engine identifier
    func deleteCredentials(forCompatibleId compatibleId: String) throws {
        try deleteRaw(account: compatibleId)
    }

    /// Save credentials for a specific VLM provider
    func saveVLMCredentials(apiKey: String, for provider: VLMProviderType) throws {
        let account = "vlm_api_key_\(provider.rawValue)"
        let credentials = StoredCredentials(apiKey: apiKey)
        guard let encodedData = try? JSONEncoder().encode(credentials) else {
            throw KeychainError.invalidData
        }
        try saveRaw(data: encodedData, account: account)
    }

    /// Retrieve credentials for a specific VLM provider
    func getVLMCredentials(for provider: VLMProviderType) async throws -> StoredCredentials? {
        let account = "vlm_api_key_\(provider.rawValue)"
        if let data = try await loadRaw(account: account) {
            return try JSONDecoder().decode(StoredCredentials.self, from: data)
        }
        if provider == .openai {
            return try await getCredentials(forCompatibleId: "vlm_api_key")
        }
        return nil
    }

    /// Delete credentials for a specific VLM provider
    func deleteVLMCredentials(for provider: VLMProviderType) throws {
        let account = "vlm_api_key_\(provider.rawValue)"
        try deleteRaw(account: account)
        if provider == .openai {
            _ = try? deleteCredentials(forCompatibleId: "vlm_api_key")
        }
    }

    /// Delete all stored credentials
    func deleteAllCredentials() throws {
        try deleteAllRaw()
    }

    // MARK: - PaddleOCR Cloud Methods

    /// Save PaddleOCR cloud API key
    /// - Parameter apiKey: The API key to store
    func savePaddleOCRCredentials(apiKey: String) throws {
        let credentials = StoredCredentials(apiKey: apiKey)
        guard let encodedData = try? JSONEncoder().encode(credentials) else {
            throw KeychainError.invalidData
        }
        try saveRaw(data: encodedData, account: Self.paddleOCRAccount)
    }

    /// Retrieve stored PaddleOCR cloud API key
    /// - Returns: The stored API key, or nil if not found
    func getPaddleOCRCredentials() async -> String? {
        do {
            guard let data = try await loadRaw(account: Self.paddleOCRAccount) else {
                return nil
            }
            let credentials = try JSONDecoder().decode(StoredCredentials.self, from: data)
            return credentials.apiKey
        } catch {
            logger.error("Failed to retrieve PaddleOCR cloud credentials: \(error.localizedDescription)")
            return nil
        }
    }

    /// Delete stored PaddleOCR cloud credentials
    func deletePaddleOCRCredentials() throws {
        try deleteRaw(account: Self.paddleOCRAccount)
    }

    /// Checks whether any non-empty API credentials exist.
    func hasAnyCredentials() -> Bool {
        #if DEBUG
        let prefix = "com.transframe.credentials.debug."
        return UserDefaults.standard.dictionaryRepresentation().contains { key, value in
            guard key.hasPrefix(prefix),
                  let data = value as? Data,
                  let credentials = try? JSONDecoder().decode(StoredCredentials.self, from: data) else {
                return false
            }
            return !credentials.apiKey.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
        }
        #else
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitAll
        ]

        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        if status == errSecSuccess {
            guard let items = result as? [Data] else {
                return false
            }
            return items.contains { data in
                guard let credentials = try? JSONDecoder().decode(StoredCredentials.self, from: data) else {
                    return false
                }
                return !credentials.apiKey.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
            }
        } else if status == errSecItemNotFound {
            return false
        } else {
            logger.error("Failed to check credential presence: status \(status)")
            return false
        }
        #endif
    }

    // MARK: - Core Storage Operations

    private func saveRaw(data: Data, account: String) throws {
        #if DEBUG
        let key = debugKey(for: account)
        UserDefaults.standard.set(data, forKey: key)
        logger.info("[Debug Storage] Saved credentials for \(account)")
        #else
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account
        ]

        let status = SecItemCopyMatching(query as CFDictionary, nil)
        if status == errSecSuccess {
            let updateQuery: [String: Any] = [
                kSecValueData as String: data,
                kSecAttrAccessible as String: kSecAttrAccessibleWhenUnlockedThisDeviceOnly
            ]
            let updateStatus = SecItemUpdate(query as CFDictionary, updateQuery as CFDictionary)
            guard updateStatus == errSecSuccess else {
                logger.error("Failed to update credentials for \(account): \(updateStatus)")
                throw KeychainError.unexpectedStatus(updateStatus)
            }
            logger.info("Updated credentials for \(account)")
        } else if status == errSecItemNotFound {
            let addQuery: [String: Any] = [
                kSecClass as String: kSecClassGenericPassword,
                kSecAttrService as String: service,
                kSecAttrAccount as String: account,
                kSecValueData as String: data,
                kSecAttrAccessible as String: kSecAttrAccessibleWhenUnlockedThisDeviceOnly
            ]
            let addStatus = SecItemAdd(addQuery as CFDictionary, nil)
            guard addStatus == errSecSuccess else {
                logger.error("Failed to save credentials for \(account): \(addStatus)")
                throw KeychainError.unexpectedStatus(addStatus)
            }
            logger.info("Saved credentials for \(account)")
        } else {
            logger.error("Failed to check credentials for \(account): \(status)")
            throw KeychainError.unexpectedStatus(status)
        }
        #endif
    }

    private func loadRaw(account: String) async throws -> Data? {
        let accessAllowed = await MainActor.run {
            CredentialAuthManager.shared.isUnlocked
        }
        guard accessAllowed else {
            logger.error("Credential access denied before app-session authentication for \(account)")
            throw KeychainError.authenticationRequired
        }

        #if DEBUG
        let key = debugKey(for: account)
        let data = UserDefaults.standard.data(forKey: key)
        if data != nil {
            logger.debug("[Debug Storage] Retrieved credentials for \(account)")
        }
        return data
        #else
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne
        ]

        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)

        guard status == errSecSuccess else {
            if status == errSecItemNotFound {
                logger.debug("No credentials found for \(account)")
                return nil
            }
            logger.error("Failed to retrieve credentials for \(account): \(status)")
            throw KeychainError.unexpectedStatus(status)
        }

        return result as? Data
        #endif
    }

    private func deleteRaw(account: String) throws {
        #if DEBUG
        let key = debugKey(for: account)
        UserDefaults.standard.removeObject(forKey: key)
        logger.info("[Debug Storage] Deleted credentials for \(account)")
        #else
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account
        ]

        let status = SecItemDelete(query as CFDictionary)
        guard status == errSecSuccess || status == errSecItemNotFound else {
            logger.error("Failed to delete credentials for \(account): \(status)")
            throw KeychainError.unexpectedStatus(status)
        }
        logger.info("Deleted credentials for \(account)")
        #endif
    }

    private func existsRaw(account: String) -> Bool {
        #if DEBUG
        let key = debugKey(for: account)
        return UserDefaults.standard.data(forKey: key) != nil
        #else
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecReturnData as String: false,
            kSecMatchLimit as String: kSecMatchLimitOne
        ]

        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        if status == errSecSuccess {
            return true
        } else if status == errSecItemNotFound {
            return false
        } else {
            logger.error("Failed to check existence of credentials for \(account): status \(status)")
            return false
        }
        #endif
    }

    private func deleteAllRaw() throws {
        #if DEBUG
        let defaults = UserDefaults.standard
        let prefix = "com.transframe.credentials.debug."
        let keysToRemove = defaults.dictionaryRepresentation().keys.filter { $0.hasPrefix(prefix) }
        for key in keysToRemove {
            defaults.removeObject(forKey: key)
        }
        logger.info("[Debug Storage] Deleted all credentials")
        #else
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service
        ]

        let status = SecItemDelete(query as CFDictionary)
        guard status == errSecSuccess || status == errSecItemNotFound else {
            throw KeychainError.unexpectedStatus(status)
        }
        logger.info("Deleted all credentials")
        #endif
    }

    private func debugKey(for account: String) -> String {
        return "com.transframe.credentials.debug.\(account)"
    }

    // MARK: - Synchronous Keychain Access for AppSettings (Non-isolated static helpers)

    @MainActor
    static func vlmAccount(for provider: VLMProviderType) -> String {
        "vlm_api_key_\(provider.rawValue)"
    }

    @MainActor
    static func loadVLMAPIKeySynchronously(for provider: VLMProviderType) -> String {
        guard CredentialAuthManager.shared.isUnlocked else { return "" }
        let account = vlmAccount(for: provider)

        #if DEBUG
        let key = "com.transframe.credentials.debug.\(account)"
        if let data = UserDefaults.standard.data(forKey: key),
           let credentials = try? JSONDecoder().decode(StoredCredentials.self, from: data) {
            return credentials.apiKey
        }
        if provider == .openai {
            return loadVLMAPIKeySynchronously()
        }
        return ""
        #else
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: KeychainService.serviceIdentifier,
            kSecAttrAccount as String: account,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne
        ]

        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)

        if status == errSecSuccess,
           let data = result as? Data,
           let credentials = try? JSONDecoder().decode(StoredCredentials.self, from: data) {
            return credentials.apiKey
        }

        if provider == .openai {
            return loadVLMAPIKeySynchronously()
        }
        return ""
        #endif
    }

    @MainActor
    static func loadVLMAPIKeySynchronously() -> String {
        guard CredentialAuthManager.shared.isUnlocked else { return "" }

        #if DEBUG
        let key = "com.transframe.credentials.debug.vlm_api_key"
        guard let data = UserDefaults.standard.data(forKey: key),
              let credentials = try? JSONDecoder().decode(StoredCredentials.self, from: data) else {
            return ""
        }
        return credentials.apiKey
        #else
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: KeychainService.serviceIdentifier,
            kSecAttrAccount as String: "vlm_api_key",
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne
        ]

        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)

        guard status == errSecSuccess,
              let data = result as? Data,
              let credentials = try? JSONDecoder().decode(StoredCredentials.self, from: data) else {
            return ""
        }

        return credentials.apiKey
        #endif
    }

    @MainActor
    static func loadPaddleOCRAPIKeySynchronously() -> String {
        guard CredentialAuthManager.shared.isUnlocked else { return "" }

        #if DEBUG
        let key = "com.transframe.credentials.debug.\(KeychainService.paddleOCRAccount)"
        guard let data = UserDefaults.standard.data(forKey: key),
              let credentials = try? JSONDecoder().decode(StoredCredentials.self, from: data) else {
            return ""
        }
        return credentials.apiKey
        #else
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: KeychainService.serviceIdentifier,
            kSecAttrAccount as String: KeychainService.paddleOCRAccount,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne
        ]

        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)

        guard status == errSecSuccess,
              let data = result as? Data,
              let credentials = try? JSONDecoder().decode(StoredCredentials.self, from: data) else {
            return ""
        }

        return credentials.apiKey
        #endif
    }
}

// MARK: - Stored Credentials

/// Structure for stored credentials
struct StoredCredentials: Codable, Sendable {
    /// Primary API key
    let apiKey: String

    /// Application ID (required for Baidu)
    let appID: String?

    /// Additional data fields
    let additional: [String: String]?

    init(apiKey: String, appID: String? = nil, additional: [String: String]? = nil) {
        self.apiKey = apiKey
        self.appID = appID
        self.additional = additional
    }
}

// MARK: - Keychain Error

/// Errors that can occur during Keychain operations
enum KeychainError: LocalizedError, Sendable {
    /// The requested item was not found in Keychain
    case itemNotFound

    /// An item with the same identifier already exists
    case duplicateItem

    /// The data format is invalid or corrupted
    case invalidData

    /// An unexpected OS status was returned
    case unexpectedStatus(OSStatus)

    /// Credentials exist but this app session has not been authenticated.
    case authenticationRequired

    var errorDescription: String? {
        switch self {
        case .itemNotFound:
            return NSLocalizedString(
                "keychain.error.item_not_found",
                comment: "Credentials not found in Keychain"
            )
        case .duplicateItem:
            return NSLocalizedString(
                "keychain.error.duplicate_item",
                comment: "Credentials already exist in Keychain"
            )
        case .invalidData:
            return NSLocalizedString(
                "keychain.error.invalid_data",
                comment: "Invalid credential data format"
            )
        case .unexpectedStatus(let status):
            return NSLocalizedString(
                "keychain.error.unexpected_status",
                comment: "Keychain operation failed with status: \(status)"
            ) + " (\(status))"
        case .authenticationRequired:
            return NSLocalizedString(
                "keychain.error.authentication_required",
                comment: "Credential access is locked"
            )
        }
    }

    var recoverySuggestion: String? {
        switch self {
        case .itemNotFound:
            return NSLocalizedString(
                "keychain.error.item_not_found.recovery",
                comment: "Please configure your API credentials in Settings"
            )
        case .duplicateItem:
            return NSLocalizedString(
                "keychain.error.duplicate_item.recovery",
                comment: "Try deleting existing credentials first"
            )
        case .invalidData:
            return NSLocalizedString(
                "keychain.error.invalid_data.recovery",
                comment: "Try re-entering your credentials"
            )
        case .unexpectedStatus:
            return NSLocalizedString(
                "keychain.error.unexpected_status.recovery",
                comment: "Please check your Keychain access permissions"
            )
        case .authenticationRequired:
            return NSLocalizedString(
                "keychain.error.authentication_required.recovery",
                comment: "Open Settings and unlock credential access"
            )
        }
    }
}

// MARK: - OSStatus Extension

extension OSStatus {
    /// Convert OSStatus to NSError for better error messages
    var asNSError: NSError {
        let domain = NSOSStatusErrorDomain
        let code = Int(self)
        let description = SecCopyErrorMessageString(self, nil) as String?
        return NSError(
            domain: domain,
            code: code,
            userInfo: [
                NSLocalizedDescriptionKey: description ?? "Unknown keychain error"
            ]
        )
    }
}
