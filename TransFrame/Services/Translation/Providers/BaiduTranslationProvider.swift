//
//  BaiduTranslationProvider.swift
//  TransFrame
//
//  Baidu Translation API provider
//

import Foundation
import os.log
import CryptoKit

/// Baidu Translation API provider
actor BaiduTranslationProvider: TranslationProvider {
    // MARK: - Properties

    nonisolated let id: String = "baidu"
    nonisolated let name: String = "百度翻译"

    private let config: TranslationEngineConfig
    private let keychain: KeychainService
    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "TransFrame",
        category: "BaiduTranslationProvider"
    )

    private let baseURL = "https://fanyi-api.baidu.com/api/trans/vip/translate"

    // MARK: - Initialization

    init(config: TranslationEngineConfig, keychain: KeychainService) async throws {
        self.config = config
        self.keychain = keychain
    }

    // MARK: - TranslationProvider Protocol

    var isAvailable: Bool {
        get async {
            await keychain.hasCredentials(for: .baidu)
        }
    }

    func translate(
        text: String,
        from sourceLanguage: String?,
        to targetLanguage: String
    ) async throws -> TranslationResult {
        guard !text.isEmpty else {
            throw TranslationProviderError.emptyInput
        }

        guard let credentials = try await keychain.getCredentials(for: .baidu),
              let appID = credentials.appID else {
            throw TranslationProviderError.invalidConfiguration("AppID or Secret Key not configured")
        }

        let start = Date()
        let salt = String(Int.random(in: 100000...999999))
        let sign = generateSign(query: text, appID: appID, salt: salt, secretKey: credentials.apiKey)

        // Build URL with query parameters (Baidu uses GET)
        var components = URLComponents(string: baseURL)!
        components.queryItems = [
            URLQueryItem(name: "q", value: text),
            URLQueryItem(name: "from", value: mapLanguageCode(sourceLanguage)),
            URLQueryItem(name: "to", value: mapLanguageCode(targetLanguage)),
            URLQueryItem(name: "appid", value: appID),
            URLQueryItem(name: "salt", value: salt),
            URLQueryItem(name: "sign", value: sign)
        ]

        var request = URLRequest(url: components.url!)
        request.httpMethod = "GET"
        request.timeoutInterval = config.options?.timeout ?? 30

        let (data, response) = try await URLSession.shared.data(for: request)

        guard let httpResponse = response as? HTTPURLResponse else {
            throw TranslationProviderError.connectionFailed("Invalid response")
        }

        guard httpResponse.statusCode == 200 else {
            // Log status code only to avoid exposing user text in logs
            logger.error("Baidu API error status=\(httpResponse.statusCode)")
            throw TranslationProviderError.translationFailed("API error: \(httpResponse.statusCode)")
        }

        let result = try parseResponse(data)
        let latency = Date().timeIntervalSince(start)

        logger.info("Baidu translation completed in \(latency)s")

        return TranslationResult(
            sourceText: text,
            translatedText: result.translatedText,
            sourceLanguage: result.sourceLanguage,
            targetLanguage: result.targetLanguage
        )
    }

    func translate(
        texts: [String],
        from sourceLanguage: String?,
        to targetLanguage: String
    ) async throws -> [TranslationResult] {
        guard !texts.isEmpty else { return [] }

        // Baidu requires separate requests for each text
        var results: [TranslationResult] = []
        results.reserveCapacity(texts.count)

        for text in texts {
            let result = try await translate(
                text: text,
                from: sourceLanguage,
                to: targetLanguage
            )
            results.append(result)
        }

        return results
    }


    // MARK: - Private Methods

    /// Generate MD5 sign for Baidu API
    private func generateSign(query: String, appID: String, salt: String, secretKey: String) -> String {
        let input = appID + query + salt + secretKey
        return input.md5
    }

    /// Map language codes to Baidu format
    private func mapLanguageCode(_ code: String?) -> String {
        guard let code = code else { return "auto" }

        let mapping: [String: String] = [
            "auto": "auto",
            "en": "en",
            "zh": "zh",
            "zh-Hans": "zh",
            "zh-CN": "zh",
            "zh-Hant": "cht",
            "zh-TW": "cht",
            "ja": "jp",
            "ko": "kor",
            "fr": "fra",
            "de": "de",
            "es": "spa",
            "pt": "pt",
            "ru": "ru",
            "it": "it"
        ]

        return mapping[code] ?? code
    }

    private func parseResponse(_ data: Data) throws -> (translatedText: String, sourceLanguage: String, targetLanguage: String) {
        // Check for error response
        if let errorResponse = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
           let errorCode = errorResponse["error_code"] as? String,
           let errorMsg = errorResponse["error_msg"] as? String {
            logger.error("Baidu API error: \(errorCode) - \(errorMsg)")

            if errorCode == "54003" {
                throw TranslationProviderError.rateLimited(retryAfter: nil)
            } else if errorCode == "54004" {
                throw TranslationProviderError.invalidConfiguration("Invalid AppID or Secret Key")
            }

            throw TranslationProviderError.translationFailed("Baidu error: \(errorMsg)")
        }

        guard let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let transResult = json["trans_result"] as? [[String: Any]],
              !transResult.isEmpty else {
            throw TranslationProviderError.translationFailed("Failed to parse Baidu response")
        }

        let translatedText = transResult.compactMap { $0["dst"] as? String }
            .joined(separator: "\n")

        let from = (json["from"] as? String) ?? "auto"
        let to = (json["to"] as? String) ?? "zh"

        return (translatedText, from, to)
    }
}

// MARK: - String MD5 Extension

extension String {
    /// MD5 hash of the string
    var md5: String {
        let inputData = Data(self.utf8)
        let hashed = Insecure.MD5.hash(data: inputData)
        return hashed.map { String(format: "%02x", $0) }.joined()
    }
}
