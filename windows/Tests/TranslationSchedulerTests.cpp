// tests/TranslationSchedulerTests.cpp — ordered fallback matrix + compare
// parallel + result cache (brief §7; vectors mirror
// TransFrameTests/OrderedChainTranslationTests.swift and
// CompareModeTranslationTests.swift).
#include "TestSupport.hpp"
#include "catch_amalgamated.hpp"

#include <chrono>

using namespace tf;
using namespace tf::tests;

namespace {

std::unique_ptr<MockTranslationProvider> makeMock(const char* id, TranslationEngineType engine,
                                                  MockTranslationProvider::Behavior b) {
    return std::make_unique<MockTranslationProvider>(id, engine, b);
}

SchedulerOutcome runOrdered(std::map<TranslationEngineType, ITranslationProvider*>& providers,
                            std::vector<EngineChainItem> chain,
                            const std::vector<std::string>& texts = {"Hello"}) {
    TranslationScheduler sched(mockDeps(providers));
    ChainRequest req;
    req.chain = std::move(chain);
    req.texts = texts;
    req.sourceLanguage = "en";
    req.targetLanguage = "zh-Hans";
    return sched.translateOrdered(req);
}

}  // namespace

// ===========================================================================
// Ordered chain — sequential fallback matrix
// ===========================================================================
TEST_CASE("ordered: first failure falls through to second engine",
          "[scheduler][ordered]") {
    auto a = makeMock("mtran", TranslationEngineType::MtranServer,
                      MockTranslationProvider::Behavior{
                          .available = true, .translateError = "not available"});
    auto b = makeMock("openai", TranslationEngineType::OpenAI,
                      MockTranslationProvider::Behavior{.available = true});
    std::map<TranslationEngineType, ITranslationProvider*> providers{
        {TranslationEngineType::MtranServer, a.get()},
        {TranslationEngineType::OpenAI, b.get()}};

    auto outcome = runOrdered(providers, {EngineChainItem::makeBuiltin(TranslationEngineType::MtranServer),
                                          EngineChainItem::makeBuiltin(TranslationEngineType::OpenAI)});
    REQUIRE(outcome.isSuccess());
    REQUIRE(outcome.bundle->primaryEngine == TranslationEngineType::OpenAI);
    // One failure + one success ride along in results (Swift behavior).
    REQUIRE(outcome.bundle->results.size() == 2);
    const auto successful = outcome.bundle->successfulEngines();
    REQUIRE(std::find(successful.begin(), successful.end(),
                      TranslationEngineType::OpenAI) != successful.end());
}

TEST_CASE("ordered: first success stops the chain", "[scheduler][ordered]") {
    auto a = makeMock("mtran", TranslationEngineType::MtranServer,
                      MockTranslationProvider::Behavior{.available = true});
    auto fallback = makeMock("openai", TranslationEngineType::OpenAI,
                             MockTranslationProvider::Behavior{.available = true});
    std::map<TranslationEngineType, ITranslationProvider*> providers{
        {TranslationEngineType::MtranServer, a.get()},
        {TranslationEngineType::OpenAI, fallback.get()}};

    auto outcome = runOrdered(providers, {EngineChainItem::makeBuiltin(TranslationEngineType::MtranServer),
                                          EngineChainItem::makeBuiltin(TranslationEngineType::OpenAI)});
    REQUIRE(outcome.isSuccess());
    REQUIRE(fallback->calls() == 0);
}

TEST_CASE("ordered: unavailable member skipped, not fatal", "[scheduler][ordered]") {
    auto dead = makeMock("mtran", TranslationEngineType::MtranServer,
                         MockTranslationProvider::Behavior{.available = false});
    auto live = makeMock("openai", TranslationEngineType::OpenAI,
                         MockTranslationProvider::Behavior{.available = true});
    std::map<TranslationEngineType, ITranslationProvider*> providers{
        {TranslationEngineType::MtranServer, dead.get()},
        {TranslationEngineType::OpenAI, live.get()}};

    auto outcome = runOrdered(providers, {EngineChainItem::makeBuiltin(TranslationEngineType::MtranServer),
                                          EngineChainItem::makeBuiltin(TranslationEngineType::OpenAI)});
    REQUIRE(outcome.isSuccess());
    REQUIRE(outcome.bundle->primaryEngine == TranslationEngineType::OpenAI);
    // The skipped member must appear as a failed result (Swift logs it).
    REQUIRE(outcome.bundle->results.size() == 2);
    REQUIRE_FALSE(outcome.bundle->results.front().isSuccess());
}

