// CrossPlatformSnapshotTests.cpp — PRD §11 跨平台快照门禁: shared test-vector
// JSON (windows/Tests/fixtures/crossplatform/, extracted verbatim from the
// macOS TransFrameTests sources named in each file's _source key) driven
// through the Windows Core implementations with FULL-field equality —
// bbox, confidence, text and order are compared for every output item,
// never sampled. macOS wiring point: each fixture's _macOSWiringNote names
// the future Swift-side loader; until then the embedded Swift vectors are
// the source of truth and both sides must move in lock-step.
#include <catch2/catch_amalgamated.hpp>

#include "Core/CaptureDocument.h"
#include "Core/Json.h"
#include "Core/TranslationPromptConfig.h"
#include "Core/TranslationScene.h"
#include "Core/TranslationEngineType.h"
#include "Core/VisualLineGrouping.h"
#include "TestSupport.hpp"
#include "tf/CoreModels.hpp"
#include "tf/LLMTranslationCore.hpp"

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace tf;
using namespace tf::tests;  // MockTranslationProvider + mockDeps (TestSupport)
namespace cfg = tf::core;

namespace {

/// Fixture resolution, in order: cwd-relative (run from windows/ or repo
/// root), then the MSBuild output layout (fixtures/ next to the exe).
bool openFixture(const char* name, std::ifstream& in) {
    const std::string base = "crossplatform/";
    const std::string file = std::string(name);
    for (const std::string& path :
         {std::string("Tests/fixtures/") + base + file,
          std::string("windows/Tests/fixtures/") + base + file,
          std::string("fixtures/") + base + file, base + file, file}) {
        in.open(path);
        if (in) return true;
    }
    return false;
}

cfg::Json loadFixture(const char* name) {
    std::ifstream in;
    if (!openFixture(name, in)) {
        FAIL("crossplatform fixture not found: " << name);
        return cfg::Json{};
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return cfg::Json::parse(buffer.str());
}

cfg::TextSegment segFromJson(const cfg::Json& s) {
    cfg::TextSegment out;
    out.id = cfg::Uuid::randomUuid();
    out.text = s.at("text").get<std::string>();
    out.boundingBox = cfg::Rect{s.at("x").get<double>(), s.at("y").get<double>(),
                                s.at("width").get<double>(), s.at("height").get<double>()};
    out.confidence = s.value("confidence", 1.0f);
    out.isUserEdited = s.value("isUserEdited", false);
    return out;
}

/// Full-field equality for one merged segment: text, all four box edges
/// (macOS accuracy 1e-9), confidence, isUserEdited — and the caller checks
/// list position, so order is covered too.
void requireSegmentEquals(const cfg::Json& expected, const cfg::TextSegment& actual) {
    INFO("text: " << actual.text);
    CHECK(actual.text == expected.at("text").get<std::string>());
    CHECK(actual.boundingBox.minX() ==
          Catch::Approx(expected.at("minX").get<double>()).epsilon(1e-9));
    CHECK(actual.boundingBox.maxX() ==
          Catch::Approx(expected.at("maxX").get<double>()).epsilon(1e-9));
    CHECK(actual.boundingBox.minY() ==
          Catch::Approx(expected.at("minY").get<double>()).epsilon(1e-9));
    CHECK(actual.boundingBox.maxY() ==
          Catch::Approx(expected.at("maxY").get<double>()).epsilon(1e-9));
    CHECK(actual.confidence ==
          Catch::Approx(expected.at("confidence").get<double>()).epsilon(1e-6));
    CHECK(actual.isUserEdited == expected.at("isUserEdited").get<bool>());
}

}  // namespace

// ===========================================================================
// VisualLineGrouping — merge vectors (macOS VisualLineGroupingTests.swift)
// ===========================================================================
TEST_CASE("snapshot: visual-line-grouping vectors reproduce macOS output "
          "field-for-field",
          "[crossplatform][visual-line-grouping]") {
    const auto fixture = loadFixture("visual-line-grouping.json");
    REQUIRE(fixture.contains("cases"));
    std::size_t verified = 0;
    for (const auto& testCase : fixture.at("cases")) {
        INFO("case: " << testCase.at("name").get<std::string>());
        std::vector<cfg::TextSegment> input;
        for (const auto& s : testCase.at("input")) { input.push_back(segFromJson(s)); }
        const auto merged = cfg::VisualLineGrouping::mergeLines(input);
        const auto& expected = testCase.at("expected");
        REQUIRE(merged.size() == expected.size());
        for (std::size_t i = 0; i < merged.size(); ++i) {
            INFO("segment " << i);
            requireSegmentEquals(expected.at(i), merged[i]);
        }
        ++verified;
    }
    CHECK(verified == 12);

    // joinText pairs (glue rules).
    std::size_t joins = 0;
    for (const auto& joinCase : fixture.at("joinTextCases")) {
        INFO("join: " << joinCase.at("source").get<std::string>());
        CHECK(cfg::VisualLineGrouping::joinText(
                  joinCase.at("lhs").get<std::string>(), joinCase.at("rhs").get<std::string>(),
                  joinCase.value("gluesDirectly", false)) ==
              joinCase.at("expected").get<std::string>());
        ++joins;
    }
    CHECK(joins == 5);
}

// ===========================================================================
// LLM batching + response parsing (macOS LLMResponseTruncationTests.swift)
// ===========================================================================
TEST_CASE("snapshot: LLM batching vectors split identically to macOS",
          "[crossplatform][llm]") {
    const auto fixture = loadFixture("llm-response-truncation.json");
    std::size_t verified = 0;
    for (const auto& testCase : fixture.at("batchingCases")) {
        INFO("case: " << testCase.at("name").get<std::string>());
        std::vector<std::string> texts;
        for (const auto& t : testCase.at("texts")) {
            std::string text = t.get<std::string>();
            // Reviewability sentinel for the surrogate-pair vector: expand it
            // here so the JSON stays diffable (exact code points below).
            if (text.rfind("<U+6F22", 0) == 0) {
                text.clear();
                const std::string family =
                    "\xe6\xbc\xa2"                          // 漢 U+6F22
                    "\xf0\x9f\x91\xa8\xe2\x80\x8d"           // 👨 ZWJ
                    "\xf0\x9f\x91\xa9\xe2\x80\x8d"           // 👩 ZWJ
                    "\xf0\x9f\x91\xa7\xe2\x80\x8d"           // 👧 ZWJ
                    "\xf0\x9f\x91\xa6";                      // 👦
                for (int i = 0; i < 100; ++i) { text += family; }
            }
            texts.push_back(std::move(text));
        }
        const auto batches = LLMTranslationBatching::split(
            texts, testCase.at("maxOutputTokens").get<std::int64_t>(),
            testCase.at("promptTemplate").get<std::string>());

        if (testCase.contains("expectedBatches")) {
            // Exact batch list: contents AND order (not a property probe).
            const auto expected =
                testCase.at("expectedBatches").get<std::vector<std::vector<std::string>>>();
            REQUIRE(batches.size() == expected.size());
            for (std::size_t b = 0; b < batches.size(); ++b) {
                INFO("batch " << b);
                REQUIRE(batches[b] == expected[b]);
            }
        }
        if (testCase.contains("expectedMinBatches")) {
            REQUIRE(batches.size() >=
                    static_cast<std::size_t>(testCase.at("expectedMinBatches").get<int>()));
        }
        if (testCase.value("expectedFlatJoinEqualsInput", false)) {
            // macOS assertion: concatenating the pieces (no separators)
            // reproduces the source exactly — nothing dropped or reordered.
            std::string flat;
            for (const auto& batch : batches) {
                for (const auto& piece : batch) { flat += piece; }
            }
            REQUIRE(flat == texts[0]);
        }
        ++verified;
    }
    CHECK(verified == 3);
}

TEST_CASE("snapshot: LLM response parser vectors classify identically to macOS",
          "[crossplatform][llm]") {
    const auto fixture = loadFixture("llm-response-truncation.json");
    std::size_t verified = 0;
    for (const auto& testCase : fixture.at("parserCases")) {
        INFO("case: " << testCase.at("name").get<std::string>());
        const auto engineRaw = testCase.at("engine").get<std::string>();
        const auto engineOpt = translationEngineFromRaw(engineRaw);
        REQUIRE(engineOpt.has_value());
        const auto out = LLMTranslationResponseParser::extractText(
            testCase.at("body").get<std::string>(), *engineOpt);
        REQUIRE(out.isOk() == testCase.at("expectOk").get<bool>());
        if (out.isOk()) {
            // Full extracted text, byte-exact.
            REQUIRE(out.value == testCase.at("expectedText").get<std::string>());
        } else {
            REQUIRE(std::string(errorCategoryName(out.error.category)) ==
                    testCase.at("expectedErrorCategory").get<std::string>());
        }
        ++verified;
    }
    CHECK(verified == 4);
}

// ===========================================================================
// Prompt placeholder expansion (macOS TranslationConfigurationTests.swift)
// ===========================================================================
TEST_CASE("snapshot: prompt resolution + substitution matches macOS byte-for-byte",
          "[crossplatform][prompt]") {
    const auto fixture = loadFixture("translation-configuration.json");
    std::size_t verified = 0;
    for (const auto& testCase : fixture.at("cases")) {
        INFO("case: " << testCase.at("name").get<std::string>());
        cfg::TranslationPromptConfig config;
        for (const auto& pair : testCase.at("enginePrompts")) {
            const auto engine = cfg::fromRaw(cfg::TranslationEngineType{},
                                             pair.at(0).get<std::string>());
            REQUIRE(engine != cfg::TranslationEngineType::Invalid);
            config.enginePrompts[engine] = pair.at(1).get<std::string>();
        }
        for (const auto& pair : testCase.at("scenePrompts")) {
            const auto scene =
                cfg::fromRaw(cfg::TranslationScene{}, pair.at(0).get<std::string>());
            REQUIRE(scene != cfg::TranslationScene::Invalid);
            config.scenePrompts[scene] = pair.at(1).get<std::string>();
        }
        for (auto it = testCase.at("compatibleEnginePrompts").begin();
             it != testCase.at("compatibleEnginePrompts").end(); ++it) {
            config.compatibleEnginePrompts[it.key()] = it.value().get<std::string>();
        }
        const auto engine = cfg::fromRaw(cfg::TranslationEngineType{},
                                         testCase.at("engine").get<std::string>());
        const auto scene =
            cfg::fromRaw(cfg::TranslationScene{}, testCase.at("scene").get<std::string>());
        REQUIRE(engine != cfg::TranslationEngineType::Invalid);
        REQUIRE(scene != cfg::TranslationScene::Invalid);
        const std::string resolved = config.resolvedPrompt(
            engine, scene, testCase.at("sourceLanguage").get<std::string>(),
            testCase.at("targetLanguage").get<std::string>(),
            testCase.at("text").get<std::string>(),
            testCase.value("compatiblePromptId", ""));
        REQUIRE(resolved == testCase.at("expected").get<std::string>());
        ++verified;
    }
    CHECK(verified == 2);
}

// ===========================================================================
// Ordered chain fallback (macOS OrderedChainTranslationTests.swift)
// ===========================================================================
namespace chainmocks {

std::unique_ptr<MockTranslationProvider> mockFor(const std::string& engineRaw,
                                                 const cfg::Json& spec) {
    const auto engine = translationEngineFromRaw(engineRaw);
    REQUIRE(engine.has_value());
    const std::string behavior = spec.at("behavior").get<std::string>();
    MockTranslationProvider::Behavior b;
    if (behavior == "ok") {
        b.available = true;
    } else if (behavior == "fail") {
        b.available = true;
        b.translateError = spec.value("errorText", std::string("error"));
    } else {  // "unavailable"
        b.available = false;
    }
    return std::make_unique<MockTranslationProvider>(engineRaw, *engine, b);
}

}  // namespace chainmocks

TEST_CASE("snapshot: ordered chain fallback order matches macOS",
          "[crossplatform][chain]") {
    const auto fixture = loadFixture("ordered-chain-translation.json");
    std::size_t verified = 0;
    for (const auto& testCase : fixture.at("cases")) {
        INFO("case: " << testCase.at("name").get<std::string>());

        std::vector<std::unique_ptr<MockTranslationProvider>> mocks;
        std::map<TranslationEngineType, ITranslationProvider*> providers;
        for (auto it = testCase.at("providers").begin(); it != testCase.at("providers").end();
             ++it) {
            auto mock = chainmocks::mockFor(it.key(), it.value());
            const auto engine = translationEngineFromRaw(it.key());
            providers[*engine] = mock.get();
            mocks.push_back(std::move(mock));
        }
        std::vector<CompatibleConfig> compatibleConfigs;
        if (testCase.contains("compatibleConfigs")) {
            for (const auto& c : testCase.at("compatibleConfigs")) {
                CompatibleConfig config;
                config.id = c.at("id").get<std::string>();  // UUID string identity
                config.displayName = c.at("displayName").get<std::string>();
                compatibleConfigs.push_back(config);
            }
        }

        ChainRequest req;
        for (const auto& entry : testCase.at("chain")) {
            if (entry.at("kind").get<std::string>() == "builtin") {
                const auto engine = translationEngineFromRaw(entry.at("engine").get<std::string>());
                REQUIRE(engine.has_value());
                req.chain.push_back(EngineChainItem::makeBuiltin(*engine));
            } else {
                req.chain.push_back(EngineChainItem::makeCompatible(
                    entry.at("compatibleId").get<std::string>()));
            }
        }
        for (const auto& t : testCase.at("texts")) { req.texts.push_back(t.get<std::string>()); }
        req.sourceLanguage = "en";
        req.targetLanguage = "zh-Hans";

        TranslationScheduler sched(mockDeps(providers, compatibleConfigs));
        const SchedulerOutcome out = sched.translateOrdered(req);
        const auto& expected = testCase.at("expected");

        REQUIRE(out.isSuccess() == expected.at("success").get<bool>());
        if (expected.at("success").get<bool>()) {
            REQUIRE(out.bundle.has_value());
            const auto& bundle = *out.bundle;
            if (expected.contains("primaryEngine")) {
                REQUIRE(std::string(translationEngineRaw(bundle.primaryEngine)) ==
                        expected.at("primaryEngine").get<std::string>());
            }
            if (expected.contains("resultCount")) {
                REQUIRE(bundle.results.size() ==
                        expected.at("resultCount").get<std::size_t>());
            }
            if (expected.contains("firstResultFailed") && !bundle.results.empty()) {
                REQUIRE(bundle.results.front().isSuccess() ==
                        !expected.at("firstResultFailed").get<bool>());
            }
            if (expected.contains("successfulEngines")) {
                std::vector<std::string> raws;
                for (const auto e : bundle.successfulEngines()) {
                    raws.push_back(translationEngineRaw(e));
                }
                REQUIRE(raws == expected.at("successfulEngines")
                                    .get<std::vector<std::string>>());
            }
        } else {
            REQUIRE(out.failures.size() == expected.at("failureCount").get<std::size_t>());
            if (expected.contains("failureEngines")) {
                std::vector<std::string> raws;
                for (const auto& f : out.failures) {
                    raws.push_back(translationEngineRaw(f.engine));
                }
                REQUIRE(raws ==
                        expected.at("failureEngines").get<std::vector<std::string>>());
            }
        }
        if (expected.contains("engineCalls")) {
            for (auto it = expected.at("engineCalls").begin();
                 it != expected.at("engineCalls").end(); ++it) {
                const auto engine = translationEngineFromRaw(it.key());
                REQUIRE(engine.has_value());
                const auto found = providers.find(*engine);
                REQUIRE(found != providers.end());
                const int calls = dynamic_cast<MockTranslationProvider*>(found->second)->calls();
                INFO("engine " << it.key() << " calls");
                REQUIRE(calls == it.value().get<int>());
            }
        }
        ++verified;
    }
    CHECK(verified == 5);

    // EngineChainItem.resolvedEngine (macOS testResolvedEngineSurfacesBuiltinAndCustom).
    std::size_t resolvedChecks = 0;
    for (const auto& rc : fixture.at("resolvedEngineCases")) {
        const auto& entry = rc.at("entry");
        EngineChainItem item;
        if (entry.at("kind").get<std::string>() == "builtin") {
            const auto engine = translationEngineFromRaw(entry.at("engine").get<std::string>());
            REQUIRE(engine.has_value());
            item = EngineChainItem::makeBuiltin(*engine);
        } else {
            item = EngineChainItem::makeCompatible(entry.at("compatibleId").get<std::string>());
        }
        REQUIRE(std::string(translationEngineRaw(item.resolvedEngine())) ==
                rc.at("expected").get<std::string>());
        ++resolvedChecks;
    }
    CHECK(resolvedChecks == 2);
}
