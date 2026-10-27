//
//  OpenAIVLMProvider.swift
//  TransFrame
//
//  Created for US-005: OpenAI Vision Provider
//

import CoreGraphics
import Foundation
import os

// MARK: - OpenAI VLM Provider

/// VLM provider implementation for OpenAI GPT-4V/GPT-4o vision models
struct OpenAIVLMProvider: VLMProvider, Sendable {
    // MARK: - Properties

    let id: String = "openai"
    let name: String = "OpenAI Vision"
    let configuration: VLMProviderConfiguration

    /// Default OpenAI API base URL
    static let defaultBaseURL = URL(string: "https://api.openai.com/v1")!

    /// Default model for vision tasks
    static let defaultModel = "gpt-4o"

    /// Request timeout in seconds
    private let timeout: TimeInterval

    let apiMode: OpenAIAPIMode
    private let logger = Logger.translation
    /// Logger for the stateless response parser, usable without an instance.
    private static let parserLogger = Logger.translation

    // MARK: - Initialization

    /// Initialize with full configuration
    /// - Parameters:
    ///   - configuration: VLM provider configuration
    ///   - apiMode: OpenAI API protocol mode (.chat or .response)
    ///   - timeout: Request timeout in seconds (default: 60)
    init(configuration: VLMProviderConfiguration, apiMode: OpenAIAPIMode = .chat, timeout: TimeInterval = 60) {
        self.configuration = configuration
        self.apiMode = apiMode
        self.timeout = timeout
    }

    private func logDebug(_ message: String) {
        logger.debug("\(message, privacy: .public)")
    }

    private func logInfo(_ message: String) {
        logger.info("\(message, privacy: .public)")
    }

    private func logWarning(_ message: String) {
        logger.warning("\(message, privacy: .public)")
    }

    private func logError(_ message: String) {
        logger.error("\(message, privacy: .public)")
    }

    /// Convenience initializer with individual parameters
    /// - Parameters:
    ///   - apiKey: OpenAI API key
    ///   - baseURL: API base URL (default: OpenAI's official endpoint)
    ///   - modelName: Model to use (default: gpt-4o)
    ///   - apiMode: OpenAI API protocol mode (default: .chat)
    ///   - timeout: Request timeout in seconds (default: 60)
    init(
        apiKey: String,
        baseURL: URL = OpenAIVLMProvider.defaultBaseURL,
        modelName: String = OpenAIVLMProvider.defaultModel,
        apiMode: OpenAIAPIMode = .chat,
        timeout: TimeInterval = 60
    ) {
        self.configuration = VLMProviderConfiguration(
            apiKey: apiKey,
            baseURL: baseURL,
            modelName: modelName
        )
        self.apiMode = apiMode
        self.timeout = timeout
    }

    // MARK: - VLMProvider Protocol

    var isAvailable: Bool {
        get async { true }
    }

    /// Maximum number of continuation attempts when response is truncated
    private let maxContinuationAttempts = 3

    func analyze(image: CGImage) async throws -> ScreenAnalysisResult {
        try await analyze(image: image, prompt: .ocr)
    }

    func analyze(image: CGImage, prompt: VLMPromptPair) async throws -> ScreenAnalysisResult {
        let encoded = try AnalysisImageEncoder.jpeg(image)
        let imageSize = CGSize(width: image.width, height: image.height)

        logDebug("Image size: \(image.width)x\(image.height), \(encoded.mimeType) bytes: \(encoded.data.count), base64 chars: \(encoded.base64String.count)")

        // Use multi-turn conversation with continuation support
        let vlmResponse = try await analyzeWithContinuation(
            base64Image: encoded.base64String,
            imageSize: imageSize,
            maxAttempts: maxContinuationAttempts,
            prompts: prompt
        )

        return vlmResponse.toScreenAnalysisResult(imageSize: imageSize)
    }

    /// Detects if the provider is using a local endpoint (for lower max_tokens)
    private var isLocalEndpoint: Bool {
        let host = configuration.baseURL.host ?? ""
        // IPv6 loopback
        if host == "::1" { return true }
        // IPv4 loopback
        if host == "localhost" || host == "127.0.0.1" { return true }
        // Private ranges: 10.x.x.x
        if host.hasPrefix("10.") { return true }
        // Private ranges: 192.168.x.x
        if host.hasPrefix("192.168.") { return true }
        // Private ranges: 172.16.x.x - 172.31.x.x
        if host.hasPrefix("172.") {
            let parts = host.split(separator: ".")
            if parts.count >= 2, let second = Int(parts[1]), second >= 16, second <= 31 {
                return true
            }
        }
        return false
    }

