// tests/EngineBootstrapTests.cpp — engines.json parsing + SchedulerDependencies
// wiring (windows/App/EngineBootstrap, PRD FR-16..24/27). Drives the
// platform-clean bootstrap TU with the shared mocks so the documented config
// format in docs/engines-config.md stays honest.
#include "EngineBootstrap.hpp"
#include "TestSupport.hpp"
#include "catch_amalgamated.hpp"

#include <memory>
#include <string>

using namespace tf;
using namespace tf::app;
using namespace tf::tests;

namespace {

constexpr const char* kInstanceUuid = "5e8f9a2b-1c3d-4e5f-8a90-1b2c3d4e5f60";

EngineBootstrap makeBootstrap(const std::string& json, std::shared_ptr<MockHttpTransport> transport) {
    return EngineBootstrap(parseEngineConfig(json), std::make_shared<MockCredentialStore>(),
                           std::move(transport));
}

/// Splices the instance UUID into JSON templates (placeholder "__UUID__"
/// → "<uuid>" so both string values and object keys stay valid JSON).
std::string substUuid(const char* tpl) {
    std::string s = tpl;
    const std::string needle = "__UUID__";
    const std::string quoted = std::string("\"") + kInstanceUuid + "\"";
    for (auto pos = s.find(needle); pos != std::string::npos; pos = s.find(needle, pos)) {
        s.replace(pos, needle.size(), quoted);
    }
    return s;
}

}  // namespace

// ===========================================================================
// Defaults (engines.json absent)
// ===========================================================================
TEST_CASE("bootstrap: defaults mirror the v1 MTranServer wiring", "[bootstrap]") {
    const auto cfg = defaultEngineConfig();
    REQUIRE(cfg.engines.size() == 1);
    REQUIRE(cfg.engines[0].engine == TranslationEngineType::MtranServer);
    REQUIRE(cfg.engines[0].enabled);
    REQUIRE(cfg.engines[0].config.baseURL == "http://localhost:8989");
    REQUIRE(cfg.compatibleInstances.empty());
    REQUIRE(cfg.sceneRoutes.empty());
    REQUIRE(cfg.globalChain.size() == 1);
    REQUIRE(cfg.globalChain[0].kind == EngineChainItem::Kind::Builtin);
    REQUIRE(cfg.globalChain[0].builtin == TranslationEngineType::MtranServer);
    REQUIRE(cfg.globalCompareSet.empty());
}

// ===========================================================================
// Engine rows → EngineConfig field mapping (FR-18)
// ===========================================================================
TEST_CASE("bootstrap: engine rows map onto EngineConfig fields", "[bootstrap]") {
    const std::string json = substUuid(R"json({
        "engines": [
            {"engine": "openai", "enabled": true, "baseURL": "https://api.openai.com/v1",
             "modelName": "gpt-4o-mini", "timeout": 30.5, "maxTokens": 2048,
             "temperature": 0.2, "customHeaders": {"X-Title": "TransFrame"}},
            {"engine": "mtran", "enabled": true, "baseURL": "http://127.0.0.1:9000"},
            {"engine": "deepl", "enabled": false}
        ]
    })json");
    const auto boot = makeBootstrap(json, nullptr);

    const auto openai = boot.engineConfig(TranslationEngineType::OpenAI);
    REQUIRE(openai.has_value());
    REQUIRE(openai->baseURL == "https://api.openai.com/v1");
    REQUIRE(openai->modelName == "gpt-4o-mini");
    REQUIRE(openai->timeoutSeconds.has_value());
    REQUIRE(*openai->timeoutSeconds == 30.5);
    REQUIRE(openai->maxTokens.has_value());
    REQUIRE(*openai->maxTokens == 2048);
    REQUIRE(openai->temperature.has_value());
    REQUIRE(*openai->temperature == 0.2);
    REQUIRE(openai->customHeaders.at("X-Title") == "TransFrame");

    const auto mtran = boot.engineConfig(TranslationEngineType::MtranServer);
    REQUIRE(mtran.has_value());
    REQUIRE(mtran->baseURL == "http://127.0.0.1:9000");

    // Disabled rows resolve to nullopt → the chain falls through (v1 semantics).
    REQUIRE_FALSE(boot.engineConfig(TranslationEngineType::DeepL).has_value());
    REQUIRE_FALSE(boot.engineConfig(TranslationEngineType::Baidu).has_value());
}

