// SmartRedactionTests.cpp — FR-10 parser boundaries + batch-transaction
// undo semantics. Mirrors the macOS behaviors fixed by
// PreviewViewModel+SmartRedact.swift (one undo step covers the batch) and
// SmartPrivacyRedactionService.swift (configuration gating, pixel rects,
// confidence >= 0.5, degenerate-box drop with CGRect.integral rounding).
#include <catch2/catch_amalgamated.hpp>

#include "Core/AnnotationDocument.h"
#include "tf/SmartRedaction.hpp"

#include <memory>

using namespace tf;

// tf::core::AnnotationDocument / Annotation / MosaicAnnotation / Uuid / Rect
// are referenced through the tf::core qualifier below (no using — tf::core::Uuid
// collides with tf::Uuid inside the test body).

namespace {

/// In-memory credential store (no CredMan dependency in Catch2).
struct MemStore final : ICredentialStore {
    std::map<std::string, StoredCredentials> data;
    void save(const std::string& account, const StoredCredentials& c) override { data[account] = c; }
    std::optional<StoredCredentials> load(const std::string& account) const override {
        auto it = data.find(account);
        return it == data.end() ? std::nullopt : std::optional<StoredCredentials>(it->second);
    }
    bool remove(const std::string& account) override { return data.erase(account) > 0; }
    bool has(const std::string& account) const override { return data.count(account) > 0; }
};

SmartRedactionEngine::Analyzer stubAnalyzer(std::string payload,
                                            std::optional<ProviderError> err = std::nullopt) {
    return [payload = std::move(payload), err](VlmProviderType, const std::string&, const OcrImage&,
                                               std::string& out) -> std::optional<ProviderError> {
        if (err) return err;
        out = payload;
        return std::nullopt;
    };
}

OcrImage fakeImage(int w = 1000, int h = 800) {
    OcrImage img;
    img.width = w;
    img.height = h;
    img.bgra.assign(static_cast<std::size_t>(w) * h * 4, '\0');
    return img;
}

/// Core-side batch apply (FR-10): regions → mosaics appended under ONE
/// AnnotationDocument history step, so one undo clears the whole batch.
/// App-side (PreviewWindow) mirrors this shape via addAll.
std::size_t applyRedactionsAsOneTransaction(tf::core::AnnotationDocument& doc,
                                            const std::vector<RedactionRegion>& regions,
                                            int blockSize) {
    std::vector<tf::core::Annotation> batch;
    batch.reserve(regions.size());
    for (const auto& r : regions) {
        batch.push_back(tf::core::Annotation::mosaic(tf::core::MosaicAnnotation(
            tf::core::Uuid::randomUuid(), tf::core::Rect{r.x, r.y, r.width, r.height}, blockSize)));
    }
    doc.addAll(std::move(batch));
    return regions.size();
}

}  // namespace

TEST_CASE("smart redaction: prompt pair matches macOS privacy template", "[redact][prompt]") {
    // Structural anchors from TransFrame/Services/VLMProvider.swift
    // VLMPromptPair.privacy — wording must track the macOS strings.
    const std::string& sys = SmartRedactionEngine::privacySystemPrompt();
    const std::string& user = SmartRedactionEngine::privacyUserPrompt();
    REQUIRE(sys.find("privacy detection assistant") != std::string::npos);
    REQUIRE(sys.find("phone numbers") != std::string::npos);
    REQUIRE(sys.find("faces") != std::string::npos);
    REQUIRE(sys.find("boundingBox") != std::string::npos);
    REQUIRE(user.find("If nothing sensitive is found") != std::string::npos);
    REQUIRE(user.find("{\"segments\":[]}") != std::string::npos);
}

TEST_CASE("smart redaction: unconfigured provider is a Configuration error",
          "[redact][config]") {
    // credentialAccount follows the EnginesPage settings-UI schema; the
    // legacy "vlm_api_key_<provider>" account (VlmOcrEngine's reader) also
    // enables redaction so keys written by either path work.
    REQUIRE(SmartRedactionEngine::credentialAccount(VlmProviderType::OpenAI) ==
            "TransFrame/vlm/openai");

    auto store = std::make_shared<MemStore>();
    SmartRedactionEngine engine(stubAnalyzer("{\"segments\":[]}"), store);
    REQUIRE_FALSE(engine.isConfigured(VlmProviderType::OpenAI));
    REQUIRE(engine.isConfigured(VlmProviderType::Ollama));

    const auto outcome = engine.detectRegions(fakeImage(), VlmProviderType::OpenAI);
    REQUIRE_FALSE(outcome.isOk());
    REQUIRE(outcome.error.category == ErrorCategory::Configuration);

    store->save("TransFrame/vlm/openai", StoredCredentials{"key", "", {}});
    REQUIRE(engine.isConfigured(VlmProviderType::OpenAI));
    store->remove("TransFrame/vlm/openai");
    REQUIRE_FALSE(engine.isConfigured(VlmProviderType::OpenAI));
    store->save("vlm_api_key_openai", StoredCredentials{"key", "", {}});
    REQUIRE(engine.isConfigured(VlmProviderType::OpenAI));
}

