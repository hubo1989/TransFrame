//
//  ClaudeVLMProvider.swift
//  TransFrame
//
//  Created for US-006: Claude Vision Provider
//

import CoreGraphics
import Foundation
import os

// MARK: - Claude VLM Provider

/// VLM provider implementation for Anthropic Claude Vision models
struct ClaudeVLMProvider: VLMProvider, Sendable {
    // MARK: - Properties

    let id: String = "claude"
    let name: String = "Claude Vision"
    let configuration: VLMProviderConfiguration

    /// Default Anthropic API base URL
    static let defaultBaseURL = URL(string: "https://api.anthropic.com")!

    /// Default model for vision tasks
    static let defaultModel = "claude-sonnet-4-20250514"

    /// Anthropic API version header
    private static let apiVersion = "2023-06-01"

    /// Request timeout in seconds
    private let timeout: TimeInterval

    private let logger = Logger.translation

    // MARK: - Initialization

    /// Initialize with full configuration
    /// - Parameters:
    ///   - configuration: VLM provider configuration
    ///   - timeout: Request timeout in seconds (default: 60)
    init(configuration: VLMProviderConfiguration, timeout: TimeInterval = 60) {
        self.configuration = configuration
        self.timeout = timeout
    }

    /// Convenience initializer with individual parameters
    /// - Parameters:
    ///   - apiKey: Anthropic API key
    ///   - baseURL: API base URL (default: Anthropic's official endpoint)
    ///   - modelName: Model to use (default: claude-sonnet-4-20250514)
    ///   - timeout: Request timeout in seconds (default: 60)
    init(
        apiKey: String,
        baseURL: URL = ClaudeVLMProvider.defaultBaseURL,
        modelName: String = ClaudeVLMProvider.defaultModel,
        timeout: TimeInterval = 60
    ) {
        self.configuration = VLMProviderConfiguration(
            apiKey: apiKey,
            baseURL: baseURL,
            modelName: modelName
        )
        self.timeout = timeout
    }

    private func logDebug(_ message: String) {
        logger.debug("\(message, privacy: .public)")
    }

    private func logWarning(_ message: String) {
        logger.warning("\(message, privacy: .public)")
    }

    private func logError(_ message: String) {
        logger.error("\(message, privacy: .public)")
    }

    // MARK: - VLMProvider Protocol

    var isAvailable: Bool {
        get async {
            true
        }
    }

    /// Maximum number of continuation attempts when response is truncated
    private let maxContinuationAttempts = 3

    func analyze(image: CGImage) async throws -> ScreenAnalysisResult {
        guard let imageData = image.jpegData(quality: 0.85), !imageData.isEmpty else {
            throw VLMProviderError.imageEncodingFailed
        }

        let base64Image = imageData.base64EncodedString()
        let imageSize = CGSize(width: image.width, height: image.height)

        // Use multi-turn conversation with continuation support
        let vlmResponse = try await analyzeWithContinuation(
            base64Image: base64Image,
            imageSize: imageSize,
            maxAttempts: maxContinuationAttempts
        )

        return vlmResponse.toScreenAnalysisResult(imageSize: imageSize)
    }

