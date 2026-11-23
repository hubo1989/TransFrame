// tests/EngineHealthCheckerTests.cpp — four-state health + 60s cache TTL
// (vectors mirror TransFrameTests/EngineHealthCheckerTests.swift).
#include "TestSupport.hpp"
#include "catch_amalgamated.hpp"

using namespace tf;
using namespace tf::tests;

namespace {

/// Probe double: counts probes and returns scripted health per key.
class CountingProbe final : public IEngineProbe {
public:
    std::map<std::string, EngineHealth> scripted;
    int probeCount = 0;

    EngineHealth probe(const EngineHealthKey& key, const CancellationToken&) override {
        ++probeCount;
        auto it = scripted.find(key.display());
        return it == scripted.end() ? EngineHealth::Unverified : it->second;
    }
};

}  // namespace

TEST_CASE("health: mtran probe hits /health and reports green on 200",
          "[health]") {
    auto transport = std::make_shared<MockHttpTransport>();
    transport->routes["http://localhost:8989/health"] = {200, "{\"status\":\"ok\"}"};
    DefaultEngineProbe probe(std::make_shared<MockCredentialStore>(), transport, nullptr);
    auto health = probe.probe(EngineHealthKey::forBuiltin(TranslationEngineType::MtranServer), {});
    REQUIRE(health == EngineHealth::Available);
    REQUIRE(transport->requests.size() == 1);
    REQUIRE(transport->requests.front().url == "http://localhost:8989/health");
}

TEST_CASE("health: mtran probe ladder tolerates 404 on /health then /",
          "[health]") {
    auto transport = std::make_shared<MockHttpTransport>();
    transport->routes["http://localhost:8989/health"] = {404, ""};
    transport->routes["http://localhost:8989/"] = {200, "ok"};
    DefaultEngineProbe probe(std::make_shared<MockCredentialStore>(), transport, nullptr);
    auto health = probe.probe(EngineHealthKey::forBuiltin(TranslationEngineType::MtranServer), {});
    REQUIRE(health == EngineHealth::Available);
    REQUIRE(transport->requests.size() == 2);
}

TEST_CASE("health: unreachable mtran is red", "[health]") {
    auto transport = std::make_shared<MockHttpTransport>();
    // No routes: every probe fails at transport level → red.
    DefaultEngineProbe probe(std::make_shared<MockCredentialStore>(), transport, nullptr);
    auto health = probe.probe(EngineHealthKey::forBuiltin(TranslationEngineType::MtranServer), {});
    REQUIRE(health == EngineHealth::Misconfigured);
}

TEST_CASE("health: google/baidu never probe — credentials gate orange/gray",
          "[health]") {
    auto emptyStore = std::make_shared<MockCredentialStore>();
    DefaultEngineProbe noCreds(emptyStore, std::make_shared<MockHttpTransport>(), nullptr);
    REQUIRE(noCreds.probe(EngineHealthKey::forBuiltin(TranslationEngineType::Google), {}) ==
            EngineHealth::Unconfigured);   // orange
    REQUIRE(noCreds.probe(EngineHealthKey::forBuiltin(TranslationEngineType::Baidu), {}) ==
            EngineHealth::Unconfigured);

    auto withCreds = credStoreWith(credentialAccount(TranslationEngineType::Google), "k");
    DefaultEngineProbe hasCreds(withCreds, std::make_shared<MockHttpTransport>(), nullptr);
    REQUIRE(hasCreds.probe(EngineHealthKey::forBuiltin(TranslationEngineType::Google), {}) ==
            EngineHealth::Unverified);  // gray — no free probe
}

TEST_CASE("health: claude/gemini/deepl never burn quota (paid verify exempt)",
          "[health]") {
    // One store holding keys for all three paid engines.
    auto store = std::make_shared<MockCredentialStore>();
    store->save(credentialAccount(TranslationEngineType::Claude), StoredCredentials{"k", "", {}});
    store->save(credentialAccount(TranslationEngineType::Gemini), StoredCredentials{"k", "", {}});
    store->save(credentialAccount(TranslationEngineType::DeepL), StoredCredentials{"k", "", {}});
    auto transport = std::make_shared<MockHttpTransport>();
    DefaultEngineProbe probe(store, transport, nullptr);
    REQUIRE(probe.probe(EngineHealthKey::forBuiltin(TranslationEngineType::Claude), {}) ==
            EngineHealth::Unverified);
    REQUIRE(probe.probe(EngineHealthKey::forBuiltin(TranslationEngineType::Gemini), {}) ==
            EngineHealth::Unverified);
    REQUIRE(probe.probe(EngineHealthKey::forBuiltin(TranslationEngineType::DeepL), {}) ==
            EngineHealth::Unverified);
    // No probe request ever sent — paid verify must not run.
    REQUIRE(transport->requests.empty());
}

TEST_CASE("health: openai with key probes models list (green on 200)",
          "[health]") {
    auto transport = std::make_shared<MockHttpTransport>();
    transport->routes["https://api.openai.com/v1/models"] = {200, R"({"data":[]})"};
    auto store = credStoreWith(credentialAccount(TranslationEngineType::OpenAI), "sk-test");
    DefaultEngineProbe probe(store, transport, nullptr);
    REQUIRE(probe.probe(EngineHealthKey::forBuiltin(TranslationEngineType::OpenAI), {}) ==
            EngineHealth::Available);
}