TEST_CASE("smart redaction: invalid image is a content error", "[redact]") {
    auto store = std::make_shared<MemStore>();
    store->save("vlm_api_key_openai", StoredCredentials{"k", "", {}});
    SmartRedactionEngine engine(stubAnalyzer("{}"), store);
    OcrImage bad;  // empty pixels
    const auto outcome = engine.detectRegions(bad, VlmProviderType::OpenAI);
    REQUIRE_FALSE(outcome.isOk());
    REQUIRE(outcome.error.category == ErrorCategory::Content);
}

TEST_CASE("smart redaction: analyzer transport errors surface unchanged", "[redact]") {
    auto store = std::make_shared<MemStore>();
    store->save("vlm_api_key_openai", StoredCredentials{"k", "", {}});
    SmartRedactionEngine engine(stubAnalyzer("", ProviderError::network("boom")), store);
    const auto outcome = engine.detectRegions(fakeImage(), VlmProviderType::OpenAI);
    REQUIRE_FALSE(outcome.isOk());
    REQUIRE(outcome.error.category == ErrorCategory::Network);
}

TEST_CASE("smart redaction: plain segments array parses to pixel rects", "[redact][parse]") {
    const std::string payload = R"({"segments":[
        {"text":"phone","boundingBox":{"x":0.1,"y":0.2,"width":0.3,"height":0.1},"confidence":0.9},
        {"text":"face","boundingBox":{"x":0.5,"y":0.5,"width":0.25,"height":0.25},"confidence":0.9}
    ]})";
    auto store = std::make_shared<MemStore>();
    store->save("vlm_api_key_openai", StoredCredentials{"k", "", {}});
    SmartRedactionEngine engine(stubAnalyzer(payload), store);
    const auto outcome = engine.detectRegions(fakeImage(1000, 800), VlmProviderType::OpenAI);
    REQUIRE(outcome.isOk());
    REQUIRE(outcome.value.size() == 2);
    REQUIRE(outcome.value[0].category == "phone");
    REQUIRE(outcome.value[0].x == Catch::Approx(100.0));
    REQUIRE(outcome.value[0].y == Catch::Approx(160.0));
    REQUIRE(outcome.value[0].width == Catch::Approx(300.0));
    REQUIRE(outcome.value[0].height == Catch::Approx(80.0));
    REQUIRE(outcome.value[1].width == Catch::Approx(250.0));
}

TEST_CASE("smart redaction: chat-completion content and json fences unwrap", "[redact][parse]") {
    const std::string payload =
        R"({"choices":[{"message":{"content":"```json\n{\"segments\":[{\"text\":\"email\",\"boundingBox\":{\"x\":0.0,\"y\":0.0,\"width\":0.5,\"height\":0.5},\"confidence\":0.7}]}\n```"}}]})";
    const auto regions = SmartRedactionEngine::parseRegions(payload, 100, 100);
    REQUIRE(regions.size() == 1);
    REQUIRE(regions[0].category == "email");
    REQUIRE(regions[0].width == Catch::Approx(50.0));
}

TEST_CASE("smart redaction: empty and unparseable responses yield no regions",
          "[redact][parse]") {
    REQUIRE(SmartRedactionEngine::parseRegions("", 100, 100).empty());
    REQUIRE(SmartRedactionEngine::parseRegions("not json at all", 100, 100).empty());
    REQUIRE(SmartRedactionEngine::parseRegions("{\"segments\":[]}", 100, 100).empty());
    REQUIRE(SmartRedactionEngine::parseRegions("[]", 100, 100).empty());
}

TEST_CASE("smart redaction: truncation tolerance keeps complete leading boxes",
          "[redact][parse]") {
    // VLM cut mid-array: the last object is incomplete; the leading complete
    // boxes must still land (no exception, no full-drop).
    const std::string payload =
        R"({"segments":[{"text":"a","boundingBox":{"x":0.1,"y":0.1,"width":0.1,"height":0.1},"confidence":0.9},{"text":"b","boundingBox":{"x":0.5,)";
    const auto regions = SmartRedactionEngine::parseRegions(payload, 1000, 1000);
    REQUIRE(regions.size() == 1);
    REQUIRE(regions[0].category == "a");
}

TEST_CASE("smart redaction: truncation salvage ignores braces inside strings", "[redact][parse]") {
    const std::string payload =
        R"({"segments":[{"text":"a } [ \" b","boundingBox":{"x":0.1,"y":0.1,"width":0.1,"height":0.1},"confidence":0.9},{"text":)";
    const auto regions = SmartRedactionEngine::parseRegions(payload, 100, 100);
    REQUIRE(regions.size() == 1);
    REQUIRE(regions[0].category == "a } [ \" b");
}

