//
//  GLMOCRVLMProvider.swift
//  TransFrame
//
//  Integrates Zhipu GLM-OCR layout parsing for screenshot text extraction.
//

import CoreGraphics
import Foundation

struct GLMOCRVLMProvider: VLMProvider, Sendable {
    let id: String = "glm_ocr"
    let name: String = "GLM OCR"
    let configuration: VLMProviderConfiguration
    let mode: GLMOCRMode

    static let defaultBaseURL: URL = {
        guard let url = URL(string: "https://open.bigmodel.cn/api/paas/v4") else {
            fatalError("Invalid URL literal for GLMOCRVLMProvider.defaultBaseURL")
        }
        return url
    }()
    static let defaultModel = "glm-ocr"
    static let defaultLocalBaseURL: URL = {
        guard let url = URL(string: "http://127.0.0.1:18081") else {
            fatalError("Invalid URL literal for GLMOCRVLMProvider.defaultLocalBaseURL")
        }
        return url
    }()
    static let defaultLocalModel = "mlx-community/GLM-OCR-bf16"

    static let connectionTestImageDataURI = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAIAAAAmkwkpAAAAE0lEQVR4nGP8//8/AwwwwVl4OQCWbgMF7ZjH1AAAAABJRU5ErkJggg=="

    private let timeout: TimeInterval

    init(configuration: VLMProviderConfiguration, mode: GLMOCRMode = .cloud, timeout: TimeInterval = 60) {
        self.configuration = configuration
        self.mode = mode
        self.timeout = timeout
    }

    var isAvailable: Bool {
        get async {
            switch mode {
            case .cloud:
                return !configuration.apiKey.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
            case .local:
                return true
            }
        }
    }

    func analyze(image: CGImage) async throws -> ScreenAnalysisResult {
        guard let imageData = image.jpegData(quality: 0.9), !imageData.isEmpty else {
            throw VLMProviderError.imageEncodingFailed
        }

        let imageSize = CGSize(width: image.width, height: image.height)
        let dataURI = "data:image/jpeg;base64,\(imageData.base64EncodedString())"

        switch mode {
        case .cloud:
            let request = try Self.makeLayoutParsingRequest(
                baseURL: configuration.baseURL,
                apiKey: configuration.apiKey,
                modelName: configuration.modelName,
                fileDataURI: dataURI,
                timeout: timeout
            )
            let data = try await executeRequest(request, mode: .cloud)
            return try Self.parseResponse(data, fallbackImageSize: imageSize)
        case .local:
            let request = try Self.makeLocalChatRequest(
                baseURL: configuration.baseURL,
                apiKey: configuration.apiKey,
                modelName: configuration.modelName,
                fileDataURI: dataURI,
                timeout: timeout
            )
            let data = try await executeRequest(request, mode: .local)
            return try Self.parseLocalResponse(data, fallbackImageSize: imageSize)
        }
    }

    static func makeLayoutParsingRequest(
        baseURL: URL,
        apiKey: String,
        modelName: String,
        fileDataURI: String,
        timeout: TimeInterval
    ) throws -> URLRequest {
        let trimmedAPIKey = apiKey.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedAPIKey.isEmpty else {
            throw VLMProviderError.invalidConfiguration("GLM OCR requires an API key.")
        }

        let endpoint = baseURL.resolvingLocalhost.appendingPathComponent("layout_parsing")
        var request = URLRequest(url: endpoint)
        request.httpMethod = "POST"
        request.timeoutInterval = timeout
        request.setValue("Bearer \(trimmedAPIKey)", forHTTPHeaderField: "Authorization")
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")

        let body = GLMOCRLayoutParsingRequest(
            model: modelName.isEmpty ? defaultModel : modelName,
            file: fileDataURI,
            returnCropImages: false,
            needLayoutVisualization: false
        )
        request.httpBody = try JSONEncoder().encode(body)
        return request
    }

    static func makeLocalChatRequest(
        baseURL: URL,
        apiKey: String,
        modelName: String,
        fileDataURI: String,
        timeout: TimeInterval
    ) throws -> URLRequest {
        let endpoint = baseURL.resolvingLocalhost.appendingPathComponent("chat/completions")
        var request = URLRequest(url: endpoint)
        request.httpMethod = "POST"
        request.timeoutInterval = timeout
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")

        let body = GLMOCRLocalChatRequest(
            model: modelName.isEmpty ? defaultLocalModel : modelName,
            messages: [
                GLMOCRLocalChatMessage(
                    role: "system",
                    content: .text(VLMPromptTemplate.localModelSystemPrompt)
                ),
                GLMOCRLocalChatMessage(
                    role: "user",
                    content: .vision([
                        .text(VLMPromptTemplate.localModelUserPrompt + "\nReturn only valid JSON."),
                        .imageURL(GLMOCRLocalImageURL(url: fileDataURI))
                    ])
                ),
            ],
            maxTokens: 4096,
            temperature: 0.1
        )
        request.httpBody = try JSONEncoder().encode(body)
        return request
    }