    /// Performs analysis with automatic continuation on truncation
    private func analyzeWithContinuation(
        base64Image: String,
        imageSize: CGSize,
        maxAttempts: Int,
        prompts: VLMPromptPair = .ocr
    ) async throws -> VLMAnalysisResponse {
        var allSegments: [VLMTextSegment] = []
        var structuredOutputEnabled = true

        var conversationMessages: [OpenAIChatMessage] = [
            OpenAIChatMessage(
                role: "system",
                content: .text(prompts.system)
            ),
            OpenAIChatMessage(
                role: "user",
                content: .vision([
                    .text(prompts.user),
                    .imageURL(OpenAIImageURL(
                        url: "data:image/jpeg;base64,\(base64Image)"
                    )),
                ])
            ),
        ]

        for attempt in 0..<maxAttempts {
            let isContinuation = attempt > 0
            var responseData: Data
            do {
                let request = try buildRequest(
                    messages: conversationMessages,
                    isContinuation: isContinuation,
                    includeResponseFormat: structuredOutputEnabled
                )
                responseData = try await executeRequest(request)
            } catch VLMProviderError.invalidConfiguration where structuredOutputEnabled {
                // Some OpenAI-compatible endpoints reject response_format
                // (400). Retry the same attempt once without it.
                logWarning("Endpoint rejected response_format, retrying without structured output")
                structuredOutputEnabled = false
                let request = try buildRequest(
                    messages: conversationMessages,
                    isContinuation: isContinuation,
                    includeResponseFormat: false
                )
                responseData = try await executeRequest(request)
            }

            let (content, isTruncated, finishReason) = try Self.extractContentAndStatus(from: responseData)

            logDebug("Attempt \(attempt + 1)/\(maxAttempts): received \(content.count) chars, finish reason: \(finishReason ?? "unknown")")

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
            conversationMessages.append(OpenAIChatMessage(
                role: "assistant",
                content: .text(content)
            ))

            // Request continuation - ask for remaining segments only
            conversationMessages.append(OpenAIChatMessage(
                role: "user",
                content: .text("Continue with the remaining segments only. Do not repeat any segments you've already provided.")
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

    /// Extracts content text and truncation status from an OpenAI response.
    /// Stateless — declared static so connection probes can validate a 200
    /// body without constructing a fully configured provider instance.
    static func extractContentAndStatus(from data: Data) throws -> (content: String, isTruncated: Bool, finishReason: String?) {
        Self.parserLogger.debug("Received raw response payload: \(data.count, privacy: .public)")

        // Check for error response first
        if let errorResponse = try? JSONDecoder().decode(OpenAIErrorResponse.self, from: data),
           !errorResponse.error.message.isEmpty {
            throw VLMProviderError.invalidResponse(errorResponse.error.message)
        }

        // Responses API 的 reasoning 可以排在 message 之前，不能只读 output[0]。
        if let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
           json["object"] as? String == "response" || json["output"] is [[String: Any]] || json["output_text"] is String {
            let status = json["status"] as? String
            let details = json["incomplete_details"] as? [String: Any]
            let reason = details?["reason"] as? String
            // JSON null 会解码成 NSNull；字段存在不代表生成被截断。
            let isTruncated = status == "incomplete" && (reason == nil || reason == "max_output_tokens")
            if let status, ["failed", "cancelled", "queued", "in_progress"].contains(status) {
                throw VLMProviderError.invalidResponse("Responses API status: \(status)")
            }
            if status == "incomplete", !isTruncated {
                throw VLMProviderError.invalidResponse("Responses API incomplete: \(reason ?? "unknown")")
            }
            if let outputText = json["output_text"] as? String,
               !outputText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                return (outputText, isTruncated, isTruncated ? "length" : nil)
            }
            let output = json["output"] as? [[String: Any]] ?? []
            let texts = output.filter { item in
                item["type"] as? String == "message" || item["type"] == nil
            }.flatMap { item in
                (item["content"] as? [[String: Any]] ?? []).compactMap { block -> String? in
                    let type = block["type"] as? String
                    guard type == nil || type == "output_text" || type == "text",
                          let text = block["text"] as? String,
                          !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return nil }
                    return text
                }
            }
            guard !texts.isEmpty else {
                let types = output.compactMap { $0["type"] as? String }.joined(separator: ", ")
                throw VLMProviderError.invalidResponse(
                    "Responses API returned no message text (status: \(status ?? "unknown"), output types: \(types))"
                )
            }
            return (texts.joined(separator: "\n"), isTruncated, isTruncated ? "length" : nil)
        }

        // Try to parse as OpenAI response
        let decoder = JSONDecoder()

        let openAIResponse: OpenAIChatResponse
        do {
            openAIResponse = try decoder.decode(OpenAIChatResponse.self, from: data)
        } catch {
            throw VLMProviderError.parsingFailed(
                "Failed to decode OpenAI response: \(error.localizedDescription). Response length: \(data.count) bytes"
            )
        }

        guard let choices = openAIResponse.choices, !choices.isEmpty else {
            throw VLMProviderError.invalidResponse("No choices in response")
        }

        let choice = choices[0]

        guard let message = choice.message else {
            throw VLMProviderError.invalidResponse("No message in choice")
        }

        var content = message.content ?? ""

        // Fallback to reasoningContent or reasoning if content is empty
        if content.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            if let reasoning = message.reasoningContent, !reasoning.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                Self.parserLogger.info("Main content is empty, falling back to reasoning_content")
                content = reasoning
            } else if let reasoningObj = message.reasoning, !reasoningObj.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                Self.parserLogger.info("Main content is empty, falling back to reasoning")
                content = reasoningObj
            }
        }