TEST_CASE("bootstrap: TRANSFRAME_MTRAN_URL override beats engines.json", "[bootstrap]") {
    const std::string json = substUuid(R"json({"engines": [{"engine": "mtran", "baseURL": "http://10.0.0.5:8989"}]})json");
    auto cfg = parseEngineConfig(json);
    cfg.mtranUrlOverride = "http://192.168.1.20:9000";
    EngineBootstrap boot(cfg, std::make_shared<MockCredentialStore>(), nullptr);
    const auto mtran = boot.engineConfig(TranslationEngineType::MtranServer);
    REQUIRE(mtran.has_value());
    REQUIRE(mtran->baseURL == "http://192.168.1.20:9000");
}

// ===========================================================================
// Compatible multi-instance (FR-19)
// ===========================================================================
TEST_CASE("bootstrap: compatible instances parse and key the provider seam", "[bootstrap]") {
    const std::string json = substUuid(R"json({
        "compatibleInstances": [
            {"id": __UUID__, "displayName": "Local vLLM",
             "baseURL": "http://localhost:8000/v1", "modelName": "qwen2.5-7b", "hasAPIKey": false}
        ],
        "globalChain": [{"kind": "compatible", "id": __UUID__}]
    })json");
    const auto boot = makeBootstrap(json, nullptr);

    const auto inst = boot.compatibleById(kInstanceUuid);
    REQUIRE(inst.has_value());
    REQUIRE(inst->displayName == "Local vLLM");
    REQUIRE(inst->baseURL == "http://localhost:8000/v1");
    REQUIRE(inst->modelName == "qwen2.5-7b");
    REQUIRE_FALSE(inst->hasAPIKey);
    REQUIRE_FALSE(boot.compatibleById("00000000-0000-0000-0000-000000000000").has_value());

    SchedulerDependencies deps;
    boot.populate(deps);
    REQUIRE(deps.compatibleProvider);
    const auto provider = deps.compatibleProvider(kInstanceUuid);
    REQUIRE(provider != nullptr);
    REQUIRE(provider->name() == "Local vLLM");
    REQUIRE(provider->engineType() == TranslationEngineType::Custom);
    // Dangling UUID (deleted instance) resolves to nullptr → chain falls through.
    REQUIRE(deps.compatibleProvider("f0e1d2c3-4a5b-6c7d-8e9f-0a1b2c3d4e5f") == nullptr);
    REQUIRE(deps.compatibleConfigs);
    const auto configs = deps.compatibleConfigs();
    REQUIRE(configs.size() == 1);
    REQUIRE(configs[0].id == kInstanceUuid);
}

// ===========================================================================
// SchedulerDependencies slots (builtin + prompt layers)
// ===========================================================================
TEST_CASE("bootstrap: builtinProvider honors enabled rows and the mtran override", "[bootstrap]") {
    const std::string json = substUuid(R"json({
        "engines": [
            {"engine": "mtran", "enabled": true, "baseURL": "http://127.0.0.1:9001"},
            {"engine": "ollama", "enabled": false}
        ]
    })json");
    auto transport = std::make_shared<MockHttpTransport>();
    const auto boot = makeBootstrap(json, transport);
    SchedulerDependencies deps;
    boot.populate(deps);

    REQUIRE(deps.builtinProvider);
    const auto mtran = deps.builtinProvider(TranslationEngineType::MtranServer);
    REQUIRE(mtran != nullptr);
    const auto* mtranProvider = dynamic_cast<MTranServerProvider*>(mtran.get());
    REQUIRE(mtranProvider != nullptr);
    REQUIRE(mtranProvider->configuration().serverAddress == "127.0.0.1");
    REQUIRE(mtranProvider->configuration().serverPort == 9001);

    // Disabled engine → nullptr (unresolved, chain falls through).
    REQUIRE(deps.builtinProvider(TranslationEngineType::Ollama) == nullptr);
    // Engines without a row are disabled by default.
    REQUIRE(deps.builtinProvider(TranslationEngineType::Google) == nullptr);
}