TEST_CASE("ordered: all failures aggregate per-engine errors", "[scheduler][ordered]") {
    auto a = makeMock("mtran", TranslationEngineType::MtranServer,
                      MockTranslationProvider::Behavior{.available = true, .translateError = "boom"});
    auto b = makeMock("openai", TranslationEngineType::OpenAI,
                      MockTranslationProvider::Behavior{
                          .available = true,
                          .error = ProviderError::quota("rate limited")});
    std::map<TranslationEngineType, ITranslationProvider*> providers{
        {TranslationEngineType::MtranServer, a.get()},
        {TranslationEngineType::OpenAI, b.get()}};

    auto outcome = runOrdered(providers, {EngineChainItem::makeBuiltin(TranslationEngineType::MtranServer),
                                          EngineChainItem::makeBuiltin(TranslationEngineType::OpenAI)});
    REQUIRE_FALSE(outcome.isSuccess());
    REQUIRE(outcome.failures.size() == 2);
    // Engine identity preserved in the aggregated entries (Swift PR #92).
    bool sawMtran = false, sawOpenAI = false;
    for (const auto& f : outcome.failures) {
        if (f.engine == TranslationEngineType::MtranServer) sawMtran = true;
        if (f.engine == TranslationEngineType::OpenAI) sawOpenAI = true;
    }
    REQUIRE(sawMtran);
    REQUIRE(sawOpenAI);
}

TEST_CASE("ordered: chain clamps to 5 entries", "[scheduler][ordered]") {
    std::map<TranslationEngineType, ITranslationProvider*> providers;
    std::vector<std::unique_ptr<MockTranslationProvider>> mocks;
    std::vector<EngineChainItem> chain;
    const std::vector<TranslationEngineType> engines = allTranslationEngines();
    for (std::size_t i = 0; i < 7 && i < engines.size(); ++i) {
        mocks.push_back(makeMock("e", engines[i], MockTranslationProvider::Behavior{
                                                      .available = true,
                                                      .translateError = "down",
                                                  }));
        providers[engines[i]] = mocks.back().get();
        chain.push_back(EngineChainItem::makeBuiltin(engines[i]));
    }
    // 7 entries but only 9 engines exist; use duplicates to reach 7.
    while (chain.size() < 7) chain.push_back(EngineChainItem::makeBuiltin(engines[0]));

    auto outcome = runOrdered(providers, chain);
    // All tried engines failed → aggregated failures == 5 (clamped).
    REQUIRE_FALSE(outcome.isSuccess());
    REQUIRE(outcome.failures.size() == EngineSelectionLimits::orderedChainLimit);
}

TEST_CASE("ordered: cancellation aborts the chain immediately", "[scheduler][ordered]") {
    auto slow = makeMock("mtran", TranslationEngineType::MtranServer,
                         MockTranslationProvider::Behavior{.available = true, .delayMs = 80});
    auto after = makeMock("openai", TranslationEngineType::OpenAI,
                          MockTranslationProvider::Behavior{.available = true});
    std::map<TranslationEngineType, ITranslationProvider*> providers{
        {TranslationEngineType::MtranServer, slow.get()},
        {TranslationEngineType::OpenAI, after.get()}};

    TranslationScheduler sched(mockDeps(providers));
    ChainRequest req;
    req.chain = {EngineChainItem::makeBuiltin(TranslationEngineType::MtranServer),
                 EngineChainItem::makeBuiltin(TranslationEngineType::OpenAI)};
    req.texts = {"Hello"};
    req.sourceLanguage = "en";
    req.targetLanguage = "zh-Hans";
    req.cancelToken = CancellationToken::alreadyCancelled();

    auto outcome = sched.translateOrdered(req);
    REQUIRE_FALSE(outcome.isSuccess());
    // Cancelled chain must NOT grind through the remaining entries.
    REQUIRE(after->calls() == 0);
}