    /// Performs analysis with automatic continuation on truncation
    private func analyzeWithContinuation(
        base64Image: String,
        imageSize: CGSize,
        maxAttempts: Int
    ) async throws -> VLMAnalysisResponse {
        var allSegments: [VLMTextSegment] = []
        var conversationHistory: [ClaudeMessage] = [
            ClaudeMessage(
                role: "user",
                content: [
                    .image(ClaudeImageContent(
                        source: ClaudeImageSource(
                            type: "base64",
                            mediaType: "image/jpeg",
                            data: base64Image
                        )
                    )),
                    .text(VLMPromptTemplate.userPrompt),
                ]
            ),
        ]

        for attempt in 0..<maxAttempts {
            let request = try buildRequest(
                messages: conversationHistory,
                isContinuation: attempt > 0
            )
            let responseData = try await executeRequest(request)

            let (content, isTruncated, stopReason) = try extractContentAndStatus(from: responseData)

            logDebug("Attempt \(attempt + 1)/\(maxAttempts): received \(content.count) chars, stop reason: \(stopReason ?? "unknown")")

            // Try to parse this response
            do {
                let response = try parseVLMContent(content)

                // For continuation requests, filter out duplicates from the beginning
                // LLM often repeats some segments when continuing
                let newSegments: [VLMTextSegment]
                if attempt > 0 {
                    newSegments = filterDuplicateSegments(
                        existing: allSegments,
                        new: response.segments
                    )
                } else {
                    newSegments = response.segments
                }

                allSegments.append(contentsOf: newSegments)
                logDebug("Parsed \(response.segments.count) segments, added \(newSegments.count) new (attempt \(attempt + 1))")

                if !isTruncated {
                    // Complete - return merged result with deduplication
                    let deduplicated = deduplicateSegments(allSegments)
                    logDebug("Complete response received, \(allSegments.count) -> \(deduplicated.count) segments after dedup")
                    return VLMAnalysisResponse(segments: deduplicated)
                }
            } catch {
                logWarning("Parse error on attempt \(attempt + 1) [\(String(describing: type(of: error)))]")

                // Try partial parsing for truncated response
                if isTruncated {
                    if let partial = try? parsePartialVLMContent(content) {
                        // Filter duplicates for partial parse too
                        let newSegments = filterDuplicateSegments(
                            existing: allSegments,
                            new: partial.segments
                        )
                        allSegments.append(contentsOf: newSegments)
                        logDebug("Partial parse recovered \(partial.segments.count) segments, added \(newSegments.count) new")
                    }
                }

                // If not truncated but parse failed, this is a real error
                if !isTruncated {
                    throw error
                }
            }

            // Response truncated, need to continue
            logDebug("Response truncated, requesting continuation")

            // Add assistant's partial response to conversation
            conversationHistory.append(ClaudeMessage(
                role: "assistant",
                content: [.text(content)]
            ))

            // Request continuation - ask for remaining segments only
            conversationHistory.append(ClaudeMessage(
                role: "user",
                content: [.text("Continue with the remaining segments only. Do not repeat any segments you've already provided.")]
            ))
        }

        // Final deduplication before returning
        let deduplicated = deduplicateSegments(allSegments)
        logWarning("Max continuation attempts reached, \(allSegments.count) -> \(deduplicated.count) segments after dedup")
        return VLMAnalysisResponse(segments: deduplicated)
    }

    /// Filters out segments from new array that already exist in existing array
    private func filterDuplicateSegments(
        existing: [VLMTextSegment],
        new: [VLMTextSegment]
    ) -> [VLMTextSegment] {
        VLMTextDeduplicator.filterDuplicates(existing: existing, new: new)
    }

    /// Removes duplicate segments from the final result
    private func deduplicateSegments(_ segments: [VLMTextSegment]) -> [VLMTextSegment] {
        VLMTextDeduplicator.deduplicate(segments) { length, count, threshold in
            // Log only safe statistics, not plaintext content
            logDebug("Detected overrepresented text: length=\(length), count=\(count), threshold=\(threshold)")
        }
    }

    /// Extracts content text and truncation status from Claude response
    private func extractContentAndStatus(from data: Data) throws -> (content: String, isTruncated: Bool, stopReason: String?) {
        if let errorResponse = try? JSONDecoder().decode(ClaudeErrorResponse.self, from: data),
           errorResponse.type == "error" {
            throw VLMProviderError.invalidResponse(errorResponse.error.message)
        }

        let decoder = JSONDecoder()
        let claudeResponse = try decoder.decode(ClaudeMessagesResponse.self, from: data)

        guard let contentBlocks = claudeResponse.content,
              let textBlock = contentBlocks.first(where: { $0.type == "text" }),
              let content = textBlock.text
        else {
            throw VLMProviderError.invalidResponse("No text content in response")
        }

        let isTruncated = claudeResponse.stopReason == "max_tokens"
        return (content, isTruncated, claudeResponse.stopReason)
    }

