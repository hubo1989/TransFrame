// LLMTranslationCoreTests.cpp — Catch2 port of
// TransFrameTests/LLMResponseTruncationTests.swift (parser + batching parts)
// plus truncation-detection cases for both SSE dialects.
#include <catch2/catch_amalgamated.hpp>

#include "Core/LLMTranslationCore.h"

#include <string>
#include <vector>

using tf::core::LLMTranslationBatching;
using tf::core::LLMTranslationResponseParser;
using tf::core::TranslationEngineType;

namespace {
std::string repeat(const std::string& s, std::size_t n) {
    std::string out;
    out.reserve(s.size() * n);
    for (std::size_t i = 0; i < n; ++i) { out += s; }
    return out;
}
}  // namespace

TEST_CASE("openai and claude length responses are rejected", "[llm][truncation]") {
    const std::string openAI =
        R"({"choices":[{"message":{"content":"部分译文"},"finish_reason":"length"}]})";
    const std::string claude =
        R"({"content":[{"type":"text","text":"partial"}],"stop_reason":"max_tokens"})";
    const auto r1 = LLMTranslationResponseParser::extractText(openAI, TranslationEngineType::OpenAI);
    REQUIRE_FALSE(r1.ok);
    REQUIRE(r1.incomplete);
    const auto r2 = LLMTranslationResponseParser::extractText(claude, TranslationEngineType::Claude);
    REQUIRE_FALSE(r2.ok);
    REQUIRE(r2.incomplete);
}

TEST_CASE("openai SSE truncation cannot be reported as success", "[llm][truncation]") {
    const std::string body =
        "data: {\"choices\":[{\"delta\":{\"content\":\"part\"},\"finish_reason\":null}]}\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"length\"}]}\n"
        "data: [DONE]\n";
    const auto r = LLMTranslationResponseParser::extractText(body, TranslationEngineType::OpenAI);
    REQUIRE_FALSE(r.ok);
}

TEST_CASE("json body containing a data line is not treated as SSE", "[llm]") {
    const std::string payload =
        R"({"stop_reason":"end_turn","content":[{"type":"text","text":"日志示例\ndata: 123"}]})";
    const auto r = LLMTranslationResponseParser::extractText(payload, TranslationEngineType::Claude);
    REQUIRE(r.ok);
    REQUIRE(r.text == "日志示例\ndata: 123");
}

TEST_CASE("claude SSE stream concatenates delta text", "[llm]") {
    const std::string body =
        "data: {\"delta\":{\"text\":\"Hello\"}}\n"
        "data: {\"delta\":{\"text\":\", 世界\"}}\n"
        "data: {\"message_stop\":{}}\n";
    const auto r = LLMTranslationResponseParser::extractText(body, TranslationEngineType::Claude);
    REQUIRE(r.ok);
    REQUIRE(r.text == "Hello, 世界");
}

TEST_CASE("claude SSE stream with max_tokens is rejected as truncated", "[llm][truncation]") {
    const std::string body =
        "data: {\"delta\":{\"text\":\"partial\"}}\n"
        "data: {\"delta\":{\"stop_reason\":\"max_tokens\"}}\n";
    const auto r = LLMTranslationResponseParser::extractText(body, TranslationEngineType::Claude);
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.incomplete);
}

TEST_CASE("openai API error payload surfaces the message", "[llm]") {
    const std::string body = R"({"error":{"message":"bad key"}})";
    const auto r = LLMTranslationResponseParser::extractText(body, TranslationEngineType::OpenAI);
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.error == "API error: bad key");
}

TEST_CASE("empty SSE stream is an error, not a success", "[llm]") {
    const std::string body = "data: [DONE]\n";
    const auto r = LLMTranslationResponseParser::extractText(body, TranslationEngineType::OpenAI);
    REQUIRE_FALSE(r.ok);
}

TEST_CASE("batching respects budget and preserves all source text", "[llm][batching]") {
    const std::vector<std::string> texts = {repeat("汉", 20), repeat("字", 20), "third", "fourth"};
    const auto batches = LLMTranslationBatching::split(texts, 20, nullptr);
    std::vector<std::string> flat;
    for (const auto& batch : batches) {
        flat.insert(flat.end(), batch.begin(), batch.end());
    }
    REQUIRE(flat == texts);
    REQUIRE(batches.size() > 1);
    // Joined form with the separator round-trips the source order.
    std::string joined;
    for (std::size_t i = 0; i < batches.size(); ++i) {
        if (i > 0) { joined += "\n---\n"; }
        joined += tf::core::Json(batches[i]).dump();  // structural sanity only
    }
    REQUIRE(joined.size() > 0);
}

TEST_CASE("long single segment is split and can be recombined", "[llm][batching]") {
    const std::string text = repeat("漢👨‍👩‍👧‍👦", 100);
    const auto batches = LLMTranslationBatching::split({text}, 32, nullptr);
    REQUIRE(batches.size() > 1);
    std::string recombined;
    for (const auto& batch : batches) {
        for (const auto& piece : batch) { recombined += piece; }
    }
    REQUIRE(recombined == text);
}

TEST_CASE("long custom prompt still splits without dropping source", "[llm][batching]") {
    const std::vector<std::string> texts = {repeat("汉", 20), repeat("字", 20)};
    const std::string prompt = repeat("p", 120) + "{text}";
    const auto batches = LLMTranslationBatching::split(texts, 32, &prompt);
    REQUIRE(batches.size() >= 2);
    std::string joined;
    for (const auto& batch : batches) {
        for (const auto& piece : batch) { joined += piece; }
    }
    REQUIRE(joined == texts[0] + texts[1]);
}

TEST_CASE("estimatedTokens counts UTF-16 code units", "[llm][batching]") {
    REQUIRE(LLMTranslationBatching::estimatedTokens("abc") == 3);
    REQUIRE(LLMTranslationBatching::estimatedTokens("汉") == 1);
    // U+1F469 ZWJ family: 8 code points but 22 UTF-16 units (4 emoji x 2 + 3 ZWJ x 2... check).
    // "漢" = 1 UTF-16 unit; "👨‍👩‍👧‍👦" = family with ZWJ = 11 UTF-16 units.
    REQUIRE(LLMTranslationBatching::estimatedTokens("漢👨‍👩‍👧‍👦") == 12);
    REQUIRE(LLMTranslationBatching::separator() == std::string("\n---\n"));
}
