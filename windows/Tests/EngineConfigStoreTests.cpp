// EngineConfigStoreTests.cpp — engines.json 合同（见 windows/docs/engines-config-shared.md）。
#include "TestSupport.hpp"
#include <catch2/catch_amalgamated.hpp>

#include <nlohmann/json.hpp>

#include "tf/EngineConfigStore.hpp"

namespace {
using tf::core::EngineConfig;
using tf::core::parseEngineConfig;
using tf::core::serializeEngineConfigPatch;
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