TEST_CASE("ordered: dangling compatible UUID is skipped", "[scheduler][ordered]") {
    auto live = makeMock("mtran", TranslationEngineType::MtranServer,
                         MockTranslationProvider::Behavior{.available = true});
    std::map<TranslationEngineType, ITranslationProvider*> providers{
        {TranslationEngineType::MtranServer, live.get()}};
    CompatibleConfig gone;
    gone.id = Uuid::random().toString();
    gone.displayName = "deleted-instance";
    gone.baseURL = "http://localhost:1234/v1";
    gone.modelName = "m";
    gone.hasAPIKey = false;

    TranslationScheduler sched(mockDeps(providers, {gone}));
    ChainRequest req;
    req.chain = {EngineChainItem::makeCompatible(gone.id),
                 EngineChainItem::makeBuiltin(TranslationEngineType::MtranServer)};
    req.texts = {"Hello"};
    req.sourceLanguage = "en";
    req.targetLanguage = "zh-Hans";
    auto outcome = sched.translateOrdered(req);
    REQUIRE(outcome.isSuccess());
    REQUIRE(outcome.bundle->primaryEngine == TranslationEngineType::MtranServer);
}

// ===========================================================================
// Compare — parallel + selection order + cache
// ===========================================================================
TEST_CASE("compare: parallel execution returns one result per engine",
          "[scheduler][compare]") {
    auto a = makeMock("mtran", TranslationEngineType::MtranServer,
                      MockTranslationProvider::Behavior{.available = true, .resultPrefix = "A:"});
    auto b = makeMock("openai", TranslationEngineType::OpenAI,
                      MockTranslationProvider::Behavior{.available = true, .resultPrefix = "B:"});
    std::map<TranslationEngineType, ITranslationProvider*> providers{
        {TranslationEngineType::MtranServer, a.get()},
        {TranslationEngineType::OpenAI, b.get()}};

    TranslationScheduler sched(mockDeps(providers));
    CompareRequest req;
    req.engines = {EngineChainItem::makeBuiltin(TranslationEngineType::MtranServer),
                   EngineChainItem::makeBuiltin(TranslationEngineType::OpenAI)};
    req.texts = {"Hello"};
    req.sourceLanguage = "en";
    req.targetLanguage = "zh-Hans";
    auto outcome = sched.translateCompare(req);
    REQUIRE(outcome.isSuccess());
    REQUIRE(outcome.bundle->results.size() == 2);
    const auto successful = outcome.bundle->successfulEngines();
    REQUIRE(successful.size() == 2);
}

TEST_CASE("compare: one failure among successes still succeeds", "[scheduler][compare]") {
    auto dead = makeMock("mtran", TranslationEngineType::MtranServer,
                         MockTranslationProvider::Behavior{
                             .available = true, .translateError = "not available"});
    auto live = makeMock("openai", TranslationEngineType::OpenAI,
                         MockTranslationProvider::Behavior{.available = true});
    std::map<TranslationEngineType, ITranslationProvider*> providers{
        {TranslationEngineType::MtranServer, dead.get()},
        {TranslationEngineType::OpenAI, live.get()}};

    TranslationScheduler sched(mockDeps(providers));
    CompareRequest req;
    req.engines = {EngineChainItem::makeBuiltin(TranslationEngineType::MtranServer),
                   EngineChainItem::makeBuiltin(TranslationEngineType::OpenAI)};
    req.texts = {"Hello"};
    req.sourceLanguage = "en";
    req.targetLanguage = "zh-Hans";
    auto outcome = sched.translateCompare(req);
    REQUIRE(outcome.isSuccess());
    REQUIRE(outcome.bundle->results.size() == 2);
    const auto successful = outcome.bundle->successfulEngines();
    REQUIRE(successful.size() == 1);
    REQUIRE(successful.front() == TranslationEngineType::OpenAI);
}

