// LLMTranslationCore.cpp — pure-logic port of LLMTranslationBatching +
// LLMTranslationResponseParser (Swift → C++20, same vectors, same semantics).
#include "tf/LLMTranslationCore.hpp"
#include "tf/Json.hpp"

#include <algorithm>

namespace tf {

// ---------------------------------------------------------------------------
// Batching (Swift LLMTranslationBatching.swift)
// ---------------------------------------------------------------------------
const char* LLMTranslationBatching::separator() { return "\n---\n"; }

std::int64_t LLMTranslationBatching::estimatedTokens(const std::string& text) {
    // Swift counts UTF-16 code units; C++ counts the same via u16 length.
    // For CJK/BMP content these agree; surrogate pairs count 2 in both.
    std::int64_t n = 0;
    for (unsigned char c : text) {
        if (c < 0x80) {
            ++n;
        } else if ((c & 0xE0) == 0xC0) {
            n += 1;  // 2-byte UTF-8 → 1 UTF-16 unit
        } else if ((c & 0xF0) == 0xE0) {
            n += 1;  // 3-byte → 1 UTF-16 unit
        } else if ((c & 0xF8) == 0xF0) {
            n += 2;  // 4-byte → surrogate pair
        }
        // continuation bytes contribute 0
    }
    return n;
}

std::vector<std::vector<std::string>> LLMTranslationBatching::split(
    const std::vector<std::string>& texts, std::int64_t maxOutputTokens,
    const std::string& promptTemplate) {
    const std::int64_t budget = std::max<std::int64_t>(1, maxOutputTokens);
    // Fixed prompt with {text} removed still occupies the budget.
    std::string fixedPrompt = promptTemplate;
    const std::string needle = "{text}";
    std::size_t pos;
    while ((pos = fixedPrompt.find(needle)) != std::string::npos) {
        fixedPrompt.replace(pos, needle.size(), "");
    }
    const std::int64_t promptCost = estimatedTokens(fixedPrompt);
    const std::int64_t sourceBudget = std::max<std::int64_t>(4, budget - promptCost);

    std::vector<std::vector<std::string>> batches;
    std::vector<std::string> current;
    std::int64_t used = 0;
    const std::int64_t sepCost = estimatedTokens(std::string(separator()));

    auto appendPiece = [&](const std::string& piece) {
        const std::int64_t pieceCost =
            estimatedTokens(piece) + (current.empty() ? 0 : sepCost);
        if (!current.empty() && used + pieceCost > sourceBudget) {
            batches.push_back(current);
            current.clear();
            used = 0;
        }
        used += estimatedTokens(piece) + (current.empty() ? 0 : sepCost);
        current.push_back(piece);
    };

    for (const auto& text : texts) {
        if (estimatedTokens(text) > budget) {
            for (const auto& piece : splitLongText(text, sourceBudget)) appendPiece(piece);
        } else {
            appendPiece(text);
        }
    }
    if (!current.empty()) batches.push_back(current);
    return batches;
}

std::vector<std::string> LLMTranslationBatching::splitLongText(const std::string& text,
                                                               std::int64_t budget) {
    if (estimatedTokens(text) <= budget) return {text};
    std::vector<std::string> pieces;
    std::string current;
    // Walk UTF-8 code points so a character is never cut in half (Swift walks
    // Characters; both never split a code point).
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t len = 1;
        unsigned char c = static_cast<unsigned char>(text[i]);
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        const std::string ch = text.substr(i, len);
        i += len;
        std::int64_t curTokens = estimatedTokens(current);
        std::int64_t chTokens = estimatedTokens(ch);
        if (!current.empty() && curTokens + chTokens > budget) {
            pieces.push_back(current);
            current.clear();
        }
        current += ch;
    }
    if (!current.empty()) pieces.push_back(current);
    return pieces;
}

