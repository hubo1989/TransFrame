// tf/LLMTranslationCore.hpp
// Pure-logic port of Swift LLMTranslationBatching.swift +
// LLMTranslationResponseParser.swift (byte-for-byte semantics; test vectors
// mirror TransFrameTests/LLMResponseTruncationTests.swift).
#ifndef TF_LLM_TRANSLATION_CORE_HPP
#define TF_LLM_TRANSLATION_CORE_HPP

#include "tf/CoreModels.hpp"

#include <type_traits>
#include <string>
#include <vector>

namespace tf {

/// Outcome<T, E = ProviderError> — small expected-like union (avoids exceptions
/// so providers compose cleanly in Catch2 tests). Declared before use.
template <typename T, typename E = ProviderError>
struct Outcome {
    static_assert(std::is_default_constructible_v<T>, "Outcome value must be default constructible");
    static_assert(std::is_default_constructible_v<E>, "Outcome error must be default constructible");

    bool okFlag = false;
    T value{};
    E error{};

    static Outcome makeOk(T v) {
        Outcome o;
        o.okFlag = true;
        o.value = std::move(v);
        return o;
    }
    static Outcome makeError(E e) {
        Outcome o;
        o.okFlag = false;
        o.error = std::move(e);
        return o;
    }
    bool isOk() const { return okFlag; }
};

/// Batch splitter ahead of the output-token budget; long fragments are cut by
/// characters (not spaces) and order is preserved.
struct LLMTranslationBatching {
    static const char* separator();  // "\n---\n"

    static std::vector<std::vector<std::string>> split(const std::vector<std::string>& texts,
                                                       std::int64_t maxOutputTokens,
                                                       const std::string& promptTemplate);

    static std::int64_t estimatedTokens(const std::string& text);

private:
    static std::vector<std::string> splitLongText(const std::string& text, std::int64_t budget);
};

/// Shared completion-state parsing for OpenAI-compatible and Claude bodies
/// (plain JSON and SSE streams).
struct LLMTranslationResponseParser {
    /// engine < 0 → treat as "custom" (OpenAI-compatible) — Swift .custom path.
    static Outcome<std::string, ProviderError> extractText(const std::string& body,
                                                            TranslationEngineType engine);

private:
    static Outcome<std::string, ProviderError> extractOpenAISSE(const std::string& text);
    static Outcome<std::string, ProviderError> extractClaudeSSE(const std::string& text);
};

}  // namespace tf

#endif  // TF_LLM_TRANSLATION_CORE_HPP