        let isTruncated = choice.finishReason == "length"
        return (content, isTruncated, choice.finishReason)
    }

    /// Attempts to extract content field manually when JSON decoder fails
    func extractContentManually(from json: String) -> String? {
        let patterns = ["\"content\":\"", "\"content\": \""]
        let reasoningPatterns = [
            "\"reasoning_content\":\"",
            "\"reasoning_content\": \"",
            "\"reasoning\":\"",
            "\"reasoning\": \""
        ]

        var extractedContent: String? = nil

        // Try extracting content first
        for pattern in patterns {
            if let content = extractField(from: json, pattern: pattern) {
                extractedContent = content
                break
            }
        }

        // If content is empty or not found, try extracting reasoning fields as fallback
        if extractedContent == nil || extractedContent?.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty == true {
            for pattern in reasoningPatterns {
                if let reasoning = extractField(from: json, pattern: pattern) {
                    logInfo("Manually extracted reasoning field as fallback")
                    extractedContent = reasoning
                    break
                }
            }
        }

        if let content = extractedContent {
            logDebug("extractContentManually found content of \(content.count) chars")
            return content
        }

        logDebug("extractContentManually found no content pattern")
        return nil
    }

    private func extractField(from json: String, pattern: String) -> String? {
        guard let range = json.range(of: pattern) else { return nil }
        let start = range.upperBound

        var end = start
        var escaped = false
        var depth = 0

        for char in json[start...] {
            if escaped {
                escaped = false
                end = json.index(after: end)
            } else if char == "\\" {
                escaped = true
                end = json.index(after: end)
            } else if char == "{" || char == "[" {
                depth += 1
                end = json.index(after: end)
            } else if char == "}" || char == "]" {
                depth -= 1
                end = json.index(after: end)
                if depth < 0 { break }
            } else if char == "\"" && depth == 0 {
                break
            } else {
                end = json.index(after: end)
            }
        }

        let content = String(json[start..<end])
        return content
            .replacingOccurrences(of: "\\\"", with: "\"")
            .replacingOccurrences(of: "\\\\", with: "\\")
            .replacingOccurrences(of: "\\n", with: "\n")
    }

    // MARK: - Private Methods

    /// Placeholder replacing the base64 image in continuation rounds, so the
    /// same capture is not uploaded again for every truncation retry.
    private static let continuationImagePlaceholder =
        "(image omitted: the same image as in the first message)"

    /// Builds the URLRequest for OpenAI Chat Completions or Responses API with custom messages
    private func buildRequest(
        messages: [OpenAIChatMessage],
        isContinuation: Bool = false,
        includeResponseFormat: Bool = true
    ) throws -> URLRequest {
        let endpoint: URL
        if apiMode == .response {
            endpoint = configuration.baseURL.resolvingLocalhost.appendingPathComponent("responses")
        } else {
            endpoint = configuration.baseURL.resolvingLocalhost.appendingPathComponent("chat/completions")
        }

        var request = URLRequest(url: endpoint)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        let key = configuration.apiKey.trimmingCharacters(in: .whitespacesAndNewlines)
        if !key.isEmpty {
            request.setValue("Bearer \(key)", forHTTPHeaderField: "Authorization")
        }
        request.timeoutInterval = timeout

        // Continuation rounds keep the conversation shape but drop only the
        // base64 image part — the text instruction stays so the model still
        // knows the task (mirrors ClaudeVLMProvider's per-block replacement).
        let effectiveMessages: [OpenAIChatMessage]
        if isContinuation {
            effectiveMessages = messages.map { message in
                guard case .vision(let parts) = message.content else { return message }
                var kept: [OpenAIChatMessage.VisionContent] = parts.compactMap { part in
                    if case .text(let text) = part { return .text(text) }
                    return nil
                }
                if kept.isEmpty {
                    kept = [.text(Self.continuationImagePlaceholder)]
                }
                return OpenAIChatMessage(role: message.role, content: .vision(kept))
            }
        } else {
            effectiveMessages = messages
        }

        // Responses API
        if apiMode == .response {
            let inputItems: [[String: Any]] = effectiveMessages.map { msg in
                switch msg.content {
                case .text(let t):
                    return ["role": msg.role, "content": t]
                case .vision(let parts):
                    let contentArray: [[String: Any]] = parts.map { part in
                        switch part {
                        case .text(let text):
                            return ["type": "input_text", "text": text]
                        case .imageURL(let img):
                            return ["type": "input_image", "image_url": img.url]
                        }
                    }
                    return ["role": msg.role, "content": contentArray]
                }
            }
            var requestBody: [String: Any] = [
                "model": configuration.modelName,
                "input": inputItems,
                "temperature": 0.1
            ]
            if includeResponseFormat {
                requestBody["text"] = ["format": ["type": "json_object"]]
            }
            request.httpBody = try JSONSerialization.data(withJSONObject: requestBody)
            return request
        }

        // Local models are slower; 4096 covers ~60-80 segments before
        // continuation kicks in, which is cheaper than a second full request.
        let maxTokens: Int
        if isLocalEndpoint {
            maxTokens = 4096
        } else {
            maxTokens = isContinuation ? 16384 : 8192
        }

        let requestBody = OpenAIChatRequest(
            model: configuration.modelName,
            messages: effectiveMessages,
            maxTokens: maxTokens,
            temperature: 0.1,
            responseFormat: includeResponseFormat ? .jsonObject : nil
        )

        let encoder = JSONEncoder()
        encoder.keyEncodingStrategy = .convertToSnakeCase
        request.httpBody = try encoder.encode(requestBody)

        return request
    }

    /// Executes the HTTP request with timeout handling
    private func executeRequest(_ request: URLRequest) async throws -> Data {
        let (data, response): (Data, URLResponse)

        logDebug("Sending request to: \(request.url?.absoluteString ?? "unknown")")
        if let body = request.httpBody {
            logDebug("Request body size: \(body.count) bytes")
        }

        do {
            (data, response) = try await URLSession.shared.data(for: request)
        } catch let error as URLError {
            logWarning("Network error: \(error.localizedDescription)")
            switch error.code {
            case .timedOut:
                throw VLMProviderError.networkError("Request timed out")
            case .notConnectedToInternet, .networkConnectionLost:
                throw VLMProviderError.networkError("No internet connection")
            default:
                throw VLMProviderError.networkError(error.localizedDescription)
            }
        } catch {
            logWarning("Unknown request error: \(error.localizedDescription)")
            throw VLMProviderError.networkError(error.localizedDescription)
        }

        guard let httpResponse = response as? HTTPURLResponse else {
            throw VLMProviderError.invalidResponse("Invalid HTTP response")
        }

        logDebug("HTTP status: \(httpResponse.statusCode), payload size: \(data.count) bytes")

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
            logWarning("429 rate limited from OpenAI endpoint")
            let retryAfter = parseRetryAfter(from: response, data: data)
            let errorMessage = parseErrorMessage(from: data)
            throw VLMProviderError.rateLimited(retryAfter: retryAfter, message: errorMessage)

        case 404:
            throw VLMProviderError.modelUnavailable(configuration.modelName)

        case 400:
            let message = parseErrorMessage(from: data) ?? "Bad request"
            logWarning("400 bad request from OpenAI endpoint")
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

        if let errorResponse = try? JSONDecoder().decode(OpenAIErrorResponse.self, from: data),
           let retryAfter = errorResponse.error.retryAfter
        {
            return retryAfter
        }

        return nil
    }

    /// Parses error message from OpenAI error response
    private func parseErrorMessage(from data: Data) -> String? {
        guard let errorResponse = try? JSONDecoder().decode(OpenAIErrorResponse.self, from: data) else {
            return nil
        }
        return errorResponse.error.message
    }

    /// Parses the VLM JSON content from assistant message
    func parseVLMContent(_ content: String, wasTruncated: Bool = false) throws -> VLMAnalysisResponse {
        guard !content.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            throw VLMProviderError.parsingFailed(
                "Received empty response from model. This may happen if the model is not vision-capable, does not support the request format, or has content filters active."
            )
        }

        var cleanedContent = extractJSON(from: content)

        // If response was truncated, try to repair the JSON by closing open brackets
        if wasTruncated {
            logDebug("Attempting to repair truncated JSON")
            cleanedContent = attemptToRepairJSON(cleanedContent)
        }

        guard !cleanedContent.isEmpty else {
            let escapedRaw = content.replacingOccurrences(of: "\n", with: " ")
            throw VLMProviderError.parsingFailed(
                "Cleaned content is empty. Raw content received: '\(escapedRaw.prefix(200))'"
            )
        }

        guard let jsonData = cleanedContent.data(using: .utf8) else {
            throw VLMProviderError.parsingFailed("Failed to convert content to data")
        }

        do {
            let response = try JSONDecoder().decode(VLMAnalysisResponse.self, from: jsonData)
            return response
        } catch {
            // JSON parsing failed - try to handle plain text response from local models
            logDebug("JSON parsing failed, attempting plain text fallback")
            if let plainTextResponse = parsePlainTextResponse(content) {
                logDebug("Successfully parsed plain text response with \(plainTextResponse.segments.count) segments")
                return plainTextResponse
            }

            if wasTruncated {
                throw VLMProviderError.invalidResponse("Response was truncated due to token limit. Try selecting a smaller area or using a model with larger context window.")
            }
            
            let escapedCleaned = cleanedContent.replacingOccurrences(of: "\n", with: " ")
            let escapedRaw = content.replacingOccurrences(of: "\n", with: " ")
            throw VLMProviderError.parsingFailed(
                "Failed to parse VLM response JSON: \(error.localizedDescription). Cleaned content (first 200 chars): '\(escapedCleaned.prefix(200))'. Raw content (first 200 chars): '\(escapedRaw.prefix(200))'"
            )
        }
    }

    /// Parses plain text response (one text per line) from local models
    private func parsePlainTextResponse(_ content: String) -> VLMAnalysisResponse? {
        let rawLines = content
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .components(separatedBy: .newlines)
            .map { $0.trimmingCharacters(in: .whitespaces) }

        // Filter out structural/noise lines
        let lines = rawLines.filter { line in
            guard !line.isEmpty else { return false }
            // Skip code fence markers
            if line == "```" || line == "`" || line.hasPrefix("```") { return false }
            // Skip lone JSON structural tokens
            if ["{", "}", "[", "]", "{ }", "[ ]"].contains(line) { return false }
            // Skip JSON field names (quoted identifiers)
            let metadataPatterns = ["\"segments\"", "\"boundingBox\"", "\"text\"", "\"confidence\"", "\"x\"", "\"y\"", "\"width\"", "\"height\""]
            if metadataPatterns.contains(where: { line.contains($0) }) { return false }
            return true
        }

        guard !lines.isEmpty else { return nil }

        // Convert each line to a segment with placeholder bounding box
        let segments = lines.enumerated().map { (index, text) in
            VLMTextSegment(
                text: text,
                boundingBox: VLMBoundingBox(
                    x: 0.0,
                    y: CGFloat(index) / CGFloat(max(lines.count, 1)),
                    width: 1.0,
                    height: 1.0 / CGFloat(max(lines.count, 1))
                ),
                confidence: nil  // Unknown confidence for plain text fallback
            )
        }

        return VLMAnalysisResponse(segments: segments)
    }

    /// Attempts to parse partial/truncated VLM content by extracting valid JSON segments
    private func parsePartialVLMContent(_ content: String) throws -> VLMAnalysisResponse {
        let cleanedContent = extractJSON(from: content)

        // Try to find the last complete segment object
        // Look for the last complete "}" that closes a segment
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

    /// Attempts to repair truncated JSON by closing open structures
    private func attemptToRepairJSON(_ json: String) -> String {
        var repaired = json

        // Count unclosed brackets
        let openBraces = repaired.filter { $0 == "{" }.count - repaired.filter { $0 == "}" }.count
        let openBrackets = repaired.filter { $0 == "[" }.count - repaired.filter { $0 == "]" }.count

        // Close any open strings (if odd number of unescaped quotes)
        let quoteCount = repaired.filter { $0 == "\"" }.count
        if quoteCount % 2 != 0 {
            repaired += "\""
        }

        // Close objects and arrays
        for _ in 0..<openBraces {
            repaired += "}"
        }
        for _ in 0..<openBrackets {
            repaired += "]"
        }

        return repaired
    }

    /// Extracts JSON from potentially markdown-wrapped content
    private func extractJSON(from content: String) -> String {
        VLMJSONExtractor.extractJSON(from: content)
    }
}