// ---------------------------------------------------------------------------
// Response parser (Swift LLMTranslationResponseParser.swift)
// ---------------------------------------------------------------------------
namespace {

std::string trimmed(const std::string& s) {
    std::size_t b = 0, e = s.size();
    auto isSpace = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
    };
    while (b < e && isSpace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && isSpace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t nl = text.find('\n', start);
        if (nl == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

}  // namespace

Outcome<std::string, ProviderError> LLMTranslationResponseParser::extractText(
    const std::string& body, TranslationEngineType engine) {
    const std::string trimmedBody = trimmed(body);

    // SSE only when the first payload line is a data event (a body whose
    // content contains "\ndata:" still parses as plain JSON — Swift rule).
    if (trimmedBody.rfind("data:", 0) == 0) {
        if (engine == TranslationEngineType::Claude) return extractClaudeSSE(trimmedBody);
        return extractOpenAISSE(trimmedBody);
    }

    Json json;
    try {
        json = Json::parse(body);
    } catch (const Json::parse_error&) {
        return Outcome<std::string, ProviderError>::makeError(
            ProviderError::content("Response JSON is not parseable"));
    }
    if (!json.is_object()) {
        return Outcome<std::string, ProviderError>::makeError(
            ProviderError::content("Response JSON is not a dictionary"));
    }

    // API error surface.
    if (json.contains("error") && json["error"].is_object() && json["error"].contains("message") &&
        json["error"]["message"].is_string()) {
        return Outcome<std::string, ProviderError>::makeError(
            ProviderError::content("API error: " + json["error"]["message"].get<std::string>()));
    }

    if (engine == TranslationEngineType::Claude) {
        if (json.contains("stop_reason") && json["stop_reason"].is_string() &&
            json["stop_reason"].get<std::string>() == "max_tokens") {
            return Outcome<std::string, ProviderError>::makeError(ProviderError::content(
                "Claude reached the output token limit"));
        }
        if (json.contains("content") && json["content"].is_array()) {
            for (const auto& part : json["content"]) {
                if (part.is_object() && part.contains("type") && part["type"] == "text" &&
                    part.contains("text") && part["text"].is_string()) {
                    return Outcome<std::string, ProviderError>::makeOk(
                        trimmed(part["text"].get<std::string>()));
                }
            }
        }
        return Outcome<std::string, ProviderError>::makeError(
            ProviderError::content("Unexpected JSON response structure"));
    }

    // OpenAI-compatible: choices[0].
    if (!json.contains("choices") || !json["choices"].is_array() || json["choices"].empty()) {
        return Outcome<std::string, ProviderError>::makeError(
            ProviderError::content("Unexpected JSON response structure"));
    }
    const Json& choice = json["choices"][0];
    if (!choice.is_object()) {
        return Outcome<std::string, ProviderError>::makeError(
            ProviderError::content("Unexpected JSON response structure"));
    }
    // Truncation must not be reported as success (macOS test vector).
    if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
        const std::string reason = choice["finish_reason"].get<std::string>();
        if (reason == "length" || reason == "max_tokens") {
            return Outcome<std::string, ProviderError>::makeError(ProviderError::content(
                "The response was truncated by the output token limit"));
        }
    }
    if (choice.contains("message") && choice["message"].is_object() &&
        choice["message"].contains("content") && choice["message"]["content"].is_string()) {
        return Outcome<std::string, ProviderError>::makeOk(
            trimmed(choice["message"]["content"].get<std::string>()));
    }
    if (choice.contains("text") && choice["text"].is_string()) {
        return Outcome<std::string, ProviderError>::makeOk(
            trimmed(choice["text"].get<std::string>()));
    }
    return Outcome<std::string, ProviderError>::makeError(
        ProviderError::content("Unexpected JSON response structure"));
}

Outcome<std::string, ProviderError> LLMTranslationResponseParser::extractOpenAISSE(
    const std::string& text) {
    std::string result;
    bool isTruncated = false;
    for (const auto& rawLine : splitLines(text)) {
        const std::string line = trimmed(rawLine);
        if (line.rfind("data:", 0) != 0) continue;
        std::string payload = trimmed(line.substr(5));
        if (payload == "[DONE]") continue;
        Json json;
        try {
            json = Json::parse(payload);
        } catch (const Json::parse_error&) {
            continue;
        }
        if (!json.is_object() || !json.contains("choices") || !json["choices"].is_array() ||
            json["choices"].empty()) {
            continue;
        }
        const Json& choice = json["choices"][0];
        if (!choice.is_object()) continue;
        if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
            const std::string reason = choice["finish_reason"].get<std::string>();
            isTruncated = isTruncated || reason == "length" || reason == "max_tokens";
        }
        if (choice.contains("delta") && choice["delta"].is_object()) {
            const Json& delta = choice["delta"];
            if (delta.contains("content") && delta["content"].is_string()) {
                result += delta["content"].get<std::string>();
            }
        } else if (choice.contains("text") && choice["text"].is_string()) {
            result += choice["text"].get<std::string>();
        }
    }
    if (isTruncated) {
        return Outcome<std::string, ProviderError>::makeError(
            ProviderError::content("The stream reached the output token limit"));
    }
    if (result.empty()) {
        return Outcome<std::string, ProviderError>::makeError(
            ProviderError::content("Empty text from SSE stream"));
    }
    return Outcome<std::string, ProviderError>::makeOk(trimmed(result));
}

Outcome<std::string, ProviderError> LLMTranslationResponseParser::extractClaudeSSE(
    const std::string& text) {
    std::string result;
    bool isTruncated = false;
    for (const auto& rawLine : splitLines(text)) {
        const std::string line = trimmed(rawLine);
        if (line.rfind("data:", 0) != 0) continue;
        std::string payload = trimmed(line.substr(5));
        Json json;
        try {
            json = Json::parse(payload);
        } catch (const Json::parse_error&) {
            continue;
        }
        if (!json.is_object()) continue;
        if (json.contains("stop_reason") && json["stop_reason"].is_string() &&
            json["stop_reason"].get<std::string>() == "max_tokens") {
            isTruncated = true;
        }
        if (json.contains("delta") && json["delta"].is_object()) {
            const Json& delta = json["delta"];
            if (delta.contains("stop_reason") && delta["stop_reason"].is_string() &&
                delta["stop_reason"].get<std::string>() == "max_tokens") {
                isTruncated = true;
            }
            if (delta.contains("text") && delta["text"].is_string()) {
                result += delta["text"].get<std::string>();
            }
        }
    }
    if (isTruncated) {
        return Outcome<std::string, ProviderError>::makeError(
            ProviderError::content("Claude stream reached the output token limit"));
    }
    if (result.empty()) {
        return Outcome<std::string, ProviderError>::makeError(
            ProviderError::content("Empty text from SSE stream"));
    }
    return Outcome<std::string, ProviderError>::makeOk(trimmed(result));
}

}  // namespace tf
