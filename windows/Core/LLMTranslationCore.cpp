// LLMTranslationCore.cpp — batching + response parser (Swift -> C++20).
#include "LLMTranslationCore.h"

#include <algorithm>
#include <cctype>

namespace tf::core {

namespace {

std::string trimmed(const std::string& s) {
    std::size_t b = 0, e = s.size();
    auto isSpace = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    };
    while (b < e && isSpace(static_cast<unsigned char>(s[b]))) { ++b; }
    while (e > b && isSpace(static_cast<unsigned char>(s[e - 1]))) { --e; }
    return s.substr(b, e - b);
}

bool hasPrefix(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

/// UTF-8 code-point-aware advance: returns byte length of the code point at i.
std::size_t utf8LengthAt(const std::string& s, std::size_t i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) { return 1; }
    if ((c & 0xE0) == 0xC0) { return 2; }
    if ((c & 0xF0) == 0xE0) { return 3; }
    if ((c & 0xF8) == 0xF0) { return 4; }
    return 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// Batching (Swift LLMTranslationBatching.swift)
// ---------------------------------------------------------------------------
const char* LLMTranslationBatching::separator() { return "\n---\n"; }

std::int64_t LLMTranslationBatching::estimatedTokens(const std::string& text) {
    // Swift counts UTF-16 code units; surrogate pairs count 2 in both.
    std::int64_t n = 0;
    for (const unsigned char c : text) {
        if (c < 0x80) {
            ++n;
        } else if ((c & 0xE0) == 0xC0) {
            n += 1;  // 2-byte UTF-8 -> 1 UTF-16 unit
        } else if ((c & 0xF0) == 0xE0) {
            n += 1;  // 3-byte -> 1 UTF-16 unit
        } else if ((c & 0xF8) == 0xF0) {
            n += 2;  // 4-byte -> surrogate pair
        }
        // continuation bytes contribute 0
    }
    return n;
}

std::vector<std::vector<std::string>> LLMTranslationBatching::split(
    const std::vector<std::string>& texts, std::int64_t maxOutputTokens,
    const std::string* promptTemplate) {
    const std::int64_t budget = std::max<std::int64_t>(1, maxOutputTokens);
    // Fixed prompt with {text} removed still occupies the budget.
    std::string fixedPrompt;
    if (promptTemplate != nullptr) {
        fixedPrompt = *promptTemplate;
        const std::string needle = "{text}";
        std::size_t pos;
        while ((pos = fixedPrompt.find(needle)) != std::string::npos) {
            fixedPrompt.replace(pos, needle.size(), "");
        }
    }
    const std::int64_t promptCost = estimatedTokens(fixedPrompt);
    const std::int64_t sourceBudget = std::max<std::int64_t>(4, budget - promptCost);

    std::vector<std::vector<std::string>> batches;
    std::vector<std::string> current;
    std::int64_t used = 0;
    const std::int64_t sepCost = estimatedTokens("\n---\n");

    auto appendPiece = [&](const std::string& piece) {
        const std::int64_t cost = estimatedTokens(piece) + (current.empty() ? 0 : sepCost);
        if (!current.empty() && used + cost > sourceBudget) {
            batches.push_back(current);
            current.clear();
            used = 0;
        }
        used += estimatedTokens(piece) + (current.empty() ? 0 : sepCost);
        current.push_back(piece);
    };

    for (const auto& text : texts) {
        if (estimatedTokens(text) > budget) {
            for (const auto& piece : splitLongText(text, sourceBudget)) { appendPiece(piece); }
        } else {
            appendPiece(text);
        }
    }
    if (!current.empty()) { batches.push_back(current); }
    return batches;
}

std::vector<std::string> LLMTranslationBatching::splitLongText(const std::string& text,
                                                               std::int64_t budget) {
    if (estimatedTokens(text) <= budget) { return {text}; }
    std::vector<std::string> pieces;
    std::string current;
    // Walk UTF-8 code points so a character is never cut in half (Swift walks
    // Characters; both never split a code point).
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t len = utf8LengthAt(text, i);
        const std::string ch = text.substr(i, len);
        i += len;
        if (!current.empty() && estimatedTokens(current) + estimatedTokens(ch) > budget) {
            pieces.push_back(current);
            current.clear();
        }
        current += ch;
    }
    if (!current.empty()) { pieces.push_back(current); }
    return pieces;
}

// ---------------------------------------------------------------------------
// Response parser (Swift LLMTranslationResponseParser.swift)
// ---------------------------------------------------------------------------
LLMTextResult LLMTranslationResponseParser::extractText(const std::string& body,
                                                        TranslationEngineType engine) {
    const std::string trimmedBody = trimmed(body);
    // Only treat the response as an SSE stream when the first payload line is
    // a data event. Translation bodies legitimately containing "\ndata:" (logs,
    // SSE examples) must still parse as plain JSON.
    if (hasPrefix(trimmedBody, "data:")) {
        if (engine == TranslationEngineType::Claude) { return extractClaudeSSE(trimmedBody); }
        return extractOpenAISSE(trimmedBody);
    }

    Json json;
    try {
        json = Json::parse(body);
    } catch (const Json::exception&) {
        return LLMTextResult::failure("Response JSON is not a dictionary");
    }
    if (!json.is_object()) {
        return LLMTextResult::failure("Response JSON is not a dictionary");
    }

    if (auto errorIt = json.find("error");
        errorIt != json.end() && errorIt->is_object()) {
        if (auto messageIt = errorIt->find("message");
            messageIt != errorIt->end() && messageIt->is_string()) {
            return LLMTextResult::failure("API error: " + messageIt->get<std::string>());
        }
    }

    if (engine == TranslationEngineType::Claude) {
        if (auto stopIt = json.find("stop_reason");
            stopIt != json.end() && stopIt->is_string() &&
            stopIt->get<std::string>() == "max_tokens") {
            return LLMTextResult::truncated("Claude reached the output token limit");
        }
        if (auto contentIt = json.find("content");
            contentIt != json.end() && contentIt->is_array()) {
            for (const auto& block : *contentIt) {
                if (block.is_object()) {
                    auto typeIt = block.find("type");
                    auto textIt = block.find("text");
                    if (typeIt != block.end() && typeIt->is_string() &&
                        typeIt->get<std::string>() == "text" && textIt != block.end() &&
                        textIt->is_string()) {
                        return LLMTextResult::success(trimmed(textIt->get<std::string>()));
                    }
                }
            }
        }
        return LLMTextResult::failure("Unexpected JSON response structure");
    }

    auto choicesIt = json.find("choices");
    if (choicesIt == json.end() || !choicesIt->is_array() || choicesIt->empty()) {
        return LLMTextResult::failure("Unexpected JSON response structure");
    }
    const Json& choice = choicesIt->front();
    if (!choice.is_object()) {
        return LLMTextResult::failure("Unexpected JSON response structure");
    }
    if (auto reasonIt = choice.find("finish_reason"); reasonIt != choice.end() && reasonIt->is_string()) {
        const std::string reason = reasonIt->get<std::string>();
        if (reason == "length" || reason == "max_tokens") {
            return LLMTextResult::truncated("The response reached the output token limit");
        }
    }
    if (auto messageIt = choice.find("message");
        messageIt != choice.end() && messageIt->is_object()) {
        if (auto contentIt = messageIt->find("content");
            contentIt != messageIt->end() && contentIt->is_string()) {
            return LLMTextResult::success(trimmed(contentIt->get<std::string>()));
        }
    }
    return LLMTextResult::failure("Unexpected JSON response structure");
}

LLMTextResult LLMTranslationResponseParser::extractOpenAISSE(const std::string& text) {
    std::string result;
    bool isTruncated = false;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        const std::string rawLine =
            text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = (end == std::string::npos) ? text.size() + 1 : end + 1;

        const std::string line = trimmed(rawLine);
        if (!hasPrefix(line, "data:") || line == "data: [DONE]") { continue; }
        std::string payload = trimmed(line.substr(5));
        Json json;
        try {
            json = Json::parse(payload);
        } catch (const Json::exception&) {
            continue;  // non-JSON data lines are skipped (Swift `try?` + continue)
        }
        if (!json.is_object()) { continue; }
        auto choicesIt = json.find("choices");
        if (choicesIt == json.end() || !choicesIt->is_array() || choicesIt->empty()) { continue; }
        const Json& choice = choicesIt->front();
        if (!choice.is_object()) { continue; }
        if (auto reasonIt = choice.find("finish_reason");
            reasonIt != choice.end() && reasonIt->is_string()) {
            const std::string reason = reasonIt->get<std::string>();
            isTruncated = isTruncated || reason == "length" || reason == "max_tokens";
        }
        if (auto deltaIt = choice.find("delta"); deltaIt != choice.end() && deltaIt->is_object()) {
            if (auto contentIt = deltaIt->find("content");
                contentIt != deltaIt->end() && contentIt->is_string()) {
                result += contentIt->get<std::string>();
            }
        } else if (auto textIt = choice.find("text");
                   textIt != choice.end() && textIt->is_string()) {
            result += textIt->get<std::string>();
        }
        if (end == std::string::npos) { break; }
    }
    if (isTruncated) {
        return LLMTextResult::truncated("The stream reached the output token limit");
    }
    if (result.empty()) { return LLMTextResult::failure("Empty text from SSE stream"); }
    return LLMTextResult::success(trimmed(result));
}