TEST_CASE("bootstrap: promptResolver layers scene > compatible > engine > default", "[bootstrap]") {
    const std::string json = substUuid(R"json({
        "prompts": {
            "enginePrompts": {"openai": "ENGINE TEMPLATE"},
            "compatibleEnginePrompts": {__UUID__: "COMPATIBLE TEMPLATE"},
            "scenePrompts": {"translate_and_insert": "SCENE TEMPLATE"}
        }
    })json");
    const auto boot = makeBootstrap(json, nullptr);
    SchedulerDependencies deps;
    boot.populate(deps);
    REQUIRE(deps.promptResolver);

    REQUIRE(deps.promptResolver(TranslationEngineType::OpenAI, TranslationScene::Screenshot,
                                std::string()) == "ENGINE TEMPLATE");
    REQUIRE(deps.promptResolver(TranslationEngineType::Custom, TranslationScene::Screenshot,
                                kInstanceUuid) == "COMPATIBLE TEMPLATE");
    // Scene layer wins over both engine layers.
    REQUIRE(deps.promptResolver(TranslationEngineType::OpenAI,
                                TranslationScene::TranslateAndInsert,
                                std::string()) == "SCENE TEMPLATE");
    // No overrides → engine-type-appropriate defaults, never empty. The scene
    // layer above applies to every engine, so defaults need a fresh bootstrap.
    const auto plain = makeBootstrap("{}", nullptr);
    SchedulerDependencies plainDeps;
    plain.populate(plainDeps);
    REQUIRE(plainDeps.promptResolver);
    REQUIRE(plainDeps.promptResolver(TranslationEngineType::Ollama, TranslationScene::Screenshot,
                                     std::string()) == TranslationPromptConfig::defaultPrompt());
    REQUIRE(plainDeps.promptResolver(TranslationEngineType::Ollama,
                                     TranslationScene::TranslateAndInsert,
                                     std::string()) ==
            TranslationPromptConfig::defaultInsertPrompt());
}

// ===========================================================================
// Scene bindings (FR-23)
// ===========================================================================
TEST_CASE("bootstrap: scene routing with ordered, compare and global fallback", "[bootstrap]") {
    const std::string json = substUuid(R"json({
        "globalChain": [{"kind": "builtin", "engine": "mtran"}],
        "sceneBindings": {
            "translate_and_insert": {"mode": "ordered",
                                     "engines": [{"kind": "builtin", "engine": "openai"},
                                                 {"kind": "builtin", "engine": "mtran"}]},
            "text_selection": {"mode": "compare",
                               "engines": [{"kind": "builtin", "engine": "mtran"},
                                           {"kind": "compatible", "id": __UUID__}]}
        },
        "compatibleInstances": [
            {"id": __UUID__, "displayName": "Local vLLM",
             "baseURL": "http://localhost:8000/v1", "modelName": "q", "hasAPIKey": false}
        ]
    })json");
    const auto boot = makeBootstrap(json, nullptr);

    // Unbound scene → global ordered chain.
    const auto screenshot = boot.routeForScene(TranslationScene::Screenshot);
    REQUIRE_FALSE(screenshot.useCompare);
    REQUIRE(screenshot.items.size() == 1);
    REQUIRE(screenshot.items[0].builtin == TranslationEngineType::MtranServer);

    // Bound ordered scene.
    const auto insert = boot.routeForScene(TranslationScene::TranslateAndInsert);
    REQUIRE_FALSE(insert.useCompare);
    REQUIRE(insert.items.size() == 2);
    REQUIRE(insert.items[0].builtin == TranslationEngineType::OpenAI);
    REQUIRE(insert.items[1].builtin == TranslationEngineType::MtranServer);

    // Bound compare scene.
    const auto selection = boot.routeForScene(TranslationScene::TextSelection);
    REQUIRE(selection.useCompare);
    REQUIRE(selection.items.size() == 2);
    REQUIRE(selection.items[1].kind == EngineChainItem::Kind::Compatible);
    REQUIRE(selection.items[1].compatibleId == kInstanceUuid);
}