TEST_CASE("smart redaction: out-of-range boxes clamp then drop when degenerate",
          "[redact][parse]") {
    const std::string payload = R"({"segments":[
        {"text":"partial","boundingBox":{"x":0.9,"y":0.9,"width":0.5,"height":0.5},"confidence":0.9},
        {"text":"outside","boundingBox":{"x":1.2,"y":1.2,"width":0.5,"height":0.5},"confidence":0.9},
        {"text":"neg","boundingBox":{"x":-0.5,"y":-0.5,"width":0.6,"height":0.6},"confidence":0.9}
    ]})";
    const auto regions = SmartRedactionEngine::parseRegions(payload, 1000, 1000);
    REQUIRE(regions.size() == 2);
    // partial: clamped to the visible 10% strip.
    REQUIRE(regions[0].x == Catch::Approx(900.0));
    REQUIRE(regions[0].width == Catch::Approx(100.0));
    // outside: clamps to zero size → dropped. neg: clamps origin to 0 with a
    // 10% box.
    REQUIRE(regions[1].category == "neg");
    REQUIRE(regions[1].x == 0.0);
    REQUIRE(regions[1].width == Catch::Approx(100.0));
}

TEST_CASE("smart redaction: NaN boxes are dropped, low confidence filtered", "[redact][parse]") {
    const std::string payload = R"({"segments":[
        {"text":"nan","boundingBox":{"x":NaN,"y":0,"width":0.5,"height":0.5},"confidence":0.9},
        {"text":"lowconf","boundingBox":{"x":0.1,"y":0.1,"width":0.2,"height":0.2},"confidence":0.3}
    ]})";
    SmartRedactionOptions opts;
    opts.minConfidence = 0.5;
    const auto regions = SmartRedactionEngine::parseRegions(payload, 100, 100, opts);
    REQUIRE(regions.empty());
}

TEST_CASE("smart redaction: padding option stays inside the image", "[redact][parse]") {
    const std::string payload =
        R"({"segments":[{"text":"edge","boundingBox":{"x":0.0,"y":0.0,"width":0.1,"height":0.1},"confidence":0.9}]})";
    SmartRedactionOptions opts;
    opts.padPx = 25.0;
    const auto regions = SmartRedactionEngine::parseRegions(payload, 1000, 1000, opts);
    REQUIRE(regions.size() == 1);
    REQUIRE(regions[0].x == 0.0);                      // padding would go negative
    REQUIRE(regions[0].width == Catch::Approx(125.0)); // left padding clipped; right edge = 100 + 25
    REQUIRE(regions[0].height == Catch::Approx(125.0));
}

TEST_CASE("smart redaction: batch apply is one undo step (FR-10)", "[redact][undo]") {
    const std::string payload = R"({"segments":[
        {"text":"a","boundingBox":{"x":0.1,"y":0.1,"width":0.2,"height":0.2},"confidence":0.9},
        {"text":"b","boundingBox":{"x":0.5,"y":0.5,"width":0.2,"height":0.2},"confidence":0.9},
        {"text":"c","boundingBox":{"x":0.7,"y":0.7,"width":0.2,"height":0.2},"confidence":0.9}
    ]})";
    const auto regions = SmartRedactionEngine::parseRegions(payload, 1000, 1000);

    tf::core::AnnotationDocument doc;
    doc.add(tf::core::Annotation::mosaic(tf::core::MosaicAnnotation(
        tf::core::Uuid::randomUuid(), tf::core::Rect{0, 0, 20, 20}, 10))); // pre-existing
    applyRedactionsAsOneTransaction(doc, regions, /*blockSize=*/12);

    REQUIRE(doc.annotations().size() == 4);
    for (std::size_t i = 1; i < 4; ++i) {
        const auto& m = std::get<tf::core::MosaicAnnotation>(doc.annotations()[i].payload);
        REQUIRE(m.blockSize == 12);
    }

    doc.undo(); // Ctrl+Z once → whole batch gone, pre-existing survives
    REQUIRE(doc.annotations().size() == 1);
    doc.undo();
    REQUIRE(doc.annotations().empty());
    doc.redo(); // redo restores the batch as one step too
    REQUIRE(doc.annotations().size() == 1);
    doc.redo();
    REQUIRE(doc.annotations().size() == 4);
}

TEST_CASE("smart redaction: empty batch pushes no history entry", "[redact][undo]") {
    // macOS guard: `rects.isEmpty` returns BEFORE pushUndoState — the App
    // side owns that gate; Core documents addAll-with-empty pushing nothing.
    tf::core::AnnotationDocument doc;
    doc.add(tf::core::Annotation::mosaic(tf::core::MosaicAnnotation(
        tf::core::Uuid::randomUuid(), tf::core::Rect{0, 0, 20, 20}, 10)));
    doc.addAll({});
    REQUIRE(doc.annotations().size() == 1);
    REQUIRE(doc.canUndo()); // only the original add()
    doc.undo();
    REQUIRE(doc.annotations().empty());
    REQUIRE_FALSE(doc.canUndo());
}
