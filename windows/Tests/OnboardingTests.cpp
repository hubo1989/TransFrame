// OnboardingTests.cpp — FR-39 pure logic: default-chain merge into
// engines.json, done-flag round-trip, privacy cloud/local classification,
// hotkey resolution, and the 3-step flow machine.
#include <catch2/catch_amalgamated.hpp>

#include "tf/Onboarding.hpp"

#include <nlohmann/json.hpp>

namespace {

nlohmann::json parseOk(const std::string& text) {
    const auto j = nlohmann::json::parse(text, nullptr, false);
    REQUIRE_FALSE(j.is_discarded());
    return j;
}

}  // namespace

using tf::core::OnboardingChainChoice;

// ---------------------------------------------------------------------------
// choice metadata
// ---------------------------------------------------------------------------
TEST_CASE("onboarding choice cards map to engines with expected locality",
          "[onboarding][choice]") {
    const auto mtran = tf::core::onboardingChoiceInfo(OnboardingChainChoice::MtranLocal);
    CHECK(std::string(mtran.engineId) == "mtran");
    CHECK(mtran.local);

    const auto ollama = tf::core::onboardingChoiceInfo(OnboardingChainChoice::OllamaLocal);
    CHECK(std::string(ollama.engineId) == "ollama");
    CHECK(ollama.local);

    const auto cloud = tf::core::onboardingChoiceInfo(OnboardingChainChoice::CloudOpenAI);
    CHECK(std::string(cloud.engineId) == "openai");
    CHECK_FALSE(cloud.local);
}

// ---------------------------------------------------------------------------
// privacy classification
// ---------------------------------------------------------------------------
TEST_CASE("engine cloud classification: local hosts never leave the box",
          "[onboarding][privacy]") {
    using tf::core::OnboardingEngineRow;
    using tf::core::onboardingEngineSendsToCloud;
    // Local endpoints.
    CHECK_FALSE(onboardingEngineSendsToCloud({"mtran", "http://localhost:8989", true}));
    CHECK_FALSE(onboardingEngineSendsToCloud({"ollama", "http://127.0.0.1:11434", true}));
    CHECK_FALSE(onboardingEngineSendsToCloud({"x", "http://[::1]:9000", true}));
    CHECK_FALSE(onboardingEngineSendsToCloud({"x", "", true}));  // no endpoint = engine default (local models)
    // Cloud.
    CHECK(onboardingEngineSendsToCloud({"openai", "https://api.openai.com", true}));
    CHECK(onboardingEngineSendsToCloud({"custom", "http://translate.example.com", true}));
    // Row disabled = never sends.
    CHECK_FALSE(onboardingEngineSendsToCloud({"openai", "https://api.openai.com", false}));
}

TEST_CASE("onboardingUrlHost extracts host", "[onboarding][privacy]") {
    using tf::core::onboardingUrlHost;
    CHECK(onboardingUrlHost("http://localhost:8989/x") == "localhost");
    CHECK(onboardingUrlHost("https://api.openai.com/v1") == "api.openai.com");
    CHECK(onboardingUrlHost("not a url") == "not a url");
    CHECK(onboardingUrlHost("") == "");
}

// ---------------------------------------------------------------------------
// default-chain merge (engines.json fragment)
// ---------------------------------------------------------------------------
TEST_CASE("mergeOnboardingChain writes engines row + globalChain, preserves keys",
          "[onboarding][merge]") {
    const std::string existing = R"({
  "appLanguage": "zh-Hans",
  "engines": [
    {"engine": "mtran", "enabled": true, "baseURL": "http://localhost:8989"},
    {"engine": "deepl", "enabled": false}
  ],
  "globalChain": [{"kind": "builtin", "engine": "mtran"}],
  "futureKey": {"nested": [1, 2]}
})";
    const std::string merged = tf::core::mergeOnboardingChain(
        existing, OnboardingChainChoice::OllamaLocal);
    const auto j = parseOk(merged);

    // Unknown top-level keys survive.
    CHECK(j.at("appLanguage") == "zh-Hans");
    CHECK(j.at("futureKey").at("nested") == nlohmann::json({1, 2}));

    // Ollama row upserted (previously absent) + enabled.
    const auto& engines = j.at("engines");
    REQUIRE(engines.size() == 3);
    REQUIRE(engines[1].at("engine") == "deepl");
    REQUIRE(engines[2].at("engine") == "ollama");
    CHECK(engines[2].at("enabled").get<bool>());
    CHECK(engines[2].at("baseURL") == "http://localhost:11434");

    // globalChain = single builtin entry pointing at the choice.
    REQUIRE(j.at("globalChain").is_array());
    REQUIRE(j.at("globalChain").size() == 1);
    CHECK(j.at("globalChain")[0].at("kind") == "builtin");
    CHECK(j.at("globalChain")[0].at("engine") == "ollama");

    // Output is parseable again (round-trip stability).
    CHECK(tf::core::mergeOnboardingChain(
              merged, OnboardingChainChoice::MtranLocal)
              .find("\"globalChain\"") != std::string::npos);
}