// ===========================================================================
// Malformed input → runtime_error (AppMain keeps defaults)
// ===========================================================================
TEST_CASE("bootstrap: malformed documents and bad entries throw", "[bootstrap]") {
    REQUIRE_THROWS_AS(parseEngineConfig("not json"), std::runtime_error);
    REQUIRE_THROWS_AS(parseEngineConfig("[]"), std::runtime_error);
    REQUIRE_THROWS_AS(parseEngineConfig(R"json({"engines": 3})json"), std::runtime_error);
    REQUIRE_THROWS_AS(parseEngineConfig(R"json({"engines": [{"engine": "nope"}]})json"),
                      std::runtime_error);
    REQUIRE_THROWS_AS(parseEngineConfig(R"json({"globalChain": [{"kind": "builtin"}]})json"),
                      std::runtime_error);
    REQUIRE_THROWS_AS(
        parseEngineConfig(R"json({"compatibleInstances": [{"id": "not-a-uuid", "baseURL": "http://x"}]})json"),
        std::runtime_error);
    REQUIRE_THROWS_AS(parseEngineConfig(R"json({"sceneBindings": {"bogus_scene": {}}})json"),
                      std::runtime_error);
    // Empty document = every section missing → all defaults kept.
    const auto cfg = parseEngineConfig("{}");
    REQUIRE(cfg.engines.empty());
    REQUIRE(cfg.globalChain.empty());
}

// ===========================================================================
// End-to-end: populated deps drive the scheduler (mock transport)
// ===========================================================================
TEST_CASE("bootstrap: populated scheduler resolves the configured mtran endpoint", "[bootstrap]") {
    const std::string json = substUuid(R"json({
        "engines": [{"engine": "mtran", "enabled": true, "baseURL": "http://127.0.0.1:9010"}]
    })json");
    auto transport = std::make_shared<MockHttpTransport>();
    transport->routes["http://127.0.0.1:9010/translate"] = {
        200, R"json({"result":"你好","detected_language":"en"})json"};
    const auto boot = makeBootstrap(json, transport);
    SchedulerDependencies deps;
    boot.populate(deps);
    TranslationScheduler scheduler(deps);

    ChainRequest req;
    req.chain = boot.routeForScene(TranslationScene::Screenshot).items;
    req.texts = {"Hello"};
    req.sourceLanguage = "";
    req.targetLanguage = "zh";
    req.scene = TranslationScene::Screenshot;
    const auto out = scheduler.translateOrdered(req);
    REQUIRE(out.isSuccess());
    REQUIRE(out.bundle->results.size() == 1);
    REQUIRE_FALSE(out.bundle->results[0].failed);
    REQUIRE(out.bundle->results[0].segments.size() == 1);
    REQUIRE(out.bundle->results[0].segments[0].translated == "你好");
}

TEST_CASE("bootstrap: compare-bound scene routes through translateCompare", "[bootstrap]") {
    const std::string json = substUuid(R"json({
        "engines": [{"engine": "mtran", "enabled": true}],
        "compatibleInstances": [
            {"id": __UUID__, "displayName": "Local vLLM",
             "baseURL": "http://localhost:8000/v1", "modelName": "q", "hasAPIKey": false}
        ],
        "sceneBindings": {
            "text_selection": {"mode": "compare",
                               "engines": [{"kind": "builtin", "engine": "mtran"},
                                           {"kind": "compatible", "id": __UUID__}]}
        }
    })json");
    auto transport = std::make_shared<MockHttpTransport>();
    transport->routes["http://localhost:8989/translate"] = {
        200, R"json({"result":"你好","detected_language":"en"})json"};
    transport->routes["http://127.0.0.1:8000/v1/chat/completions"] = {
        200,
        R"json({"choices":[{"message":{"content":"<tf0>你好</tf0>"}}]})json"};
    const auto boot = makeBootstrap(json, transport);
    SchedulerDependencies deps;
    boot.populate(deps);
    TranslationScheduler scheduler(deps);

    const auto route = boot.routeForScene(TranslationScene::TextSelection);
    REQUIRE(route.useCompare);
    CompareRequest req;
    req.engines = route.items;
    req.texts = {"Hello"};
    req.sourceLanguage = "";
    req.targetLanguage = "zh";
    req.scene = TranslationScene::TextSelection;
    const auto out = scheduler.translateCompare(req);
    REQUIRE(out.isSuccess());
    REQUIRE(out.bundle->results.size() == 2);
    // Both engines served from the same mock transport.
    for (const auto& r : out.bundle->results) {
        REQUIRE_FALSE(r.failed);
        REQUIRE_FALSE(r.segments.empty());
    }
}

// ===========================================================================
// Health wiring (FR-22) — queries for the future settings page
// ===========================================================================
TEST_CASE("bootstrap: health queries report green and orange", "[bootstrap]") {
    const std::string json = substUuid(R"json({
        "engines": [{"engine": "mtran", "enabled": true}]
    })json");
    auto transport = std::make_shared<MockHttpTransport>();
    // MTranServer free probe ladder hits /health on the default endpoint.
    transport->routes["http://localhost:8989/health"] = {200, R"json({"status":"ok"})json"};
    const auto boot = makeBootstrap(json, transport);
    REQUIRE(boot.healthChecker() != nullptr);
    REQUIRE(boot.healthOf(TranslationEngineType::MtranServer) == EngineHealth::Available);
    // Enabled-but-keyless builtin (openai gate) → orange, no probe sent.
    REQUIRE(boot.healthOf(TranslationEngineType::OpenAI) == EngineHealth::Unconfigured);
    // Unknown compatible instance → red (deleted instance).
    REQUIRE(boot.healthOfCompatible("00000000-0000-0000-0000-000000000000") ==
            EngineHealth::Misconfigured);
}
