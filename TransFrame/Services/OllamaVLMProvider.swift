//
//  OllamaVLMProvider.swift
//  TransFrame
//
//  Created for US-007: Ollama Vision Provider
//

import CoreGraphics
import Foundation

// MARK: - Ollama VLM Provider

/// VLM provider implementation for local Ollama vision models (llava, qwen-vl, etc.)
struct OllamaVLMProvider: VLMProvider, Sendable {
    // MARK: - Properties

    let id: String = "ollama"
    let name: String = "Local Model"
    let configuration: VLMProviderConfiguration

    /// Default Ollama API base URL (local server)
    static let defaultBaseURL = URL(string: "http://localhost:11434")!

    /// Default model for vision tasks
    static let defaultModel = "llava"

    /// Request timeout in seconds
    private let timeout: TimeInterval

    // MARK: - Initialization

    /// Initialize with full configuration
    /// - Parameters:
    ///   - configuration: VLM provider configuration
    ///   - timeout: Request timeout in seconds (default: 120 for local models)
    init(configuration: VLMProviderConfiguration, timeout: TimeInterval = 120) {
        self.configuration = configuration
        self.timeout = timeout
    }

    /// Convenience initializer with individual parameters
    /// - Parameters:
    ///   - baseURL: API base URL (default: localhost:11434)
    ///   - modelName: Model to use (default: llava)
    ///   - timeout: Request timeout in seconds (default: 120)
    init(
        baseURL: URL = OllamaVLMProvider.defaultBaseURL,
        modelName: String = OllamaVLMProvider.defaultModel,
        timeout: TimeInterval = 120
    ) {
        // Ollama doesn't require API key, but VLMProviderConfiguration requires one
        self.configuration = VLMProviderConfiguration(
            apiKey: "",
            baseURL: baseURL,
            modelName: modelName
        )
        self.timeout = timeout
    }

    // MARK: - VLMProvider Protocol

    var isAvailable: Bool {
        get async {
            await checkServerAvailability()
        }
    }

    func analyze(image: CGImage) async throws -> ScreenAnalysisResult {
        try await analyze(image: image, prompt: .ocr)
    }

    func analyze(image: CGImage, prompt: VLMPromptPair) async throws -> ScreenAnalysisResult {
        let encoded = try AnalysisImageEncoder.jpeg(image)
        let imageSize = CGSize(width: image.width, height: image.height)

        var responseData: Data
        do {
            let request = try buildRequest(base64Image: encoded.base64String, prompts: prompt, includeResponseFormat: true)
            responseData = try await executeRequest(request)
        } catch VLMProviderError.invalidConfiguration {
            // Some OpenAI-compatible endpoints reject response_format with a
            // 400; retry once without it (mirrors OpenAIVLMProvider).
            let request = try buildRequest(base64Image: encoded.base64String, prompts: prompt, includeResponseFormat: false)
            responseData = try await executeRequest(request)
        }

        let vlmResponse = try parseOllamaResponse(responseData)

        return vlmResponse.toScreenAnalysisResult(imageSize: imageSize)
    }

    // MARK: - Private Methods

    /// Checks if Ollama server is running and accessible
    private func checkServerAvailability() async -> Bool {
        let isChatCompletions = configuration.baseURL.path.contains("v1") || configuration.baseURL.path.contains("chat/completions")
        let endpoint: URL
        if isChatCompletions {
            var url = configuration.baseURL.resolvingLocalhost
            if url.path.hasSuffix("/chat/completions") {
                url = url.deletingLastPathComponent()
            }
            endpoint = url.appendingPathComponent("models")
        } else {
            endpoint = configuration.baseURL.resolvingLocalhost.appendingPathComponent("api/tags")
        }

        var request = URLRequest(url: endpoint)
        request.httpMethod = "GET"
        request.timeoutInterval = 5 // Short timeout for health check
        let key = configuration.apiKey.trimmingCharacters(in: .whitespacesAndNewlines)
        if !key.isEmpty {
            request.setValue("Bearer \(key)", forHTTPHeaderField: "Authorization")
        }

        do {
            let (_, response) = try await URLSession.shared.data(for: request)
            guard let httpResponse = response as? HTTPURLResponse else {
                return false
            }
            return httpResponse.statusCode == 200 || httpResponse.statusCode == 401
        } catch {
            if !isChatCompletions {
                let fallback = configuration.baseURL.resolvingLocalhost.appendingPathComponent("v1/models")
                var fbReq = URLRequest(url: fallback)
                fbReq.httpMethod = "GET"
                fbReq.timeoutInterval = 3
                if let (_, resp) = try? await URLSession.shared.data(for: fbReq),
                   let http = resp as? HTTPURLResponse {
                    return http.statusCode == 200 || http.statusCode == 401
                }
            }
            return false
        }
    }