TEST_CASE("mergeOnboardingChain upserts an existing engine row in place",
          "[onboarding][merge]") {
    const std::string existing = R"({
  "engines": [
    {"engine": "openai", "enabled": false},
    {"engine": "mtran", "enabled": true}
  ]
})";
    const auto j = parseOk(tf::core::mergeOnboardingChain(
        existing, OnboardingChainChoice::CloudOpenAI));
    const auto& engines = j.at("engines");
    REQUIRE(engines.size() == 2);
    REQUIRE(engines[0].at("engine") == "openai");
    CHECK(engines[0].at("enabled").get<bool>());  // flipped on
    CHECK(engines[1].at("engine") == "mtran");    // untouched
    CHECK(j.at("globalChain")[0].at("engine") == "openai");
}

TEST_CASE("mergeOnboardingChain tolerates empty and broken input",
          "[onboarding][merge]") {
    const auto j = parseOk(tf::core::mergeOnboardingChain(
        "", OnboardingChainChoice::MtranLocal));
    REQUIRE(j.at("engines").size() == 1);
    CHECK(j.at("globalChain")[0].at("engine") == "mtran");

    const auto j2 = parseOk(tf::core::mergeOnboardingChain(
        "{ not json", OnboardingChainChoice::MtranLocal));
    CHECK(j2.at("globalChain")[0].at("engine") == "mtran");
}

// ---------------------------------------------------------------------------
// done flag (settings.json)
// ---------------------------------------------------------------------------
TEST_CASE("onboarding done flag round-trips and preserves settings",
          "[onboarding][done]") {
    CHECK_FALSE(tf::core::parseOnboardingDone(""));
    CHECK_FALSE(tf::core::parseOnboardingDone(R"({"hotkeys": []})"));
    CHECK_FALSE(tf::core::parseOnboardingDone("garbage"));

    const std::string settings = R"({"TransFrame.checkForUpdates.win": true})";
    const std::string flagged = tf::core::serializeOnboardingDoneFlag(settings);
    const auto j = parseOk(flagged);
    CHECK(j.at("onboardingDone").get<bool>());
    CHECK(j.at("TransFrame.checkForUpdates.win").get<bool>());  // preserved
    CHECK(tf::core::parseOnboardingDone(flagged));

    // Re-flagging is idempotent (no duplicate key, still true).
    CHECK(tf::core::parseOnboardingDone(
        tf::core::serializeOnboardingDoneFlag(flagged)));
}

// ---------------------------------------------------------------------------
// hotkey resolution (step 1)
// ---------------------------------------------------------------------------
TEST_CASE("resolveOnboardingHotkeys applies settings.json over defaults",
          "[onboarding][hotkeys]") {
    const auto defaults = tf::core::defaultOnboardingHotkeys();
    REQUIRE(defaults.size() == 5);
    // README family: Ctrl+Alt+F/A/T/D/I.
    const std::uint32_t ctrlAlt = 0x1 | 0x2;
    CHECK(defaults[0].vk == 0x46);  // F
    CHECK(defaults[1].vk == 0x41);  // A
    CHECK(defaults[2].vk == 0x54);  // T
    CHECK(defaults[3].vk == 0x44);  // D
    CHECK(defaults[4].vk == 0x49);  // I
    for (const auto& b : defaults) CHECK(b.modifiers == ctrlAlt);

    // settings.json per-action keys (schema §1) override the shown values.
    const std::string settings = R"({
  "TransFrame.selectionShortcut": {"vk": 83, "modifiers": 10, "enabled": true},
  "TransFrame.translationModeShortcut": {"vk": 84, "modifiers": 3, "enabled": true}
})";
    const auto resolved = tf::core::resolveOnboardingHotkeys(settings);
    REQUIRE(resolved.size() == 5);
    CHECK(resolved[1].vk == 83);            // region override
    CHECK(resolved[1].modifiers == 10);
    CHECK(resolved[2].vk == 84);            // translate override
    CHECK(resolved[0].vk == 0x46);          // untouched default
    // Broken settings fall back to defaults (field-wise: HotkeyBinding has
    // no operator==).
    const auto fallback = tf::core::resolveOnboardingHotkeys("{ nope");
    REQUIRE(fallback.size() == defaults.size());
    for (std::size_t i = 0; i < defaults.size(); ++i) {
        CHECK(fallback[i].vk == defaults[i].vk);
        CHECK(fallback[i].modifiers == defaults[i].modifiers);
        CHECK(fallback[i].enabled == defaults[i].enabled);
    }
}

// ---------------------------------------------------------------------------
// flow state machine
// ---------------------------------------------------------------------------
TEST_CASE("onboarding flow walks 3 steps with back/continue/skip",
          "[onboarding][flow]") {
    using tf::core::OnboardingFlow;
    OnboardingFlow f;
    CHECK(f.step == 0);
    CHECK_FALSE(f.canBack());
    CHECK(f.canContinue());

    f.next();  // 0 -> 1
    CHECK(f.step == 1);
    CHECK(f.canBack());
    f.next();  // 1 -> 2
    CHECK(f.step == 2);
    CHECK(f.canBack());
    f.back();  // 2 -> 1
    CHECK(f.step == 1);

    f.next();  // back to the last step
    CHECK(f.step == 2);
    // No card selected: cannot advance past step 2, finish yields skip.
    CHECK_FALSE(f.canContinue());
    CHECK_FALSE(f.finish().has_value());
    f.choice = OnboardingChainChoice::MtranLocal;
    CHECK(f.canContinue());
    CHECK(f.finish().has_value());  // step 2 + choice = completion payload
    CHECK(*f.finish() == OnboardingChainChoice::MtranLocal);
}
