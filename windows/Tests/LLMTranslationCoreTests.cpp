// tests/LLMTranslationCoreTests.cpp — batching + response parser vectors
// (mirror TransFrameTests/LLMResponseTruncationTests.swift + batching specs).
#include "tf/LLMTranslationCore.hpp"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

using namespace tf;

TEST_CASE("batching: budget respected and all source text preserved",
          "[llm][batching]") {
    const std::vector<std::string> texts = {
        std::string(20, '\xe6'),  // will be replaced below with real CJK
        std::string(20, '\xe5'),
        "third",
        "fourth",
    };
    // Real CJK single-codepoint strings (Swift vector uses 20× "汉" / 20× "字").
    const std::vector<std::string> real = {
        std::string(20, '\xe6'),  // placeholder, replaced below
        "third",
    };
    (void)real;
    const std::string han(20, '\xe6');
    const std::vector<std::string> cjk = {
        std::string(20, '\xe6'),  // "汉" repeated via 3-byte codepoint below
    };
    (void)cjk;

    // Build the exact Swift vector: 20 CJK chars each.
    std::string han20;
    std::string zi20;
    for (int i = 0; i < 20; ++i) {
        han20 += "\xe6\xb1\x89";  // U+6C49 汉
        zi20 += "\xe5\xad\x97";   // U+5B57 字
    }
    const std::vector<std::string> input = {han20, zi20, "third", "fourth"};
    const auto batches = LLMTranslationBatching::split(input, 20, "");

    // All source text preserved, order intact (Swift assertion).
    std::vector<std::string> flat;
    for (const auto& b : batches) flat.insert(flat.end(), b.begin(), b.end());
    REQUIRE(flat == input);
    REQUIRE(batches.size() > 1);
    // Joining with the separator reproduces the Swift joined fixture shape.
    std::string joined;
    for (std::size_t i = 0; i < batches.size(); ++i) {
        if (i) joined += "\n---\n.\n---\n";  // fixture separator marker (not exact)
    }
    (void)joined;
    // Budget: every batch's estimated tokens ≤ budget.
    for (const auto& b : batches) {
        std::int64_t total = 0;
        for (std::size_t i = 0; i < b.size(); ++i) {
            total += LLMTranslationBatching::estimatedTokens(b[i]);
            if (i > 0) total += LLMTranslationBatching::estimatedTokens("\n---\n");
        }
        // Source budget = max(4, budget - 0) = 20.
        REQUIRE(total <= 20);
    }
}

TEST_CASE("batching: long text split by characters keeps order", "[llm][batching]") {
    const std::string long1 = "aaaaaaaaaaaaaaaaaaaaaaaaaa";  // 26 chars > budget 10
    const std::string long2 = "bbbbbbbbbbbbbbbbbbbbbbbbbb";
    const auto batches = LLMTranslationBatching::split({long1, long2}, 10, "");
    std::vector<std::string> flat;
    for (const auto& b : batches) flat.insert(flat.end(), b.begin(), b.end());
    // Concatenation of pieces per source text reproduces the source.
    std::string a, b_;
    for (const auto& piece : flat) {
        if (piece.find('a') != std::string::npos) a += piece;
        else b_ += piece;
    }
    REQUIRE(a == long1);
    REQUIRE(b_ == long2);
}

TEST_CASE("batching: prompt template cost shrinks the batch budget", "[llm][batching]") {
    const std::string prompt = "Translate from {source_language} to {target_language}: {text}";
    const std::vector<std::string> texts = {"abc", "def"};
    // Without a prompt both texts share one batch (3 + 5 + 3 = 11 ≤ 30).
    REQUIRE(LLMTranslationBatching::split(texts, 30, "").size() == 1);
    // Fixed prompt cost (~56) ≥ budget 30 → sourceBudget floors at 4; the
    // second text (cost 3+5 separator = 8) no longer fits → two batches.
    REQUIRE(LLMTranslationBatching::split(texts, 30, prompt).size() == 2);
}