TEST_CASE("health: openai without key is orange without any request",
          "[health]") {
    auto transport = std::make_shared<MockHttpTransport>();
    DefaultEngineProbe probe(std::make_shared<MockCredentialStore>(), transport, nullptr);
    REQUIRE(probe.probe(EngineHealthKey::forBuiltin(TranslationEngineType::OpenAI), {}) ==
            EngineHealth::Unconfigured);
    REQUIRE(transport->requests.empty());
}

TEST_CASE("health: ollama probes v1/models keyless", "[health]") {
    auto transport = std::make_shared<MockHttpTransport>();
    transport->routes["http://127.0.0.1:11434/v1/models"] = {200, R"({"data":[]})"};
    DefaultEngineProbe probe(std::make_shared<MockCredentialStore>(), transport, nullptr);
    REQUIRE(probe.probe(EngineHealthKey::forBuiltin(TranslationEngineType::Ollama), {}) ==
            EngineHealth::Available);
}

TEST_CASE("health: dangling compatible instance is red", "[health]") {
    DefaultEngineProbe probe(std::make_shared<MockCredentialStore>(),
                             std::make_shared<MockHttpTransport>(),
                             [] { return std::vector<CompatibleConfig>{}; });
    REQUIRE(probe.probe(EngineHealthKey::forCompatible(Uuid::random().toString()), {}) ==
            EngineHealth::Misconfigured);
}

TEST_CASE("health: compatible instance with missing key is orange", "[health]") {
    CompatibleConfig cfg;
    cfg.id = Uuid::random().toString();
    cfg.displayName = "inst";
    cfg.baseURL = "http://localhost:8000/v1";
    cfg.modelName = "m";
    cfg.hasAPIKey = true;  // declares a key but none stored
    DefaultEngineProbe probe(std::make_shared<MockCredentialStore>(),
                             std::make_shared<MockHttpTransport>(),
                             [cfg] { return std::vector<CompatibleConfig>{cfg}; });
    REQUIRE(probe.probe(EngineHealthKey::forCompatible(cfg.id), {}) ==
            EngineHealth::Unconfigured);
}

TEST_CASE("health: compatible instance free models probe", "[health]") {
    auto transport = std::make_shared<MockHttpTransport>();
    transport->routes["http://127.0.0.1:8000/v1/models"] = {200, R"({"data":[]})"};
    CompatibleConfig cfg;
    cfg.id = Uuid::random().toString();
    cfg.displayName = "inst";
    cfg.baseURL = "http://localhost:8000/v1";
    cfg.modelName = "m";
    cfg.hasAPIKey = false;
    DefaultEngineProbe probe(std::make_shared<MockCredentialStore>(), transport,
                             [cfg] { return std::vector<CompatibleConfig>{cfg}; });
    REQUIRE(probe.probe(EngineHealthKey::forCompatible(cfg.id), {}) ==
            EngineHealth::Available);
}

// --- Cache semantics (60s TTL, macOS parity) ---

TEST_CASE("health cache: within TTL avoids re-probe", "[health][cache]") {
    auto probe = std::make_shared<CountingProbe>();
    probe->scripted["mtran"] = EngineHealth::Available;
    EngineHealthChecker checker(probe, 60.0, [] { return 100.0; });
    const auto key = EngineHealthKey::forBuiltin(TranslationEngineType::MtranServer);
    CHECK(checker.health(key) == EngineHealth::Available);
    CHECK(checker.health(key) == EngineHealth::Available);
    REQUIRE(probe->probeCount == 1);
}

TEST_CASE("health cache: expiry after TTL re-probes", "[health][cache]") {
    auto probe = std::make_shared<CountingProbe>();
    probe->scripted["mtran"] = EngineHealth::Available;
    double now = 100.0;
    EngineHealthChecker checker(probe, 60.0, [&now] { return now; });
    const auto key = EngineHealthKey::forBuiltin(TranslationEngineType::MtranServer);
    CHECK(checker.health(key) == EngineHealth::Available);
    now += 61.0;  // stale
    CHECK(checker.health(key) == EngineHealth::Available);
    REQUIRE(probe->probeCount == 2);
}

TEST_CASE("health cache: invalidateAll forces re-probe", "[health][cache]") {
    auto probe = std::make_shared<CountingProbe>();
    probe->scripted["mtran"] = EngineHealth::Available;
    EngineHealthChecker checker(probe, 60.0, [] { return 100.0; });
    const auto key = EngineHealthKey::forBuiltin(TranslationEngineType::MtranServer);
    CHECK(checker.health(key) == EngineHealth::Available);
    checker.invalidateAll();
    CHECK(checker.health(key) == EngineHealth::Available);
    REQUIRE(probe->probeCount == 2);
}

TEST_CASE("health cache: refresh bypasses the cache", "[health][cache]") {
    auto probe = std::make_shared<CountingProbe>();
    probe->scripted["mtran"] = EngineHealth::Available;
    EngineHealthChecker checker(probe, 60.0, [] { return 100.0; });
    const auto key = EngineHealthKey::forBuiltin(TranslationEngineType::MtranServer);
    CHECK(checker.refresh(key) == EngineHealth::Available);
    CHECK(checker.refresh(key) == EngineHealth::Available);
    REQUIRE(probe->probeCount == 2);
}
