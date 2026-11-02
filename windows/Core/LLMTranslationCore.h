#pragma once
// LLMTranslationCore.h — pure-logic port of Swift LLMTranslationBatching.swift
// + LLMTranslationResponseParser.swift (same semantics, same test vectors as
// TransFrameTests/LLMResponseTruncationTests.swift).

#include "Json.h"
#include "TranslationEngineType.h"

#include <string>
#include <vector>

namespace tf::core {

/// Expected-like result union (avoids exceptions so providers compose cleanly).
struct LLMTextResult {
    bool ok = false;
    std::string text;   // when ok
    bool incomplete = false;  // truncation error (Swift incompleteResponse)
    std::string error;  // message when !ok

    static LLMTextResult success(std::string t) {
        LLMTextResult r;
        r.ok = true;
        r.text = std::move(t);
        return r;
    }
    static LLMTextResult truncated(std::string msg) {
        LLMTextResult r;
        r.ok = false;
        r.incomplete = true;
        r.error = std::move(msg);
        return r;
    }
    static LLMTextResult failure(std::string msg) {
        LLMTextResult r;
        r.ok = false;
        r.error = std::move(msg);
        return r;
    }
};

/// Batch splitter ahead of the output-token budget; long fragments are cut by
/// characters (never code points) and order is preserved.
struct LLMTranslationBatching {
    static const char* separator();  // "\n---\n"

    /// Swift split(texts:maxOutputTokens:promptTemplate:) — template defaults
    /// to "" when not supplied (nullopt).
    static std::vector<std::vector<std::string>> split(
        const std::vector<std::string>& texts, std::int64_t maxOutputTokens,
        const std::string* promptTemplate);

    /// Swift estimatedTokens: UTF-16 code-unit count.
    static std::int64_t estimatedTokens(const std::string& text);

private:
    static std::vector<std::string> splitLongText(const std::string& text, std::int64_t budget);
};

/// Shared completion-state parsing for OpenAI-compatible and Claude bodies
/// (plain JSON and SSE streams). A body only counts as SSE when the first
/// payload line starts with "data:" — bodies whose content contains "\ndata:"
/// still parse as plain JSON.
struct LLMTranslationResponseParser {
    static LLMTextResult extractText(const std::string& body, TranslationEngineType engine);

private:
    static LLMTextResult extractOpenAISSE(const std::string& text);
    static LLMTextResult extractClaudeSSE(const std::string& text);
};

}  // namespace tf::core
