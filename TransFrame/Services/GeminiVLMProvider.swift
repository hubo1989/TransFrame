//
//  GeminiVLMProvider.swift
//  TransFrame
//

import Foundation
import CoreGraphics
import os

/// VLM Provider implementation for Google Gemini API
final class GeminiVLMProvider: VLMProvider, Sendable {
    let id: String = "gemini"
    let name: String = "Gemini"
    let configuration: VLMProviderConfiguration
    private let timeout: TimeInterval = 60.0
    private static let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "TransFrame", category: "GeminiVLMProvider")

    var isAvailable: Bool {
        get async { true }
    }

    init(configuration: VLMProviderConfiguration) {
        self.configuration = configuration
    }

    func analyze(image: CGImage) async throws -> ScreenAnalysisResult {
        try await analyze(image: image, prompt: .ocr)
    }

    func analyze(image: CGImage, prompt: VLMPromptPair) async throws -> ScreenAnalysisResult {
        let encoded = try AnalysisImageEncoder.jpeg(image)
        let imageSize = CGSize(width: image.width, height: image.height)

        let request = try buildRequest(base64Image: encoded.base64String, prompts: prompt)
        let responseData = try await executeRequest(request)
        let content = try extractContent(from: responseData)

        let cleaned = VLMJSONExtractor.extractJSON(from: content)
        guard let data = cleaned.data(using: .utf8) else {
            throw VLMProviderError.parsingFailed("Failed to encode cleaned JSON")
        }

        let decoder = JSONDecoder()
        do {
            let parsed = try decoder.decode(VLMAnalysisResponse.self, from: data)
            return parsed.toScreenAnalysisResult(imageSize: imageSize)
        } catch {
            throw VLMProviderError.parsingFailed("Failed to parse Gemini VLM response: \(error.localizedDescription)")
        }
    }

    private func buildRequest(base64Image: String, prompts: VLMPromptPair = .ocr) throws -> URLRequest {
        let baseURL = configuration.baseURL.resolvingLocalhost
        let model = configuration.modelName.isEmpty ? "gemini-2.0-flash" : configuration.modelName

        // If URL already points to OpenAI compatible endpoint (e.g., /chat/completions)
        let endpoint: URL
        let isChatCompletions = baseURL.path.contains("chat/completions")
        if isChatCompletions {
            endpoint = baseURL
        } else {
            // Standard Google Gemini REST API: /models/{model}:generateContent
            endpoint = baseURL.appendingPathComponent("models/\(model):generateContent")
        }

        var request = URLRequest(url: endpoint)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.timeoutInterval = timeout

        // Optional API key: if provided, send via x-goog-api-key or Authorization
        let apiKey = configuration.apiKey.trimmingCharacters(in: .whitespacesAndNewlines)
        if !apiKey.isEmpty {
            if isChatCompletions {
                request.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
            } else {
                request.setValue(apiKey, forHTTPHeaderField: "x-goog-api-key")
            }
        }

        if isChatCompletions {
            // OpenAI compatible payload
            let body: [String: Any] = [
                "model": model,
                "messages": [
                    [
                        "role": "user",
                        "content": [
                            ["type": "text", "text": prompts.user],
                            ["type": "image_url", "image_url": ["url": "data:image/jpeg;base64,\(base64Image)"]]
                        ]
                    ]
                ],
                "response_format": ["type": "json_object"]
            ]
            request.httpBody = try JSONSerialization.data(withJSONObject: body)
        } else {
            // Gemini generateContent payload
            let prompt = "\(prompts.system)\n\n\(prompts.user)"
            let body: [String: Any] = [
                "contents": [
                    [
                        "parts": [
                            ["text": prompt],
                            [
                                "inline_data": [
                                    "mime_type": "image/jpeg",
                                    "data": base64Image
                                ]
                            ]
                        ]
                    ]
                ],
                "generationConfig": [
                    "response_mime_type": "application/json"
                ]
            ]
            request.httpBody = try JSONSerialization.data(withJSONObject: body)
        }

        return request
    }

    private func executeRequest(_ request: URLRequest) async throws -> Data {
        do {
            let (data, response) = try await URLSession.shared.data(for: request)
            guard let httpResponse = response as? HTTPURLResponse else {
                throw VLMProviderError.networkError("Invalid response type")
            }

            if httpResponse.statusCode == 401 || httpResponse.statusCode == 403 {
                throw VLMProviderError.authenticationFailed
            }

            guard (200...299).contains(httpResponse.statusCode) else {
                let errorBody = String(data: data, encoding: .utf8) ?? "Unknown server error"
                throw VLMProviderError.invalidResponse("HTTP \(httpResponse.statusCode): \(errorBody)")
            }

            return data
        } catch let error as VLMProviderError {
            throw error
        } catch {
            throw VLMProviderError.networkError(error.localizedDescription)
        }
    }

    private func extractContent(from data: Data) throws -> String {
        // Try decoding as Gemini standard response
        if let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
            if let candidates = json["candidates"] as? [[String: Any]],
               let firstCandidate = candidates.first,
               let content = firstCandidate["content"] as? [String: Any],
               let parts = content["parts"] as? [[String: Any]],
               let firstPart = parts.first,
               let text = firstPart["text"] as? String {
                return text
            }

            // Try decoding as OpenAI compatible response
            if let choices = json["choices"] as? [[String: Any]],
               let firstChoice = choices.first,
               let message = firstChoice["message"] as? [String: Any],
               let text = message["content"] as? String {
                return text
            }
        }

        if let raw = String(data: data, encoding: .utf8) {
            return raw
        }

        throw VLMProviderError.invalidResponse("Could not extract text content from response")
    }
}