    /// Builds request with custom messages and continuation settings
    private func buildRequest(messages: [ClaudeMessage], isContinuation: Bool) throws -> URLRequest {
        let endpoint = configuration.baseURL.resolvingLocalhost.appendingPathComponent("v1/messages")

        var request = URLRequest(url: endpoint)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        let key = configuration.apiKey.trimmingCharacters(in: .whitespacesAndNewlines)
        if !key.isEmpty {
            request.setValue(key, forHTTPHeaderField: "x-api-key")
        }
        request.setValue(Self.apiVersion, forHTTPHeaderField: "anthropic-version")
        request.timeoutInterval = timeout

        // Use higher max_tokens for continuation requests to minimize truncation
        let maxTokens = isContinuation ? 16384 : 8192

        let requestBody = ClaudeMessagesRequest(
            model: configuration.modelName,
            maxTokens: maxTokens,
            system: VLMPromptTemplate.systemPrompt,
            messages: messages
        )

        let encoder = JSONEncoder()
        encoder.keyEncodingStrategy = .convertToSnakeCase
        request.httpBody = try encoder.encode(requestBody)

        return request
    }

    // MARK: - Private Methods

    /// Executes the HTTP request with timeout handling
    private func executeRequest(_ request: URLRequest) async throws -> Data {
        let (data, response): (Data, URLResponse)

        do {
            (data, response) = try await URLSession.shared.data(for: request)
        } catch let error as URLError {
            switch error.code {
            case .timedOut:
                throw VLMProviderError.networkError("Request timed out")
            case .notConnectedToInternet, .networkConnectionLost:
                throw VLMProviderError.networkError("No internet connection")
            default:
                throw VLMProviderError.networkError(error.localizedDescription)
            }
        } catch {
            throw VLMProviderError.networkError(error.localizedDescription)
        }

        guard let httpResponse = response as? HTTPURLResponse else {
            throw VLMProviderError.invalidResponse("Invalid HTTP response")
        }

        try handleHTTPStatus(httpResponse, data: data)

        return data
    }

    /// Handles HTTP status codes and throws appropriate errors
    private func handleHTTPStatus(_ response: HTTPURLResponse, data: Data) throws {
        switch response.statusCode {
        case 200 ... 299:
            return

        case 401:
            throw VLMProviderError.authenticationFailed

        case 429:
            let retryAfter = parseRetryAfter(from: response, data: data)
            let errorMessage = parseErrorMessage(from: data)
            throw VLMProviderError.rateLimited(retryAfter: retryAfter, message: errorMessage)

        case 404:
            throw VLMProviderError.modelUnavailable(configuration.modelName)

        case 400:
            let message = parseErrorMessage(from: data) ?? "Bad request"
            throw VLMProviderError.invalidConfiguration(message)

        case 500 ... 599:
            let message = parseErrorMessage(from: data) ?? "Server error"
            throw VLMProviderError.networkError("Server error (\(response.statusCode)): \(message)")

        default:
            let message = parseErrorMessage(from: data) ?? "Unknown error"
            throw VLMProviderError.invalidResponse("HTTP \(response.statusCode): \(message)")
        }
    }

    /// Parses the retry-after value from rate limit response
    private func parseRetryAfter(from response: HTTPURLResponse, data: Data) -> TimeInterval? {
        if let headerValue = response.value(forHTTPHeaderField: "Retry-After"),
           let seconds = Double(headerValue)
        {
            return seconds
        }

        if let errorResponse = try? JSONDecoder().decode(ClaudeErrorResponse.self, from: data),
           let retryAfter = errorResponse.error.retryAfter
        {
            return retryAfter
        }

        return nil
    }

    /// Parses error message from Claude error response
    private func parseErrorMessage(from data: Data) -> String? {
        guard let errorResponse = try? JSONDecoder().decode(ClaudeErrorResponse.self, from: data) else {
            return nil
        }
        return errorResponse.error.message
    }

    /// Parses the VLM JSON content from assistant message
    private func parseVLMContent(_ content: String) throws -> VLMAnalysisResponse {
        let cleanedContent = extractJSON(from: content)

        guard let jsonData = cleanedContent.data(using: .utf8) else {
            throw VLMProviderError.parsingFailed("Failed to convert content to data")
        }

        do {
            let response = try JSONDecoder().decode(VLMAnalysisResponse.self, from: jsonData)
            return response
        } catch {
            throw VLMProviderError.parsingFailed(
                "Failed to parse VLM response JSON: \(error.localizedDescription). Content: \(cleanedContent.prefix(200))..."
            )
        }
    }