TEST_CASE("compare: all failures aggregate with engine identity", "[scheduler][compare]") {
    auto a = makeMock("mtran", TranslationEngineType::MtranServer,
                      MockTranslationProvider::Behavior{.available = true, .translateError = "x"});
    auto b = makeMock("openai", TranslationEngineType::OpenAI,
                      MockTranslationProvider::Behavior{.available = true, .translateError = "y"});
    std::map<TranslationEngineType, ITranslationProvider*> providers{
        {TranslationEngineType::MtranServer, a.get()},
        {TranslationEngineType::OpenAI, b.get()}};

    TranslationScheduler sched(mockDeps(providers));
    CompareRequest req;
    req.engines = {EngineChainItem::makeBuiltin(TranslationEngineType::MtranServer),
                   EngineChainItem::makeBuiltin(TranslationEngineType::OpenAI)};
    req.texts = {"Hello"};
    req.sourceLanguage = "en";
    req.targetLanguage = "zh-Hans";
    auto outcome = sched.translateCompare(req);
    REQUIRE_FALSE(outcome.isSuccess());
    REQUIRE(outcome.failures.size() == 2);
}

TEST_CASE("compare: results preserve selection order despite completion order",
          "[scheduler][compare]") {
    // mtran is slow (100ms), openai fast — selection order is [mtran, openai].
    auto slow = makeMock("mtran", TranslationEngineType::MtranServer,
                         MockTranslationProvider::Behavior{.available = true, .delayMs = 100});
    auto fast = makeMock("openai", TranslationEngineType::OpenAI,
                         MockTranslationProvider::Behavior{.available = true});
    std::map<TranslationEngineType, ITranslationProvider*> providers{
        {TranslationEngineType::MtranServer, slow.get()},
        {TranslationEngineType::OpenAI, fast.get()}};

    TranslationScheduler sched(mockDeps(providers));
    CompareRequest req;
    req.engines = {EngineChainItem::makeBuiltin(TranslationEngineType::MtranServer),
                   EngineChainItem::makeBuiltin(TranslationEngineType::OpenAI)};
    req.texts = {"Hello"};
    req.sourceLanguage = "en";
    req.targetLanguage = "zh-Hans";
    auto outcome = sched.translateCompare(req);
    REQUIRE(outcome.isSuccess());
    REQUIRE(outcome.bundle->results.size() == 2);
    REQUIRE(outcome.bundle->results[0].engine == TranslationEngineType::MtranServer);
    REQUIRE(outcome.bundle->results[1].engine == TranslationEngineType::OpenAI);
}

TEST_CASE("compare: cache serves repeat views without re-translation",
          "[scheduler][compare][cache]") {
    struct Counter {
        int value_ = 0;
        int value() const { return value_; }
        void increment() { ++value_; }
    } provider;  // counts translate() calls
    struct CountingProvider final : ITranslationProvider {
        Counter& counter;
        explicit CountingProvider(Counter& c) : counter(c) {}
        std::string id() const override { return "mtran"; }
        std::string name() const override { return "mtran"; }
        TranslationEngineType engineType() const override {
            return TranslationEngineType::MtranServer;
        }
        bool isAvailable() const override { return true; }
        TranslationOutcome translate(const TranslateRequest& r) override {
            counter.increment();
            std::vector<TranslationResult> out;
            for (const auto& t : r.texts) out.emplace_back(t, "译:" + t, "en", r.targetLanguage);
            return TranslationOutcome::ok(std::move(out));
        }
        std::optional<ProviderError> verifyConnection(const CancellationToken&) override {
            return std::nullopt;
        }
    };
    CountingProvider counting{provider};
    std::map<TranslationEngineType, ITranslationProvider*> providers{
        {TranslationEngineType::MtranServer, &counting}};

    TranslationScheduler sched(mockDeps(providers));
    CompareRequest req;
    req.engines = {EngineChainItem::makeBuiltin(TranslationEngineType::MtranServer)};
    req.texts = {"Hello", "World"};
    req.sourceLanguage = "en";
    req.targetLanguage = "zh-Hans";

    auto first = sched.translateCompare(req);
    REQUIRE(first.isSuccess());
    REQUIRE(provider.value() == 1);

    // Second identical compare: cache hit, provider not called again
    // (brief FR-21 点击切换不重发请求).
    auto second = sched.translateCompare(req);
    REQUIRE(second.isSuccess());
    REQUIRE(provider.value() == 1);

    // Different texts → cache miss → provider called again.
    CompareRequest other = req;
    other.texts = {"Different"};
    auto third = sched.translateCompare(other);
    REQUIRE(third.isSuccess());
    REQUIRE(provider.value() == 2);

    // clearResultCache drops the entry.
    sched.clearResultCache();
    auto fourth = sched.translateCompare(req);
    REQUIRE(fourth.isSuccess());
    REQUIRE(provider.value() == 3);
}