    /// Builds the URLRequest for local server (Ollama /api/generate or OpenAI /chat/completions)
    private func buildRequest(base64Image: String, prompts: VLMPromptPair = .ocr, includeResponseFormat: Bool = true) throws -> URLRequest {
        let isChatCompletions = configuration.baseURL.path.contains("v1") || configuration.baseURL.path.contains("chat/completions")
        let endpoint: URL
        if isChatCompletions {
            let url = configuration.baseURL.resolvingLocalhost
            if url.path.hasSuffix("/chat/completions") {
                endpoint = url
            } else {
                endpoint = url.appendingPathComponent("chat/completions")
            }
        } else {
            endpoint = configuration.baseURL.resolvingLocalhost.appendingPathComponent("api/generate")
        }

        var request = URLRequest(url: endpoint)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        let key = configuration.apiKey.trimmingCharacters(in: .whitespacesAndNewlines)
        if !key.isEmpty {
            request.setValue("Bearer \(key)", forHTTPHeaderField: "Authorization")
        }
        request.timeoutInterval = timeout

        if isChatCompletions {
            var body: [String: Any] = [
                "model": configuration.modelName,
                "messages": [
                    [
                        "role": "user",
                        "content": [
                            ["type": "text", "text": "\(prompts.system)\n\n\(prompts.user)"],
                            ["type": "image_url", "image_url": ["url": "data:image/jpeg;base64,\(base64Image)"]]
                        ]
                    ]
                ],
                "temperature": 0.1
            ]
            if includeResponseFormat {
                // Ollama's OpenAI-compatible layer and most vLLM builds accept
                // this; incompatible servers hit the 400 fallback in analyze.
                body["response_format"] = ["type": "json_object"]
            }
            request.httpBody = try JSONSerialization.data(withJSONObject: body)
        } else {
            let prompt = """
                \(prompts.system)

                \(prompts.user)
                """

            let requestBody = OllamaGenerateRequest(
                model: configuration.modelName,
                prompt: prompt,
                images: [base64Image],
                format: "json",
                stream: false,
                options: OllamaOptions(
                    temperature: 0.1,
                    numPredict: 4096
                )
            )

            let encoder = JSONEncoder()
            encoder.keyEncodingStrategy = .convertToSnakeCase
            request.httpBody = try encoder.encode(requestBody)
        }

        return request
    }

    /// Executes the HTTP request with timeout handling
    private func executeRequest(_ request: URLRequest) async throws -> Data {
        let (data, response): (Data, URLResponse)

        do {
            (data, response) = try await URLSession.shared.data(for: request)
        } catch let error as URLError {
            switch error.code {
            case .timedOut:
                throw VLMProviderError.networkError("Request timed out")
            case .cannotConnectToHost, .cannotFindHost:
                throw VLMProviderError.networkError(
                    "Cannot connect to Ollama server at \(configuration.baseURL). Is Ollama running?"
                )
            case .notConnectedToInternet, .networkConnectionLost:
                throw VLMProviderError.networkError("No network connection")
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

        case 401, 403:
            throw VLMProviderError.authenticationFailed

        case 404:
            throw VLMProviderError.modelUnavailable(
                "\(configuration.modelName). Run 'ollama pull \(configuration.modelName)' to download it."
            )

        case 400:
            let message = parseErrorMessage(from: data) ?? "Bad request"
            throw VLMProviderError.invalidConfiguration(message)

        case 500 ... 599:
            let message = parseErrorMessage(from: data) ?? "Server error"
            throw VLMProviderError.networkError("Ollama server error (\(response.statusCode)): \(message)")

        default:
            let message = parseErrorMessage(from: data) ?? "Unknown error"
            throw VLMProviderError.invalidResponse("HTTP \(response.statusCode): \(message)")
        }
    }

    /// Parses error message from Ollama error response
    private func parseErrorMessage(from data: Data) -> String? {
        guard let errorResponse = try? JSONDecoder().decode(OllamaErrorResponse.self, from: data) else {
            return nil
        }
        return errorResponse.error
    }

    /// Parses local server response (Ollama or OpenAI-compatible) and extracts VLM analysis
    private func parseOllamaResponse(_ data: Data) throws -> VLMAnalysisResponse {
        // 1. Try decoding OpenAI-compatible format
        if let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
           let choices = json["choices"] as? [[String: Any]],
           let first = choices.first,
           let message = first["message"] as? [String: Any],
           let content = message["content"] as? String {
            return try parseVLMContent(content)
        }

        // 2. Try decoding standard Ollama format
        let decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase
        if let ollamaResponse = try? decoder.decode(OllamaGenerateResponse.self, from: data),
           !ollamaResponse.response.isEmpty {
            return try parseVLMContent(ollamaResponse.response)
        }

        // 3. Fallback: try raw string content
        if let raw = String(data: data, encoding: .utf8), !raw.isEmpty {
            return try parseVLMContent(raw)
        }

        throw VLMProviderError.parsingFailed("Failed to decode local model response")
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

    /// Extracts JSON from potentially markdown-wrapped content
    private func extractJSON(from content: String) -> String {
        VLMJSONExtractor.extractJSON(from: content)
    }
}

// MARK: - Ollama API Request/Response Models

/// Ollama Generate API request structure
private struct OllamaGenerateRequest: Encodable, Sendable {
    let model: String
    let prompt: String
    let images: [String]
    /// "json" restricts generation to valid JSON, preventing prose responses
    /// that would fall through to the plain-text bbox fallback.
    let format: String?
    let stream: Bool
    let options: OllamaOptions?
}

/// Ollama generation options
private struct OllamaOptions: Encodable, Sendable {
    let temperature: Double
    let numPredict: Int

    enum CodingKeys: String, CodingKey {
        case temperature
        case numPredict = "num_predict"
    }
}

/// Ollama Generate API response structure
private struct OllamaGenerateResponse: Decodable, Sendable {
    let model: String
    let response: String
    let done: Bool
    let totalDuration: Int64?
    let loadDuration: Int64?
    let promptEvalCount: Int?
    let evalCount: Int?

    enum CodingKeys: String, CodingKey {
        case model, response, done
        case totalDuration = "total_duration"
        case loadDuration = "load_duration"
        case promptEvalCount = "prompt_eval_count"
        case evalCount = "eval_count"
    }
}

/// Ollama error response structure
private struct OllamaErrorResponse: Decodable, Sendable {
    let error: String
}