// MARK: - OpenAI API Request/Response Models

/// OpenAI Chat Completion request structure
private struct OpenAIChatRequest: Encodable, Sendable {
    let model: String
    let messages: [OpenAIChatMessage]
    let maxTokens: Int
    let temperature: Double
    let responseFormat: OpenAIResponseFormat?
    let stream: Bool = false

    enum CodingKeys: String, CodingKey {
        case model, messages, temperature, stream
        case maxTokens = "max_tokens"
        case responseFormat = "response_format"
    }
}

/// OpenAI `response_format` payload forcing JSON output, so providers that
/// support it never emit prose that would fall through to the plain-text
/// fallback (which fabricates bounding boxes).
private struct OpenAIResponseFormat: Encodable, Sendable {
    static let jsonObject = OpenAIResponseFormat(type: "json_object")

    let type: String
}

/// Chat message with support for vision content
private struct OpenAIChatMessage: Encodable, Sendable {
    let role: String
    let content: MessageContent

    enum MessageContent: Sendable {
        case text(String)
        case vision([VisionContent])
    }

    enum VisionContent: Sendable {
        case text(String)
        case imageURL(OpenAIImageURL)
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(role, forKey: .role)

        switch content {
        case .text(let text):
            try container.encode(text, forKey: .content)
        case .vision(let contents):
            var contentArray = container.nestedUnkeyedContainer(forKey: .content)
            for item in contents {
                switch item {
                case .text(let text):
                    try contentArray.encode(["type": "text", "text": text])
                case .imageURL(let imageURL):
                    var itemContainer = contentArray.nestedContainer(keyedBy: VisionCodingKeys.self)
                    try itemContainer.encode("image_url", forKey: .type)
                    try itemContainer.encode(imageURL, forKey: .imageUrl)
                }
            }
        }
    }

