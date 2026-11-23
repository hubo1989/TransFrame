// TranslationPromptConfigTests.cpp — placeholder resolution and priority
// order (macOS TranslationPromptConfigTests via TranslationConfigurationTests).
#include <catch2/catch_amalgamated.hpp>

#include "Core/TranslationPromptConfig.h"
#include "Core/TranslationScene.h"
#include "Core/TranslationEngineType.h"

using namespace tf::core;

TEST_CASE("default prompt substitutes all placeholders", "[prompt]") {
    TranslationPromptConfig config;
    const std::string resolved =
        config.resolvedPrompt(TranslationEngineType::OpenAI, TranslationScene::Screenshot,
                              "English", "Chinese (Simplified)", "hello world");
    REQUIRE(resolved.find("{source_language}") == std::string::npos);
    REQUIRE(resolved.find("{target_language}") == std::string::npos);
    REQUIRE(resolved.find("{text}") == std::string::npos);
    REQUIRE(resolved.find("English") != std::string::npos);
    REQUIRE(resolved.find("Chinese (Simplified)") != std::string::npos);
    REQUIRE(resolved.find("hello world") != std::string::npos);
}

TEST_CASE("insert scene gets the insert prompt by default", "[prompt]") {
    TranslationPromptConfig config;
    const std::string preview =
        config.promptPreview(TranslationEngineType::OpenAI,
                             TranslationScene::TranslateAndInsert);
    REQUIRE(preview == std::string(TranslationPromptConfig::kDefaultInsertPrompt));
    const std::string normal =
        config.promptPreview(TranslationEngineType::OpenAI, TranslationScene::Screenshot);
    REQUIRE(normal == std::string(TranslationPromptConfig::kDefaultPrompt));
}

TEST_CASE("priority: scene-specific beats engine-specific", "[prompt]") {
    TranslationPromptConfig config;
    config.enginePrompts[TranslationEngineType::OpenAI] = "engine prompt {text}";
    config.scenePrompts[TranslationScene::Screenshot] = "scene prompt {text}";
    REQUIRE(config.promptPreview(TranslationEngineType::OpenAI,
                                 TranslationScene::Screenshot) == "scene prompt {text}");
}

TEST_CASE("priority: compatible-specific beats engine-specific for custom", "[prompt]") {
    TranslationPromptConfig config;
    config.enginePrompts[TranslationEngineType::Custom] = "engine prompt";
    config.compatibleEnginePrompts["instance-1"] = "compatible prompt";
    REQUIRE(config.promptPreview(TranslationEngineType::Custom,
                                 TranslationScene::Screenshot, "instance-1") ==
            "compatible prompt");
    // Without a compatible id the engine prompt applies.
    REQUIRE(config.promptPreview(TranslationEngineType::Custom,
                                 TranslationScene::Screenshot) == "engine prompt");
}

TEST_CASE("empty strings fall through to the next tier", "[prompt]") {
    TranslationPromptConfig config;
    config.enginePrompts[TranslationEngineType::OpenAI] = "";  // empty = unset
    REQUIRE(config.promptPreview(TranslationEngineType::OpenAI,
                                 TranslationScene::Screenshot) ==
            std::string(TranslationPromptConfig::kDefaultPrompt));
}

TEST_CASE("sequential substitution matches Swift order", "[prompt]") {
    // Swift replaces {source_language}, then {target_language}, then {text};
    // placeholder-shaped text inside earlier substitutions is substituted by
    // later passes. C++ mirrors that order exactly.
    TranslationPromptConfig config;
    const std::string resolved =
        config.resolvedPrompt(TranslationEngineType::OpenAI, TranslationScene::Screenshot,
                              "{text}", "English", "PAYLOAD");
    REQUIRE(resolved.find("PAYLOAD") != std::string::npos);
    // {source_language} was replaced with "{text}", which the {text} pass then
    // replaced with "PAYLOAD" — so the raw "{text}" marker is gone.
    REQUIRE(resolved.find("{text}") == std::string::npos);
    REQUIRE(resolved.find("{source_language}") == std::string::npos);
}

TEST_CASE("hasCustomPrompts and reset", "[prompt]") {
    TranslationPromptConfig config;
    REQUIRE_FALSE(config.hasCustomPrompts());
    config.enginePrompts[TranslationEngineType::OpenAI] = "x";
    config.compatibleEnginePrompts["a"] = "y";
    config.scenePrompts[TranslationScene::Screenshot] = "z";
    REQUIRE(config.hasCustomPrompts());
    config.reset();
    REQUIRE_FALSE(config.hasCustomPrompts());
}