    static func parseResponse(_ data: Data, fallbackImageSize: CGSize) throws -> ScreenAnalysisResult {
        let decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase

        if let errorResponse = try? decoder.decode(GLMOCRAPIErrorResponse.self, from: data),
           let message = errorResponse.error.message,
           !message.isEmpty {
            throw VLMProviderError.invalidResponse(message)
        }

        let response: GLMOCRLayoutParsingResponse
        do {
            response = try decoder.decode(GLMOCRLayoutParsingResponse.self, from: data)
        } catch {
            throw VLMProviderError.parsingFailed("Failed to decode GLM OCR response: \(error.localizedDescription)")
        }

        let segments = response.layoutDetails
            .flatMap { $0 }
            .compactMap { item in
                textSegment(from: item)
            }

        let resolvedImageSize = response.dataInfo?.pages.first.map {
            CGSize(width: $0.width, height: $0.height)
        } ?? fallbackImageSize

        return ScreenAnalysisResult(segments: segments, imageSize: resolvedImageSize)
    }

    static func parseLocalResponse(_ data: Data, fallbackImageSize: CGSize) throws -> ScreenAnalysisResult {
        let decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase

        if let errorResponse = try? decoder.decode(GLMOCRLocalErrorResponse.self, from: data) {
            throw VLMProviderError.invalidResponse(errorResponse.error.message)
        }

        let response: GLMOCRLocalChatResponse
        do {
            response = try decoder.decode(GLMOCRLocalChatResponse.self, from: data)
        } catch {
            throw VLMProviderError.parsingFailed("Failed to decode local GLM OCR response: \(error.localizedDescription)")
        }

        guard let content = response.choices?.first?.message?.content,
              !content.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            throw VLMProviderError.invalidResponse("No content in local GLM OCR response")
        }

        do {
            return try parseLocalContent(content).toScreenAnalysisResult(imageSize: fallbackImageSize)
        } catch {
            throw VLMProviderError.parsingFailed("Failed to parse local GLM OCR content: \(error.localizedDescription)")
        }
    }

    private func executeRequest(_ request: URLRequest, mode: GLMOCRMode) async throws -> Data {
        do {
            let (data, response) = try await URLSession.shared.data(for: request)
            guard let httpResponse = response as? HTTPURLResponse else {
                throw VLMProviderError.invalidResponse("Invalid HTTP response")
            }

            switch httpResponse.statusCode {
            case 200:
                return data
            case 401, 403:
                throw VLMProviderError.authenticationFailed
            case 429:
                throw VLMProviderError.rateLimited(
                    retryAfter: httpResponse.value(forHTTPHeaderField: "Retry-After").flatMap(TimeInterval.init),
                    message: parseAPIErrorMessage(from: data, mode: mode)
                )
            default:
                let message = parseAPIErrorMessage(from: data, mode: mode) ?? "HTTP \(httpResponse.statusCode)"
                throw VLMProviderError.invalidResponse(message)
            }
        } catch let error as VLMProviderError {
            throw error
        } catch {
            throw VLMProviderError.networkError(error.localizedDescription)
        }
    }

    private func parseAPIErrorMessage(from data: Data, mode: GLMOCRMode) -> String? {
        let decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase
        switch mode {
        case .cloud:
            if let response = try? decoder.decode(GLMOCRAPIErrorResponse.self, from: data),
               let message = response.error.message,
               !message.isEmpty {
                return message
            }
        case .local:
            if let response = try? decoder.decode(GLMOCRLocalErrorResponse.self, from: data) {
                return response.error.message
            }
        }
        return String(data: data, encoding: .utf8)
    }

    private static func parseLocalContent(_ content: String) throws -> VLMAnalysisResponse {
        let cleanedContent = extractJSON(from: content)

        if let jsonData = cleanedContent.data(using: .utf8),
           let response = try? JSONDecoder().decode(VLMAnalysisResponse.self, from: jsonData) {
            return response
        }

        if let jsonData = cleanedContent.data(using: .utf8),
           let textOnlyResponse = try? JSONDecoder().decode(GLMOCRLocalTextOnlyResponse.self, from: jsonData),
           let textContent = textOnlyResponse.resolvedText,
           let plainTextResponse = parsePlainTextResponse(textContent) {
            return plainTextResponse
        }

        if let plainTextResponse = parsePlainTextResponse(content) {
            return plainTextResponse
        }

        throw VLMProviderError.parsingFailed("Content was not valid JSON")
    }

    private static func textSegment(from item: GLMOCRLayoutItem) -> TextSegment? {
        let text = cleanedContent(from: item)
        guard !text.isEmpty else {
            return nil
        }

        guard item.bbox2D.count == 4 else {
            return nil
        }

        let x1 = clamp(item.bbox2D[0])
        let y1 = clamp(item.bbox2D[1])
        let x2 = clamp(item.bbox2D[2])
        let y2 = clamp(item.bbox2D[3])
        let width = max(0, x2 - x1)
        let height = max(0, y2 - y1)

        guard width > 0, height > 0 else {
            return nil
        }

        return TextSegment(
            text: text,
            boundingBox: CGRect(x: x1, y: y1, width: width, height: height),
            confidence: 1.0
        )
    }

    private static func cleanedContent(from item: GLMOCRLayoutItem) -> String {
        let trimmed = item.content.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else {
            return ""
        }

        switch item.label {
        case "image":
            return ""
        case "table":
            return stripHTML(from: trimmed)
        default:
            return collapseWhitespace(in: trimmed)
        }
    }

    private static func stripHTML(from string: String) -> String {
        let withoutTags = string.replacingOccurrences(
            of: "<[^>]+>",
            with: " ",
            options: .regularExpression
        )
        return collapseWhitespace(in: withoutTags)
    }

    private static func collapseWhitespace(in string: String) -> String {
        string.replacingOccurrences(
            of: "\\s+",
            with: " ",
            options: .regularExpression
        ).trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private static func clamp(_ value: CGFloat) -> CGFloat {
        min(max(value, 0), 1)
    }

    private static func extractJSON(from content: String) -> String {
        VLMJSONExtractor.extractJSON(from: content)
    }

    private static func parsePlainTextResponse(_ content: String) -> VLMAnalysisResponse? {
        let lines = content
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .components(separatedBy: .newlines)
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { line in
                guard !line.isEmpty else { return false }
                if line == "```" || line.hasPrefix("```") { return false }
                if ["{", "}", "[", "]"].contains(line) { return false }
                return true
            }

        guard !lines.isEmpty else {
            return nil
        }

        let segments = lines.enumerated().map { index, text in
            VLMTextSegment(
                text: text,
                boundingBox: VLMBoundingBox(
                    x: 0,
                    y: CGFloat(index) / CGFloat(max(lines.count, 1)),
                    width: 1,
                    height: 1 / CGFloat(max(lines.count, 1))
                ),
                confidence: nil
            )
        }

        return VLMAnalysisResponse(segments: segments)
    }
}