    enum CodingKeys: String, CodingKey {
        case role, content
    }

    enum VisionCodingKeys: String, CodingKey {
        case type
        case imageUrl = "image_url"
    }
}

/// Image URL structure for vision requests
private struct OpenAIImageURL: Encodable, Sendable {
    let url: String
    let detail: String

    init(url: String, detail: String = "high") {
        self.url = url
        self.detail = detail
    }
}

/// OpenAI Chat Completion response structure
private struct OpenAIChatResponse: Decodable, Sendable {
    let id: String?
    let choices: [OpenAIChatChoice]?
    let usage: OpenAIUsage?
}

private struct OpenAIChatChoice: Decodable, Sendable {
    let index: Int?
    let message: OpenAIResponseMessage?
    let finishReason: String?

    enum CodingKeys: String, CodingKey {
        case index, message
        case finishReason = "finish_reason"
    }
}

private struct OpenAIResponseMessage: Decodable, Sendable {
    let role: String?
    let content: String?
    let reasoningContent: String?
    let reasoning: String?

    enum CodingKeys: String, CodingKey {
        case role, content
        case reasoningContent = "reasoning_content"
        case reasoning
    }
}

private struct OpenAIUsage: Decodable, Sendable {
    let promptTokens: Int
    let completionTokens: Int
    let totalTokens: Int

    enum CodingKeys: String, CodingKey {
        case promptTokens = "prompt_tokens"
        case completionTokens = "completion_tokens"
        case totalTokens = "total_tokens"
    }
}

/// OpenAI error response structure
private struct OpenAIErrorResponse: Decodable, Sendable {
    let error: OpenAIError
}

private struct OpenAIError: Decodable, Sendable {
    let message: String
    let type: String?
    let code: String?
    let retryAfter: TimeInterval?

    enum CodingKeys: String, CodingKey {
        case message, type, code
        case retryAfter = "retry_after"
    }
}
