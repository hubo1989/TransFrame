//
//  VLMProvider.swift
//  TransFrame
//
//  Created for US-004: VLM Provider Protocol
//

import Foundation
import CoreGraphics

// MARK: - VLM Provider Configuration

/// Configuration for VLM provider connections
struct VLMProviderConfiguration: Sendable, Equatable {
    let apiKey: String
    let baseURL: URL
    let modelName: String
    
    init(apiKey: String, baseURL: URL, modelName: String) {
        self.apiKey = apiKey
        self.baseURL = baseURL
        self.modelName = modelName
    }
    
    init(apiKey: String, baseURLString: String, modelName: String) throws {
        guard let url = URL(string: baseURLString) else {
            throw VLMProviderError.invalidConfiguration("Invalid base URL: \(baseURLString)")
        }
        self.apiKey = apiKey
        self.baseURL = url
        self.modelName = modelName
    }
}

// MARK: - VLM Provider Protocol

/// Protocol defining a Vision Language Model provider for screen analysis
/// Implementations can wrap different VLM APIs (OpenAI GPT-4V, Claude Vision, Gemini, etc.)
protocol VLMProvider: Sendable {
    /// Unique identifier for this provider
    var id: String { get }
    
    /// Human-readable name for display
    var name: String { get }
    
    /// Whether the provider is currently available (configured and reachable)
    var isAvailable: Bool { get async }
    
    /// Current configuration
    var configuration: VLMProviderConfiguration { get }
    
    /// Analyze an image and extract text segments with bounding boxes
    /// - Parameter image: The image to analyze
    /// - Returns: Analysis result containing text segments with positions
    /// - Throws: VLMProviderError if analysis fails
    func analyze(image: CGImage) async throws -> ScreenAnalysisResult
}

// MARK: - VLM Provider Errors

/// Errors that can occur during VLM provider operations
enum VLMProviderError: LocalizedError, Sendable {
    case invalidConfiguration(String)
    case networkError(String)
    case authenticationFailed
    case rateLimited(retryAfter: TimeInterval?, message: String? = nil)
    case invalidResponse(String)
    case modelUnavailable(String)
    case imageEncodingFailed
    case parsingFailed(String)
    
    var errorDescription: String? {
        switch self {
        case .invalidConfiguration(let message):
            return "Invalid configuration: \(message)"
        case .networkError(let message):
            return "Network error: \(message)"
        case .authenticationFailed:
            return "Authentication failed. Please check your API key."
        case .rateLimited(let retryAfter, let message):
            if let msg = message {
                return msg
            }
            if let seconds = retryAfter {
                return "Rate limited. Retry after \(Int(seconds)) seconds."
            }
            return "Rate limited. Please try again later."
        case .invalidResponse(let message):
            return "Invalid response from server: \(message)"
        case .modelUnavailable(let model):
            return "Model '\(model)' is not available."
        case .imageEncodingFailed:
            return "Failed to encode image for upload."
        case .parsingFailed(let message):
            return "Failed to parse VLM response: \(message)"
        }
    }
}

// MARK: - VLM Prompt Template

/// Standard prompt template for VLM screen analysis
/// Designed to extract text with bounding boxes in a structured JSON format
enum VLMPromptTemplate {
    
    /// System prompt establishing the VLM's role
    static let systemPrompt = """
        You are an OCR assistant. Recognize ALL text in the image.
        Return JSON: {"segments":[{"text":"...","boundingBox":{"x":0.0,"y":0.0,"width":0.0,"height":0.0},"confidence":0.95}]}
        """
    
    /// User prompt requesting text extraction
    static let userPrompt = """
        Please recognize all text in this image and return the results in JSON format.
        Use this format: {"segments":[{"text":"...","boundingBox":{"x":0.0,"y":0.0,"width":0.0,"height":0.0},"confidence":0.95}]}
        
        - x,y: top-left corner (0.0-1.0 normalized to image size)
        - width,height: box dimensions (0.0-1.0)
        - confidence: 0.0-1.0
        """

    /// Local model prompts (same as cloud - use unified prompt)
    static let localModelSystemPrompt = systemPrompt
    static let localModelUserPrompt = userPrompt
    
    /// JSON schema description for documentation and API configuration
    /// Used to configure VLM APIs that support structured output (e.g., OpenAI's response_format)
    static let responseSchemaDescription = """
        {
          "type": "object",
          "properties": {
            "segments": {
              "type": "array",
              "items": {
                "type": "object",
                "properties": {
                  "text": {"type": "string"},
                  "boundingBox": {
                    "type": "object",
                    "properties": {
                      "x": {"type": "number", "minimum": 0, "maximum": 1},
                      "y": {"type": "number", "minimum": 0, "maximum": 1},
                      "width": {"type": "number", "minimum": 0, "maximum": 1},
                      "height": {"type": "number", "minimum": 0, "maximum": 1}
                    },
                    "required": ["x", "y", "width", "height"]
                  },
                  "confidence": {"type": "number", "minimum": 0, "maximum": 1}
                },
                "required": ["text", "boundingBox", "confidence"]
              }
            }
          },
          "required": ["segments"]
        }
        """
}

// MARK: - VLM Response Parsing

/// Response structure from VLM for parsing
struct VLMAnalysisResponse: Codable, Sendable {
    let segments: [VLMTextSegment]
}

struct VLMTextSegment: Codable, Sendable {
    let text: String
    let boundingBox: VLMBoundingBox
    let confidence: Float?
}

struct VLMBoundingBox: Codable, Sendable {
    let x: CGFloat
    let y: CGFloat
    let width: CGFloat
    let height: CGFloat
    
    var cgRect: CGRect {
        CGRect(x: x, y: y, width: width, height: height)
    }
}

// MARK: - Response Conversion Extension

extension VLMAnalysisResponse {
    /// Convert VLM response to ScreenAnalysisResult
    func toScreenAnalysisResult(imageSize: CGSize) -> ScreenAnalysisResult {
        let textSegments = segments.map { segment in
            TextSegment(
                text: segment.text,
                boundingBox: segment.boundingBox.cgRect,
                confidence: segment.confidence ?? 1.0
            )
        }
        return ScreenAnalysisResult(segments: textSegments, imageSize: imageSize)
    }
}

// MARK: - VLM JSON Extractor

/// Shared helper for extracting clean JSON from LLM/VLM text responses.
enum VLMJSONExtractor {
    /// Extracts JSON string from potentially markdown-wrapped or free-form text response.
    static func extractJSON(from content: String) -> String {
        var text = content.trimmingCharacters(in: .whitespacesAndNewlines)

        // Handle markdown code blocks
        if text.hasPrefix("```json") {
            text = String(text.dropFirst(7))
        } else if text.hasPrefix("```") {
            text = String(text.dropFirst(3))
        }

        if text.hasSuffix("```") {
            text = String(text.dropLast(3))
        }

        text = text.trimmingCharacters(in: .whitespacesAndNewlines)

        // Handle case where response has leading text before JSON
        if let jsonStart = text.firstIndex(of: "{") {
            text = String(text[jsonStart...])
        }

        // Handle case where there's trailing text after JSON
        if let jsonEnd = text.lastIndex(of: "}") {
            text = String(text[...jsonEnd])
        }

        return text.trimmingCharacters(in: .whitespacesAndNewlines)
    }
}
