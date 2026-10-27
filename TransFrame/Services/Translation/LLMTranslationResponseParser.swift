import Foundation

/// OpenAI-compatible 和 Claude 文本响应的共同完成状态检查。
enum LLMTranslationResponseParser {
    static func extractText(from data: Data, for engine: TranslationEngineType) throws -> String {
        guard let rawString = String(data: data, encoding: .utf8) else {
            throw TranslationProviderError.translationFailed("Unable to decode data to UTF-8 string")
        }
        let trimmed = rawString.trimmingCharacters(in: .whitespacesAndNewlines)
        // Only treat the response as an SSE stream when the first payload line
        // is a data event. Translation bodies legitimately containing "\ndata:"
        // (logs, SSE examples) must still parse as plain JSON.
        if trimmed.hasPrefix("data:") {
            guard engine != .claude else {
                return try extractClaudeSSE(from: trimmed)
            }
            return try extractOpenAISSE(from: trimmed)
        }
        guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw TranslationProviderError.translationFailed("Response JSON is not a dictionary")
        }
        if let error = json["error"] as? [String: Any], let message = error["message"] as? String {
            throw TranslationProviderError.translationFailed("API error: \(message)")
        }
        if engine == .claude {
            guard json["stop_reason"] as? String != "max_tokens" else {
                throw TranslationProviderError.incompleteResponse("Claude reached the output token limit")
            }
            guard let content = (json["content"] as? [[String: Any]])?
                .first(where: { $0["type"] as? String == "text" })?["text"] as? String else {
                throw TranslationProviderError.translationFailed("Unexpected JSON response structure")
            }
            return content.trimmingCharacters(in: .whitespacesAndNewlines)
        }
        guard let choice = (json["choices"] as? [[String: Any]])?.first else {
            throw TranslationProviderError.translationFailed("Unexpected JSON response structure")
        }
        guard choice["finish_reason"] as? String != "length",
              choice["finish_reason"] as? String != "max_tokens" else {
            throw TranslationProviderError.incompleteResponse("The response reached the output token limit")
        }
        guard let content = (choice["message"] as? [String: Any])?["content"] as? String else {
            throw TranslationProviderError.translationFailed("Unexpected JSON response structure")
        }
        return content.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private static func extractOpenAISSE(from text: String) throws -> String {
        var result = ""
        var isTruncated = false
        for line in text.components(separatedBy: .newlines) {
            let line = line.trimmingCharacters(in: .whitespacesAndNewlines)
            guard line.hasPrefix("data:"), line != "data: [DONE]",
                  let data = line.dropFirst(5).trimmingCharacters(in: .whitespaces).data(using: .utf8),
                  let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { continue }
            if let choice = (json["choices"] as? [[String: Any]])?.first {
                let reason = choice["finish_reason"] as? String
                isTruncated = isTruncated || reason == "length" || reason == "max_tokens"
                if let delta = choice["delta"] as? [String: Any], let content = delta["content"] as? String {
                    result += content
                } else if let content = choice["text"] as? String {
                    result += content
                }
            }
        }
        guard !isTruncated else { throw TranslationProviderError.incompleteResponse("The stream reached the output token limit") }
        guard !result.isEmpty else { throw TranslationProviderError.translationFailed("Empty text from SSE stream") }
        return result.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private static func extractClaudeSSE(from text: String) throws -> String {
        var result = ""
        var isTruncated = false
        for line in text.components(separatedBy: .newlines) {
            let line = line.trimmingCharacters(in: .whitespacesAndNewlines)
            guard line.hasPrefix("data:"),
                  let data = line.dropFirst(5).trimmingCharacters(in: .whitespaces).data(using: .utf8),
                  let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { continue }
            if json["stop_reason"] as? String == "max_tokens" { isTruncated = true }
            if let delta = json["delta"] as? [String: Any] {
                if delta["stop_reason"] as? String == "max_tokens" { isTruncated = true }
                if let content = delta["text"] as? String { result += content }
            }
        }
        guard !isTruncated else { throw TranslationProviderError.incompleteResponse("Claude stream reached the output token limit") }
        guard !result.isEmpty else { throw TranslationProviderError.translationFailed("Empty text from SSE stream") }
        return result.trimmingCharacters(in: .whitespacesAndNewlines)
    }
}