TEST_CASE("compare: clamps to 4 engines", "[scheduler][compare]") {
    std::map<TranslationEngineType, ITranslationProvider*> providers;
    std::vector<std::unique_ptr<MockTranslationProvider>> mocks;
    std::vector<EngineChainItem> engines;
    for (const auto t : allTranslationEngines()) {
        mocks.push_back(makeMock("e", t, MockTranslationProvider::Behavior{
                                             .available = true, .translateError = "down"}));
        providers[t] = mocks.back().get();
        if (engines.size() < 6) engines.push_back(EngineChainItem::makeBuiltin(t));
    }

    TranslationScheduler sched(mockDeps(providers));
    CompareRequest req;
    req.engines = engines;
    req.texts = {"Hello"};
    req.sourceLanguage = "en";
    req.targetLanguage = "zh-Hans";
    auto outcome = sched.translateCompare(req);
    REQUIRE_FALSE(outcome.isSuccess());
    REQUIRE(outcome.failures.size() == EngineSelectionLimits::compareEnginesLimit);
}

// ===========================================================================
// Scene routing (FR-23)
// ===========================================================================
TEST_CASE("scene routing: bound chain overrides global", "[scheduler][scene]") {
    auto global = makeMock("mtran", TranslationEngineType::MtranServer,
                           MockTranslationProvider::Behavior{.available = true});
    auto scenePick = makeMock("openai", TranslationEngineType::OpenAI,
                              MockTranslationProvider::Behavior{.available = true});
    std::map<TranslationEngineType, ITranslationProvider*> providers{
        {TranslationEngineType::MtranServer, global.get()},
        {TranslationEngineType::OpenAI, scenePick.get()}};

    TranslationScheduler sched(mockDeps(providers));
    ChainRequest globalChain;
    globalChain.chain = {EngineChainItem::makeBuiltin(TranslationEngineType::MtranServer)};
    CompareRequest globalCompare;

    SceneEngineBinding binding;
    binding.scene = TranslationScene::TranslateAndInsert;
    binding.chain = {EngineChainItem::makeBuiltin(TranslationEngineType::OpenAI)};

    auto outcome = sched.translateForScene(globalChain, globalCompare, binding, {"Hi"}, "en",
                                           "zh-Hans", {});
    REQUIRE(outcome.isSuccess());
    REQUIRE(outcome.bundle->primaryEngine == TranslationEngineType::OpenAI);
}

TEST_CASE("scene routing: unbound scene uses global chain", "[scheduler][scene]") {
    auto global = makeMock("mtran", TranslationEngineType::MtranServer,
                           MockTranslationProvider::Behavior{.available = true});
    std::map<TranslationEngineType, ITranslationProvider*> providers{
        {TranslationEngineType::MtranServer, global.get()}};

    TranslationScheduler sched(mockDeps(providers));
    ChainRequest globalChain;
    globalChain.chain = {EngineChainItem::makeBuiltin(TranslationEngineType::MtranServer)};
    SceneEngineBinding unbound;  // empty binding
    unbound.scene = TranslationScene::Screenshot;

    auto outcome = sched.translateForScene(globalChain, CompareRequest{}, unbound, {"Hi"}, "en",
                                           "zh-Hans", {});
    REQUIRE(outcome.isSuccess());
    REQUIRE(outcome.bundle->primaryEngine == TranslationEngineType::MtranServer);
}