LLMTextResult LLMTranslationResponseParser::extractClaudeSSE(const std::string& text) {
    std::string result;
    bool isTruncated = false;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        const std::string rawLine =
            text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = (end == std::string::npos) ? text.size() + 1 : end + 1;

        const std::string line = trimmed(rawLine);
        if (!hasPrefix(line, "data:")) { continue; }
        const std::string payload = trimmed(line.substr(5));
        Json json;
        try {
            json = Json::parse(payload);
        } catch (const Json::exception&) {
            continue;
        }
        if (!json.is_object()) { continue; }
        if (auto stopIt = json.find("stop_reason");
            stopIt != json.end() && stopIt->is_string() &&
            stopIt->get<std::string>() == "max_tokens") {
            isTruncated = true;
        }
        if (auto deltaIt = json.find("delta"); deltaIt != json.end() && deltaIt->is_object()) {
            if (auto stopIt = deltaIt->find("stop_reason");
                stopIt != deltaIt->end() && stopIt->is_string() &&
                stopIt->get<std::string>() == "max_tokens") {
                isTruncated = true;
            }
            if (auto textIt = deltaIt->find("text");
                textIt != deltaIt->end() && textIt->is_string()) {
                result += textIt->get<std::string>();
            }
        }
        if (end == std::string::npos) { break; }
    }
    if (isTruncated) {
        return LLMTextResult::truncated("Claude stream reached the output token limit");
    }
    if (result.empty()) { return LLMTextResult::failure("Empty text from SSE stream"); }
    return LLMTextResult::success(trimmed(result));
}

}  // namespace tf::core