    /// Attempts to parse partial/truncated VLM content by extracting valid JSON segments
    private func parsePartialVLMContent(_ content: String) throws -> VLMAnalysisResponse {
        let cleanedContent = extractJSON(from: content)

        // Try to find the last complete textBlock object
        // Look for the last complete "}]}" pattern which ends a text block
            if let lastCompleteBlockEnd = cleanedContent.range(of: "}", options: .backwards) {
                let truncatedContent = String(cleanedContent[..<lastCompleteBlockEnd.upperBound])

                // Try to complete the JSON structure
                var completedJSON = truncatedContent
            if !truncatedContent.hasSuffix("]") {
                completedJSON += "]"
            }
            if !completedJSON.hasSuffix("}") {
                completedJSON += "}"
            }

            guard let jsonData = completedJSON.data(using: .utf8) else {
                throw VLMProviderError.parsingFailed("Failed to convert partial content to data")
            }

            do {
                let response = try JSONDecoder().decode(VLMAnalysisResponse.self, from: jsonData)
                logDebug("Successfully parsed partial response with \(response.segments.count) segments")
                return response
            } catch {
                // If that didn't work, return empty result with warning
                logWarning("Partial parse failed, returning empty result")
                return VLMAnalysisResponse(segments: [])
            }
        }

        // Last resort: return empty result
        return VLMAnalysisResponse(segments: [])
    }

    /// Extracts JSON from potentially markdown-wrapped content
    private func extractJSON(from content: String) -> String {
        VLMJSONExtractor.extractJSON(from: content)
    }
}

// MARK: - Claude API Request/Response Models

/// Claude Messages API request structure
private struct ClaudeMessagesRequest: Encodable, Sendable {
    let model: String
    let maxTokens: Int
    let system: String
    let messages: [ClaudeMessage]

    enum CodingKeys: String, CodingKey {
        case model
        case maxTokens = "max_tokens"
        case system
        case messages
    }
}

/// Claude message with support for multimodal content
private struct ClaudeMessage: Encodable, Sendable {
    let role: String
    let content: [ClaudeContentBlock]
}

/// Content block that can be text or image
private enum ClaudeContentBlock: Encodable, Sendable {
    case text(String)
    case image(ClaudeImageContent)

    func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        switch self {
        case .text(let text):
            try container.encode(ClaudeTextBlock(type: "text", text: text))
        case .image(let imageContent):
            try container.encode(imageContent)
        }
    }
}

private struct ClaudeTextBlock: Encodable, Sendable {
    let type: String
    let text: String
}

/// Image content structure for Claude vision requests
private struct ClaudeImageContent: Encodable, Sendable {
    let type: String = "image"
    let source: ClaudeImageSource
}

/// Image source with base64 data
private struct ClaudeImageSource: Encodable, Sendable {
    let type: String
    let mediaType: String
    let data: String

    enum CodingKeys: String, CodingKey {
        case type
        case mediaType = "media_type"
        case data
    }
}

/// Claude Messages API response structure
private struct ClaudeMessagesResponse: Decodable, Sendable {
    let id: String?
    let type: String?
    let role: String?
    let content: [ClaudeResponseContentBlock]?
    let model: String?
    let stopReason: String?
    let usage: ClaudeUsage?

    enum CodingKeys: String, CodingKey {
        case id, type, role, content, model
        case stopReason = "stop_reason"
        case usage
    }
}

private struct ClaudeResponseContentBlock: Decodable, Sendable {
    let type: String
    let text: String?
}

private struct ClaudeUsage: Decodable, Sendable {
    let inputTokens: Int
    let outputTokens: Int

    enum CodingKeys: String, CodingKey {
        case inputTokens = "input_tokens"
        case outputTokens = "output_tokens"
    }
}

/// Claude error response structure
private struct ClaudeErrorResponse: Decodable, Sendable {
    let type: String
    let error: ClaudeError
}

private struct ClaudeError: Decodable, Sendable {
    let type: String
    let message: String
    let retryAfter: TimeInterval?

    enum CodingKeys: String, CodingKey {
        case type, message
        case retryAfter = "retry_after"
    }
}