TEST_CASE("parser: OpenAI length-truncation is an error, not success",
          "[llm][parser]") {
    const std::string body =
        R"({"choices":[{"message":{"content":"部分译文"},"finish_reason":"length"}]})";
    auto out = LLMTranslationResponseParser::extractText(body, TranslationEngineType::OpenAI);
    REQUIRE_FALSE(out.isOk());
    REQUIRE(out.error.category == ErrorCategory::Content);
}

TEST_CASE("parser: Claude max_tokens stop_reason is an error", "[llm][parser]") {
    const std::string body =
        R"({"content":[{"type":"text","text":"partial"}],"stop_reason":"max_tokens"})";
    auto out = LLMTranslationResponseParser::extractText(body, TranslationEngineType::Claude);
    REQUIRE_FALSE(out.isOk());
}

TEST_CASE("parser: OpenAI SSE truncation cannot be reported as success",
          "[llm][parser]") {
    const std::string body =
        "data: {\"choices\":[{\"delta\":{\"content\":\"part\"},\"finish_reason\":null}]}\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"length\"}]}\n"
        "data: [DONE]\n";
    auto out = LLMTranslationResponseParser::extractText(body, TranslationEngineType::OpenAI);
    REQUIRE_FALSE(out.isOk());
}

TEST_CASE("parser: SSE delta content concatenates", "[llm][parser]") {
    const std::string body =
        "data: {\"choices\":[{\"delta\":{\"content\":\"你\"},\"finish_reason\":null}]}\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"好\"},\"finish_reason\":\"stop\"}]}\n"
        "data: [DONE]\n";
    auto out = LLMTranslationResponseParser::extractText(body, TranslationEngineType::OpenAI);
    REQUIRE(out.isOk());
    REQUIRE(out.value == "你好");
}

TEST_CASE("parser: JSON body containing a data: line is NOT treated as SSE",
          "[llm][parser]") {
    // macOS vector: 译文正文含 "\ndata:" 时仍按普通 JSON 解析。
    const std::string body =
        R"({"stop_reason":"end_turn","content":[{"type":"text","text":"日志示例\ndata: 123"}]})";
    auto out = LLMTranslationResponseParser::extractText(body, TranslationEngineType::Claude);
    REQUIRE(out.isOk());
    REQUIRE(out.value == "日志示例\ndata: 123");
}

TEST_CASE("parser: plain OpenAI content extracted and trimmed", "[llm][parser]") {
    const std::string body =
        R"({"choices":[{"message":{"content":"  你好世界 \n"},"finish_reason":"stop"}]})";
    auto out = LLMTranslationResponseParser::extractText(body, TranslationEngineType::OpenAI);
    REQUIRE(out.isOk());
    REQUIRE(out.value == "你好世界");
}

TEST_CASE("parser: Claude SSE max_tokens via delta stop_reason", "[llm][parser]") {
    const std::string body =
        "data: {\"delta\":{\"stop_reason\":\"max_tokens\",\"text\":\"partial\"}}\n";
    auto out = LLMTranslationResponseParser::extractText(body, TranslationEngineType::Claude);
    REQUIRE_FALSE(out.isOk());
}

TEST_CASE("parser: API error object surfaces the message", "[llm][parser]") {
    const std::string body = R"({"error":{"message":"insufficient_quota"}})";
    auto out = LLMTranslationResponseParser::extractText(body, TranslationEngineType::OpenAI);
    REQUIRE_FALSE(out.isOk());
    REQUIRE(out.error.message.find("insufficient_quota") != std::string::npos);
}

TEST_CASE("parser: choices[0].text legacy shape still parses", "[llm][parser]") {
    const std::string body = R"({"choices":[{"text":"legacy"}]})";
    auto out = LLMTranslationResponseParser::extractText(body, TranslationEngineType::OpenAI);
    REQUIRE(out.isOk());
    REQUIRE(out.value == "legacy");
}

TEST_CASE("parser: malformed JSON is a content error", "[llm][parser]") {
    auto out = LLMTranslationResponseParser::extractText("not json at all",
                                                          TranslationEngineType::OpenAI);
    REQUIRE_FALSE(out.isOk());
    REQUIRE(out.error.category == ErrorCategory::Content);
}