private struct GLMOCRLayoutParsingRequest: Encodable, Sendable {
    let model: String
    let file: String
    let returnCropImages: Bool
    let needLayoutVisualization: Bool

    enum CodingKeys: String, CodingKey {
        case model
        case file
        case returnCropImages = "return_crop_images"
        case needLayoutVisualization = "need_layout_visualization"
    }
}

private struct GLMOCRLocalChatRequest: Encodable, Sendable {
    let model: String
    let messages: [GLMOCRLocalChatMessage]
    let maxTokens: Int
    let temperature: Double

    enum CodingKeys: String, CodingKey {
        case model, messages, temperature
        case maxTokens = "max_tokens"
    }
}

private struct GLMOCRLocalTextOnlyResponse: Decodable, Sendable {
    let text: String?
    let Text: String?

    var resolvedText: String? {
        text ?? Text
    }
}

private struct GLMOCRLocalChatMessage: Encodable, Sendable {
    let role: String
    let content: MessageContent

    enum MessageContent: Sendable {
        case text(String)
        case vision([VisionContent])
    }

    enum VisionContent: Sendable {
        case text(String)
        case imageURL(GLMOCRLocalImageURL)
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

private struct GLMOCRLocalImageURL: Encodable, Sendable {
    let url: String
    let detail: String

    init(url: String, detail: String = "high") {
        self.url = url
        self.detail = detail
    }
}

struct GLMOCRLayoutParsingResponse: Decodable, Sendable {
    let id: String?
    let model: String?
    let mdResults: String?
    let layoutDetails: [[GLMOCRLayoutItem]]
    let dataInfo: GLMOCRDataInfo?
}

struct GLMOCRLayoutItem: Decodable, Sendable {
    let index: Int?
    let label: String
    let bbox2D: [CGFloat]
    let content: String
    let height: Int?
    let width: Int?
}

struct GLMOCRDataInfo: Decodable, Sendable {
    let numPages: Int?
    let pages: [GLMOCRPageInfo]
}

struct GLMOCRPageInfo: Decodable, Sendable {
    let width: CGFloat
    let height: CGFloat
}

private struct GLMOCRAPIErrorResponse: Decodable, Sendable {
    let error: GLMOCRAPIErrorBody
}

private struct GLMOCRAPIErrorBody: Decodable, Sendable {
    let message: String?
}

struct GLMOCRLocalModelsResponse: Decodable, Sendable {
    let data: [GLMOCRLocalModel]
}

struct GLMOCRLocalModel: Decodable, Sendable {
    let id: String
}

private struct GLMOCRLocalChatResponse: Decodable, Sendable {
    let choices: [GLMOCRLocalChatChoice]?
}

private struct GLMOCRLocalChatChoice: Decodable, Sendable {
    let message: GLMOCRLocalResponseMessage?
}

private struct GLMOCRLocalResponseMessage: Decodable, Sendable {
    let content: String?
}

private struct GLMOCRLocalErrorResponse: Decodable, Sendable {
    let error: GLMOCRLocalErrorBody
}

private struct GLMOCRLocalErrorBody: Decodable, Sendable {
    let message: String
}
