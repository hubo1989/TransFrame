// EngineConfigStoreTests.cpp — engines.json 合同（见 windows/docs/engines-config-shared.md）。
#include "TestSupport.hpp"
#include <catch2/catch_amalgamated.hpp>

#include <nlohmann/json.hpp>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include "tf/EngineConfigStore.hpp"

namespace {
using tf::core::ColorConfig;
using tf::core::EngineConfig;
using tf::core::parseEngineConfig;
using tf::core::serializeEngineConfigPatch;
using tf::core::ShortcutConfig;
using tf::core::writeEngineConfigFileAtomic;
using tf::core::VlmConfig;
using tf::core::PaddleOcrConfig;
}  // namespace

TEST_CASE("engines.json missing/empty falls back to defaults", "[engines][config]") {
    const auto r = parseEngineConfig("");
    REQUIRE(r.ok);
    CHECK_FALSE(r.fileExisted);
    CHECK(r.config.mtranBaseUrl == "http://localhost:8989");
    CHECK(r.config.targetLang == "zh");
}

TEST_CASE("engines.json full parse", "[engines][config]") {
    const auto r = parseEngineConfig(
        R"({"mtran":{"baseUrl":"http://10.0.0.2:9000"},"targetLang":"en"})");
    REQUIRE(r.ok);
    CHECK(r.fileExisted);
    CHECK(r.config.mtranBaseUrl == "http://10.0.0.2:9000");
    CHECK(r.config.targetLang == "en");
}

TEST_CASE("engines.json normalizes targetLang and trims baseUrl", "[engines][config]") {
    const auto r = parseEngineConfig(
        R"({"mtran":{"baseUrl":"  http://X:1  "},"targetLang":" EN "})");
    REQUIRE(r.ok);
    CHECK(r.config.mtranBaseUrl == "http://X:1");
    CHECK(r.config.targetLang == "en");
}

TEST_CASE("engines.json tolerates unknown engine sections", "[engines][config]") {
    const auto r = parseEngineConfig(
        R"({"ollama":{"baseUrl":"http://127.0.0.1:11434"},"targetLang":"ja"})");
    REQUIRE(r.ok);
    CHECK(r.config.mtranBaseUrl == "http://localhost:8989");  // default
    CHECK(r.config.targetLang == "ja");
    CHECK(r.error.empty());
}

TEST_CASE("engines.json invalid root reports ok=false with defaults", "[engines][config]") {
    for (const char* bad : {"\"x\"", "42", "[]", "{\"mtran\":"}) {
        CAPTURE(bad);
        const auto r = parseEngineConfig(bad);
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.error.empty());
        CHECK(r.config.mtranBaseUrl == "http://localhost:8989");
        CHECK(r.config.targetLang == "zh");
    }
}

TEST_CASE("engines.json wrong-typed fields fall back per-field", "[engines][config]") {
    const auto r = parseEngineConfig(
        R"({"mtran":{"baseUrl":42},"targetLang":[]})");
    REQUIRE(r.ok);
    CHECK(r.config.mtranBaseUrl == "http://localhost:8989");
    CHECK(r.config.targetLang == "zh");
}

TEST_CASE("engines.json empty baseUrl means unset (default)", "[engines][config]") {
    const auto r = parseEngineConfig(R"({"mtran":{"baseUrl":""},"targetLang":"de"})");
    REQUIRE(r.ok);
    CHECK(r.config.mtranBaseUrl == "http://localhost:8989");
    CHECK(r.config.targetLang == "de");
}

TEST_CASE("serializeEngineConfigPatch round-trips and preserves unknown keys", "[engines][config]") {
    const std::string existing =
        R"({"ollama":{"baseUrl":"http://127.0.0.1:11434","keep":true},"targetLang":"fr"})";
    EngineConfig patch;
    patch.mtranBaseUrl = "http://10.0.0.9:8989";
    patch.targetLang = "ru";
    const std::string merged = serializeEngineConfigPatch(existing, patch);

    const auto back = parseEngineConfig(merged);
    REQUIRE(back.ok);
    CHECK(back.config.mtranBaseUrl == "http://10.0.0.9:8989");
    CHECK(back.config.targetLang == "ru");

    // wt-engines' section survives byte-for-byte (as a JSON value).
    const auto ollama = nlohmann::json::parse(merged).at("ollama");
    CHECK(ollama == nlohmann::json::parse(R"({"baseUrl":"http://127.0.0.1:11434","keep":true})"));
}

TEST_CASE("serializeEngineConfigPatch from empty base creates valid file", "[engines][config]") {
    const std::string merged = serializeEngineConfigPatch("", EngineConfig::makeDefault());
    const auto back = parseEngineConfig(merged);
    REQUIRE(back.ok);
    CHECK(back.config.mtranBaseUrl == "http://localhost:8989");
    CHECK(back.config.targetLang == "zh");
}

// ---- full-field extension (mac AppSettings.swift parity) ----

TEST_CASE("extended fields default to mac values", "[engines][config][full]") {
    const EngineConfig d = EngineConfig::makeDefault();
    CHECK(d.defaultFormat == "png");
    CHECK(d.jpegQuality == 0.9);
    CHECK(d.heicQuality == 0.9);
    CHECK(d.saveLocation.empty());
    CHECK(d.sourceLang == "auto");
    CHECK(d.autoDetect);
    CHECK(d.ocrEngine == "vision");
    CHECK(d.translationEngine == "apple");
    CHECK(d.translationMode == "below");
    CHECK(d.preferredTranslationEngine == "apple");
    CHECK(d.translationFallbackEnabled);
    CHECK(d.engineModeV2 == "ordered");
    REQUIRE(d.engineChainV2.size() == 1);
    CHECK((d.engineChainV2[0].isCompatible == false && d.engineChainV2[0].builtinEngine == "apple"));
    REQUIRE(d.compareEnginesV2.size() == 2);
    CHECK(d.compareEnginesV2[1].builtinEngine == "mtran");
    CHECK(d.strokeColor == ColorConfig::makeRed());
    CHECK(d.strokeWidth == 2.0);
    CHECK(d.textSize == 14.0);
    CHECK_FALSE(d.rectangleFilled);
    CHECK_FALSE(d.ellipseFilled);
    CHECK(d.shapeFillColor == ColorConfig::makeYellow());
    CHECK(d.mosaicBlockSize == 10.0);
    CHECK(d.fullScreenShortcut == ShortcutConfig{0x14, 0x0300});
    CHECK(d.pickColorShortcut == ShortcutConfig{0x08, 0x1100});
    CHECK_FALSE(d.onboardingCompleted);
    CHECK_FALSE(d.userSkippedPermissions);
    CHECK(d.vlm == VlmConfig{});
    CHECK(d.paddleOcr == PaddleOcrConfig{});
    CHECK(d.translateAndInsert.sourceLang == "auto");
    CHECK(d.translateAndInsert.targetLang.empty());
}

TEST_CASE("extended fields parse from mac-style JSON", "[engines][config][full]") {
    const auto r = parseEngineConfig(R"({
      "defaultFormat":"heic","jpegQuality":0.5,"heicQuality":0.75,
      "saveLocation":"/Users/x/Pictures",
      "translationSourceLanguage":" EN ","translationAutoDetect":false,
      "ocrEngine":"vision","translationEngine":"mtran","translationMode":"inline",
      "preferredTranslationEngine":"mtran","translationFallbackEnabled":false,
      "engineModeV2":"compare",
      "engineChainV2":[{"id":"u1","kind":{"builtin":"mtran"}},{"id":"u2","kind":{"compatible":"AB-CD"}}],
      "compareEnginesV2":[{"id":"u3","kind":{"builtin":"openai"}}],
      "strokeColor":{"red":0.2,"green":0.4,"blue":0.6,"opacity":0.8},
      "strokeWidth":3.5,"textSize":18.0,"rectangleFilled":true,"ellipseFilled":true,
      "shapeFillColor":{"red":0.1,"green":0.9,"blue":0.1,"opacity":1.0},
      "mosaicBlockSize":12.0,
      "fullScreenShortcut":{"keyCode":11,"modifiers":768},
      "pickColorShortcut":{"keyCode":8,"modifiers":4352},
      "onboardingCompleted":true,"userSkippedPermissions":true,
      "vlmProvider":"claude","vlmBaseURL":"https://api.anthropic.com/v1",
      "vlmModelName":"claude-sonnet-4-20250514","vlmOpenAIMode":"responses",
      "paddleOCRMode":"precise","paddleOCRUseCloud":true,
      "paddleOCRCloudBaseURL":"http://ocr.local","paddleOCRCloudModelId":"vl1",
      "paddleOCRLocalVLModelDir":"/models/vl",
      "translateAndInsertSourceLanguage":"zh","translateAndInsertTargetLanguage":" EN "
    })");
    REQUIRE(r.ok);
    const auto& c = r.config;
    CHECK(c.defaultFormat == "heic");
    CHECK(c.jpegQuality == 0.5);
    CHECK(c.heicQuality == 0.75);
    CHECK(c.saveLocation == "/Users/x/Pictures");
    CHECK(c.sourceLang == "en");
    CHECK_FALSE(c.autoDetect);
    CHECK(c.translationEngine == "mtran");
    CHECK(c.translationMode == "inline");
    CHECK(c.preferredTranslationEngine == "mtran");
    CHECK_FALSE(c.translationFallbackEnabled);
    CHECK(c.engineModeV2 == "compare");
    REQUIRE(c.engineChainV2.size() == 2);
    CHECK(c.engineChainV2[0].builtinEngine == "mtran");
    CHECK((c.engineChainV2[1].isCompatible && c.engineChainV2[1].compatibleId == "AB-CD"));
    REQUIRE(c.compareEnginesV2.size() == 1);
    CHECK(c.compareEnginesV2[0].builtinEngine == "openai");
    CHECK(c.strokeColor == ColorConfig{0.2, 0.4, 0.6, 0.8});
    CHECK(c.strokeWidth == 3.5);
    CHECK(c.textSize == 18.0);
    CHECK(c.rectangleFilled);
    CHECK(c.ellipseFilled);
    CHECK(c.shapeFillColor == ColorConfig{0.1, 0.9, 0.1, 1.0});
    CHECK(c.mosaicBlockSize == 12.0);
    CHECK(c.fullScreenShortcut == ShortcutConfig{11, 768});
    CHECK(c.pickColorShortcut == ShortcutConfig{8, 4352});
    CHECK(c.onboardingCompleted);
    CHECK(c.userSkippedPermissions);
    CHECK(c.vlm.provider == "claude");
    CHECK(c.vlm.baseURL == "https://api.anthropic.com/v1");
    CHECK(c.vlm.modelName == "claude-sonnet-4-20250514");
    CHECK(c.vlm.openAIMode == "responses");
    CHECK(c.paddleOcr.mode == "precise");
    CHECK(c.paddleOcr.useCloud);
    CHECK(c.paddleOcr.cloudBaseURL == "http://ocr.local");
    CHECK(c.paddleOcr.cloudModelId == "vl1");
    CHECK(c.paddleOcr.localVlModelDir == "/models/vl");
    CHECK(c.translateAndInsert.sourceLang == "zh");
    CHECK(c.translateAndInsert.targetLang == "en");  // trim + lowercase
}

TEST_CASE("bad values fall back per-field without failing the parse", "[engines][config][full]") {
    const auto r = parseEngineConfig(R"({
      "defaultFormat":"bmp","jpegQuality":5,"heicQuality":"x",
      "strokeWidth":-1,"textSize":0,"mosaicBlockSize":1e9,
      "strokeColor":{"red":2.0,"green":0,"blue":0,"opacity":1},
      "shapeFillColor":"blue","rectangleFilled":"yes",
      "translationMode":"diagonal","vlmProvider":"grok","engineModeV2":"turbo",
      "engineChainV2":[],"fullScreenShortcut":{"keyCode":"x"},
      "pickColorShortcut":[1,2]
    })");
    REQUIRE(r.ok);
    const auto& c = r.config;
    CHECK(c.defaultFormat == "png");       // unknown enum tag -> default
    CHECK(c.jpegQuality == 0.9);           // out of range
    CHECK(c.heicQuality == 0.9);           // wrong type
    CHECK(c.strokeWidth == 2.0);
    CHECK(c.textSize == 14.0);
    CHECK(c.mosaicBlockSize == 10.0);
    CHECK(c.strokeColor == ColorConfig::makeRed());   // bad component -> whole color default
    CHECK(c.shapeFillColor == ColorConfig::makeYellow());
    CHECK_FALSE(c.rectangleFilled);
    CHECK(c.translationMode == "below");
    CHECK(c.vlm.provider == "openai");
    CHECK(c.engineModeV2 == "ordered");
    // empty chain keeps the mac default (ordered mode must have something to run)
    REQUIRE(c.engineChainV2.size() == 1);
    CHECK(c.engineChainV2[0].builtinEngine == "apple");
    CHECK(c.fullScreenShortcut == ShortcutConfig{0x14, 0x0300});
    CHECK(c.pickColorShortcut == ShortcutConfig{0x08, 0x1100});
}

TEST_CASE("extended fields round-trip through serialize/parse", "[engines][config][full]") {
    EngineConfig cfg = EngineConfig::makeDefault();
    cfg.defaultFormat = "jpeg";
    cfg.jpegQuality = 0.7;
    cfg.saveLocation = "C:/Users/x/Desktop";
    cfg.autoDetect = false;
    cfg.translationEngine = "openai";
    cfg.strokeColor = {0.25, 0.5, 0.75, 0.5};
    cfg.strokeWidth = 4.0;
    cfg.rectangleFilled = true;
    cfg.fullScreenShortcut = {99, 12};
    cfg.onboardingCompleted = true;
    cfg.vlm.provider = "gemini";
    cfg.vlm.modelName = "gemini-2.0-flash";
    cfg.paddleOcr.mode = "precise";
    cfg.paddleOcr.useCloud = true;
    cfg.translateAndInsert.targetLang = "ja";
    cfg.engineChainV2 = {{false, "mtran", ""}, {true, "", "UUID-1"}};
    cfg.compareEnginesV2 = {{false, "apple", ""}};

    const std::string json = serializeEngineConfigPatch("", cfg);
    const auto back = parseEngineConfig(json);
    REQUIRE(back.ok);
    CHECK(back.config == cfg);  // full-struct round trip
}

TEST_CASE("V2 chains are clamped to mac limits and junk entries dropped", "[engines][config][full]") {
    std::string chain;
    for (int i = 0; i < 7; ++i) chain += "{\"id\":\"u\",\"kind\":{\"builtin\":\"openai\"}},";
    const auto r = parseEngineConfig(
        std::string("{\"engineChainV2\":[") + chain.substr(0, chain.size() - 1) +
        "],\"compareEnginesV2\":[{\"id\":\"x\",\"kind\":{}},{\"id\":\"y\",\"kind\":{\"builtin\":\"mtran\"}}]}");
    REQUIRE(r.ok);
    CHECK(r.config.engineChainV2.size() == 5);  // orderedChainLimit
    // kind:{} dropped, valid entry kept
    REQUIRE(r.config.compareEnginesV2.size() == 1);
    CHECK(r.config.compareEnginesV2[0].builtinEngine == "mtran");
}

TEST_CASE("writeEngineConfigFileAtomic writes and replaces", "[engines][config][full]") {
    const auto dir = std::filesystem::temp_directory_path() /
        ("tf-cfg-test-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    const auto file = dir / "engines.json";

    std::error_code ec;
    std::filesystem::remove(file, ec);

    auto w = writeEngineConfigFileAtomic(file, "{\"a\":1}");
    REQUIRE(w.ok);
    CHECK(std::filesystem::file_size(file) == 7);

    // replace over an existing file
    w = writeEngineConfigFileAtomic(file, "{\"b\":2}");
    REQUIRE(w.ok);
    std::ifstream f(file);
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(content == "{\"b\":2}");
    CHECK(!std::filesystem::exists(file.string() + ".tmp"));  // no temp litter

    // unwritable path reports an error without throwing
    const auto badPath = dir / "file-as-dir";
    { std::ofstream touch(badPath); }
    const auto w2 = writeEngineConfigFileAtomic(badPath / "x.json", "{}");
    CHECK_FALSE(w2.ok);
    CHECK_FALSE(w2.error.empty());

    std::filesystem::remove_all(dir, ec);
}
