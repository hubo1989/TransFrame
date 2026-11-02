// tests/ProvidersTests.cpp — per-engine request shape / response parsing /
// error classification (brief §2 + §7). MockHttpTransport verifies the exact
// request bodies so the docs/windows/engines/*.md samples stay honest.
#include "TestSupport.hpp"
#include "catch_amalgamated.hpp"

using namespace tf;
using namespace tf::tests;

// ===========================================================================
// MTranServer
// ===========================================================================
TEST_CASE("mtran: request shape and response parsing", "[mtran]") {
    auto transport = std::make_shared<MockHttpTransport>();
    transport->routes["http://localhost:8989/translate"] = {
        200, R"({"result":"你好","detected_language":"en"})"};
    auto cfg = MTranServerProvider::Configuration::parse("localhost:8989");
    REQUIRE(cfg.has_value());
    MTranServerProvider provider(*cfg, transport);

    TranslateRequest req;
    req.texts = {"Hello"};
    req.sourceLanguage = "en";
    req.targetLanguage = "zh-Hans";
    auto out = provider.translate(req);
    REQUIRE(out.results.has_value());
    REQUIRE((*out.results)[0].translatedText == "你好");
    REQUIRE((*out.results)[0].sourceLanguage == "en");

    REQUIRE(transport->requests.size() == 1);
    const auto& sent = transport->requests.front();
    REQUIRE(sent.method == "POST");
    REQUIRE(sent.headers.at("Content-Type") == "application/json");
    // Body: {"from":"en","text":"Hello","to":"zh"} (zh-Hans normalized).
    REQUIRE(sent.body.find("\"from\":\"en\"") != std::string::npos);
    REQUIRE(sent.body.find("\"to\":\"zh\"") != std::string::npos);
}

TEST_CASE("mtran: language normalization matrix", "[mtran]") {
    REQUIRE(MTranServerProvider::normalizeLanguageCode("zh-Hans") == "zh");
    REQUIRE(MTranServerProvider::normalizeLanguageCode("zh-CN") == "zh");
    REQUIRE(MTranServerProvider::normalizeLanguageCode("zh-Hant") == "zh-TW");
    REQUIRE(MTranServerProvider::normalizeLanguageCode("zh-TW") == "zh-TW");
    REQUIRE(MTranServerProvider::normalizeLanguageCode("en-US") == "en");
    REQUIRE(MTranServerProvider::normalizeLanguageCode("ja-JP") == "ja");
    REQUIRE(MTranServerProvider::normalizeLanguageCode("ko-KR") == "ko");
    REQUIRE(MTranServerProvider::normalizeLanguageCode("fr") == "fr");
}

TEST_CASE("mtran: configuration parsing rejects paths and bad hosts", "[mtran]") {
    REQUIRE(MTranServerProvider::Configuration::parse("localhost:8989").has_value());
    REQUIRE(MTranServerProvider::Configuration::parse("http://192.168.1.5:9000").has_value());
    REQUIRE(MTranServerProvider::Configuration::parse("localhost").has_value());
    REQUIRE_FALSE(MTranServerProvider::Configuration::parse("http://host/path").has_value());
    REQUIRE_FALSE(MTranServerProvider::Configuration::parse("bad host!").has_value());
    REQUIRE_FALSE(MTranServerProvider::Configuration::parse("host:notaport").has_value());
}

TEST_CASE("mtran: 503 maps to network error", "[mtran]") {
    auto transport = std::make_shared<MockHttpTransport>();
    transport->routes["http://127.0.0.1:8989/translate"] = {503, ""};
    MTranServerProvider provider(*MTranServerProvider::Configuration::parse("localhost:8989"),
                                 transport);
    TranslateRequest req;
    req.texts = {"Hello"};
    req.targetLanguage = "zh";
    auto out = provider.translate(req);
    REQUIRE(out.error.has_value());
    REQUIRE(out.error->category == ErrorCategory::Network);
}

// ===========================================================================
// LLM chat (OpenAI / Claude / Gemini / Ollama)
// ===========================================================================
TEST_CASE("llm openai: request shape + batching separators preserved",
          "[llm][openai]") {
    auto transport = std::make_shared<MockHttpTransport>();
    transport->routes["https://api.openai.com/v1/chat/completions"] = {
        200, R"({"choices":[{"message":{"content":"你好\n---\n世界"},"finish_reason":"stop"}]})"};
    EngineConfig cfg = EngineConfig::make(TranslationEngineType::OpenAI);
    auto store = credStoreWith(credentialAccount(TranslationEngineType::OpenAI), "sk-abc123xyz");
    LLMChatProvider provider(TranslationEngineType::OpenAI, cfg, store, transport);

    TranslateRequest req;
    req.texts = {"Hello", "World"};
    req.sourceLanguage = "en";
    req.targetLanguage = "zh-Hans";
    auto out = provider.translate(req);
    REQUIRE(out.results.has_value());
    REQUIRE(out.results->size() == 2);
    REQUIRE((*out.results)[0].translatedText == "你好");
    REQUIRE((*out.results)[1].translatedText == "世界");

    REQUIRE(transport->requests.size() == 1);
    const auto& sent = transport->requests.front();
    REQUIRE(sent.headers.at("Authorization") == "Bearer sk-abc123xyz");
    REQUIRE(sent.body.find("\"model\":\"gpt-4o-mini\"") != std::string::npos);
    REQUIRE(sent.body.find("\"temperature\":0.3") != std::string::npos);
    // Prompt embeds the batch separator (Swift separator hint).
    REQUIRE(sent.body.find("---") != std::string::npos);
}

TEST_CASE("llm openai: 401 → auth, 429 → quota", "[llm][openai]") {
    EngineConfig cfg = EngineConfig::make(TranslationEngineType::OpenAI);
    auto store = credStoreWith(credentialAccount(TranslationEngineType::OpenAI), "sk-x");

    auto t401 = std::make_shared<MockHttpTransport>();
    t401->routes["https://api.openai.com/v1/chat/completions"] = {401, "{}"};
    LLMChatProvider p401(TranslationEngineType::OpenAI, cfg, store, t401);
    TranslateRequest req;
    req.texts = {"x"};
    req.targetLanguage = "zh";
    auto out401 = p401.translate(req);
    REQUIRE(out401.error->category == ErrorCategory::Auth);

    auto t429 = std::make_shared<MockHttpTransport>();
    t429->routes["https://api.openai.com/v1/chat/completions"] = {429, "{}"};
    LLMChatProvider p429(TranslationEngineType::OpenAI, cfg, store, t429);
    auto out429 = p429.translate(req);
    REQUIRE(out429.error->category == ErrorCategory::Quota);
}

TEST_CASE("llm claude: endpoint + headers + body shape", "[llm][claude]") {
    auto transport = std::make_shared<MockHttpTransport>();
    transport->routes["https://api.anthropic.com/v1/messages"] = {
        200, R"({"content":[{"type":"text","text":"你好"}],"stop_reason":"end_turn"})"};
    EngineConfig cfg = EngineConfig::make(TranslationEngineType::Claude);
    auto store = credStoreWith(credentialAccount(TranslationEngineType::Claude), "sk-ant");
    LLMChatProvider provider(TranslationEngineType::Claude, cfg, store, transport);

    TranslateRequest req;
    req.texts = {"Hello"};
    req.sourceLanguage = "en";
    req.targetLanguage = "zh-Hans";
    auto out = provider.translate(req);
    REQUIRE(out.results.has_value());

    const auto& sent = transport->requests.front();
    REQUIRE(sent.headers.at("x-api-key") == "sk-ant");
    REQUIRE(sent.headers.at("anthropic-version") == "2023-06-01");
    // Claude body: no temperature/stream keys (Swift Claude branch).
    REQUIRE(sent.body.find("\"max_tokens\"") != std::string::npos);
    REQUIRE(sent.body.find("\"temperature\"") == std::string::npos);
}

TEST_CASE("llm gemini: openai-compatible endpoint used", "[llm][gemini]") {
    auto transport = std::make_shared<MockHttpTransport>();
    transport
        ->routes["https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"] = {
        200, R"({"choices":[{"message":{"content":"你好"},"finish_reason":"stop"}]})"};
    EngineConfig cfg = EngineConfig::make(TranslationEngineType::Gemini);
    auto store = credStoreWith(credentialAccount(TranslationEngineType::Gemini), "g-key");
    LLMChatProvider provider(TranslationEngineType::Gemini, cfg, store, transport);

    TranslateRequest req;
    req.texts = {"Hello"};
    req.targetLanguage = "zh-Hans";
    REQUIRE(provider.translate(req).results.has_value());
    REQUIRE(transport->requests.front().headers.at("Authorization") == "Bearer g-key");
}

TEST_CASE("llm ollama: keyless local endpoint", "[llm][ollama]") {
    auto transport = std::make_shared<MockHttpTransport>();
    transport->routes["http://127.0.0.1:11434/v1/chat/completions"] = {
        200, R"({"choices":[{"message":{"content":"你好"},"finish_reason":"stop"}]})"};
    EngineConfig cfg = EngineConfig::make(TranslationEngineType::Ollama);
    LLMChatProvider provider(TranslationEngineType::Ollama, cfg,
                             std::make_shared<MockCredentialStore>(), transport);
    REQUIRE(provider.isAvailable());  // ollama needs no key

    TranslateRequest req;
    req.texts = {"Hello"};
    req.targetLanguage = "zh-Hans";
    REQUIRE(provider.translate(req).results.has_value());
    REQUIRE(transport->requests.front().headers.count("Authorization") == 0);
}

TEST_CASE("llm: refuses API key over plain HTTP for non-localhost",
          "[llm][security]") {
    EngineConfig cfg = EngineConfig::make(TranslationEngineType::OpenAI);
    cfg.baseURL = "http://api.openai.com/v1";  // plain http, remote host
    auto store = credStoreWith(credentialAccount(TranslationEngineType::OpenAI), "sk-x");
    auto transport = std::make_shared<MockHttpTransport>();
    LLMChatProvider provider(TranslationEngineType::OpenAI, cfg, store, transport);

    TranslateRequest req;
    req.texts = {"x"};
    req.targetLanguage = "zh";
    auto out = provider.translate(req);
    REQUIRE(out.error.has_value());
    REQUIRE(out.error->category == ErrorCategory::Auth);
    REQUIRE(transport->requests.empty());  // guard fired before any request
}

TEST_CASE("llm: localhost keyless base keeps openai available", "[llm]") {
    EngineConfig cfg = EngineConfig::make(TranslationEngineType::OpenAI);
    cfg.baseURL = "http://localhost:1234/v1";
    LLMChatProvider provider(TranslationEngineType::OpenAI, cfg,
                             std::make_shared<MockCredentialStore>(),
                             std::make_shared<MockHttpTransport>());
    REQUIRE(provider.isAvailable());
}

// ===========================================================================
// Google / DeepL / Baidu
// ===========================================================================
TEST_CASE("google: request shape and array parsing", "[google]") {
    auto transport = std::make_shared<MockHttpTransport>();
    transport->routes["https://translation.googleapis.com/language/translate/v2?key=AIza-test"] = {
        200,
        R"({"data":{"translations":[{"translatedText":"你好","detectedSourceLanguage":"en"},
              {"translatedText":"世界"}]}})"};
    EngineConfig cfg = EngineConfig::make(TranslationEngineType::Google);
    auto store = credStoreWith(credentialAccount(TranslationEngineType::Google), "AIza-test");
    GoogleTranslateProvider provider(cfg, store, transport);

    TranslateRequest req;
    req.texts = {"Hello", "World"};
    req.targetLanguage = "zh-Hans";
    auto out = provider.translate(req);
    REQUIRE(out.results.has_value());
    REQUIRE((*out.results)[0].translatedText == "你好");
    REQUIRE((*out.results)[0].sourceLanguage == "en");  // detected source wins
    REQUIRE((*out.results)[1].translatedText == "世界");

    const auto& sent = transport->requests.front();
    REQUIRE(sent.method == "POST");
    REQUIRE(sent.body.find("\"q\":[\"Hello\",\"World\"]") != std::string::npos);
    REQUIRE(sent.body.find("\"target\":\"zh\"") != std::string::npos);
}

TEST_CASE("deepl: endpoint normalization + target casing + parsing", "[deepl]") {
    REQUIRE(DeepLProvider::normalizeEndpoint("") == "https://api.deepl.com/v2/translate");
    REQUIRE(DeepLProvider::normalizeEndpoint("https://api-free.deepl.com/v2") ==
            "https://api-free.deepl.com/v2/translate");
    REQUIRE(DeepLProvider::normalizeEndpoint("https://api.deepl.com/v2/translate") ==
            "https://api.deepl.com/v2/translate");
    REQUIRE(DeepLProvider::targetCode("zh-Hans") == "ZH");
    REQUIRE(DeepLProvider::targetCode("zh-Hant") == "ZH-HANT");
    REQUIRE(DeepLProvider::targetCode("en-US") == "EN");
    REQUIRE(DeepLProvider::sourceCode("EN") == "en");
    REQUIRE(DeepLProvider::sourceCode("auto").empty());

    auto transport = std::make_shared<MockHttpTransport>();
    transport->routes["https://api.deepl.com/v2/translate"] = {
        200,
        R"({"translations":[{"detected_source_language":"EN","text":"Hallo"}]})"};
    EngineConfig cfg = EngineConfig::make(TranslationEngineType::DeepL);
    auto store = credStoreWith(credentialAccount(TranslationEngineType::DeepL), "dk");
    DeepLProvider provider(cfg, store, transport);

    TranslateRequest req;
    req.texts = {"Hello"};
    req.targetLanguage = "de";
    auto out = provider.translate(req);
    REQUIRE(out.results.has_value());
    REQUIRE((*out.results)[0].translatedText == "Hallo");

    const auto& sent = transport->requests.front();
    REQUIRE(sent.headers.at("Authorization") == "DeepL-Auth-Key dk");
    REQUIRE(sent.body.find("\"target_lang\":\"DE\"") != std::string::npos);
}

TEST_CASE("deepl: 456 maps to quota", "[deepl]") {
    auto transport = std::make_shared<MockHttpTransport>();
    transport->routes["https://api.deepl.com/v2/translate"] = {456, "{}"};
    EngineConfig cfg = EngineConfig::make(TranslationEngineType::DeepL);
    auto store = credStoreWith(credentialAccount(TranslationEngineType::DeepL), "dk");
    DeepLProvider provider(cfg, store, transport);
    TranslateRequest req;
    req.texts = {"x"};
    req.targetLanguage = "de";
    auto out = provider.translate(req);
    REQUIRE(out.error->category == ErrorCategory::Quota);
}

TEST_CASE("baidu: MD5 sign known vector", "[baidu]") {
    // RFC 1321 vector: MD5("abc") = 900150983cd24fb0d6963f7d28e17f72.
    REQUIRE(BaiduProvider::md5Hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    REQUIRE(BaiduProvider::md5Hex("") == "d41d8cd98f00b204e9800998ecf8427e");
    REQUIRE(BaiduProvider::md5Hex("The quick brown fox jumps over the lazy dog") ==
            "9e107d9d372bb6826bd81d3542a419d6");
}

TEST_CASE("baidu: language mapping + request URL + parsing", "[baidu]") {
    REQUIRE(BaiduProvider::mapLanguageCode("zh-Hant") == "cht");
    REQUIRE(BaiduProvider::mapLanguageCode("ja") == "jp");
    REQUIRE(BaiduProvider::mapLanguageCode("ko") == "kor");
    REQUIRE(BaiduProvider::mapLanguageCode("fr") == "fra");
    REQUIRE(BaiduProvider::mapLanguageCode("es") == "spa");
    REQUIRE(BaiduProvider::mapLanguageCode("") == "auto");

    // Salt is random per call, so the canned-route mock cannot key on the
    // full URL; a prefix-answering transport records and answers any Baidu
    // request.
    struct AnyBaidu final : IHttpTransport {
        std::string body;
        std::vector<HttpRequest> seen;
        HttpOutcome send(const HttpRequest& request) override {
            seen.push_back(request);
            HttpResponse r;
            r.status = 200;
            r.body = body;
            return HttpOutcome::ok(std::move(r));
        }
    };
    auto transport = std::make_shared<AnyBaidu>();
    transport->body = R"({"from":"en","to":"zh","trans_result":[{"src":"Hello","dst":"你好"}]})";
    EngineConfig cfg = EngineConfig::make(TranslationEngineType::Baidu);
    auto store = credStoreWith(credentialAccount(TranslationEngineType::Baidu), "secret",
                               "20231205000");
    BaiduProvider provider(cfg, store, transport);

    TranslateRequest req;
    req.texts = {"Hello"};
    req.sourceLanguage = "en";
    req.targetLanguage = "zh-Hans";
    auto out = provider.translate(req);
    REQUIRE(out.results.has_value());
    REQUIRE((*out.results)[0].translatedText == "你好");

    // Request shape: GET with q/from/to/appid/salt/sign query params.
    REQUIRE(transport->seen.size() == 1);
    const HttpRequest& sent = transport->seen.front();
    REQUIRE(sent.method == "GET");
    const std::string prefix =
        "https://fanyi-api.baidu.com/api/trans/vip/translate?q=Hello&from=en&to=zh"
        "&appid=20231205000&salt=";
    REQUIRE(sent.url.rfind(prefix, 0) == 0);
    REQUIRE(sent.url.find("&sign=") != std::string::npos);
}

TEST_CASE("baidu: 54003 → quota, 54004 → auth", "[baidu]") {
    EngineConfig cfg = EngineConfig::make(TranslationEngineType::Baidu);
    TranslateRequest req;
    req.texts = {"x"};
    req.targetLanguage = "zh";

    // The mock routes on the full URL including salt/sign; use a wildcard-free
    // approach: transport with no matching route fails at network level, so
    // instead assert the error-code mapping directly through a crafted body
    // via a route keyed by the exact URL is impractical (random salt). The
    // mapping itself is covered by the parse branch below.
    auto transport = std::make_shared<MockHttpTransport>();
    // Route every Baidu URL: match by prefix is unavailable in the mock, so
    // hook via custom transport.
    struct AnyBaidu final : IHttpTransport {
        std::string body;
        HttpOutcome send(const HttpRequest&) override {
            HttpResponse r;
            r.status = 200;
            r.body = body;
            return HttpOutcome::ok(std::move(r));
        }
    };
    auto quota = std::make_shared<AnyBaidu>();
    quota->body = R"({"error_code":"54003","error_msg":"qps limited"})";
    BaiduProvider pQuota(cfg, credStoreWith(credentialAccount(TranslationEngineType::Baidu),
                                            "s", "a"),
                         quota);
    REQUIRE(pQuota.translate(req).error->category == ErrorCategory::Quota);

    auto auth = std::make_shared<AnyBaidu>();
    auth->body = R"({"error_code":"54004","error_msg":"invalid key"})";
    BaiduProvider pAuth(cfg, credStoreWith(credentialAccount(TranslationEngineType::Baidu),
                                           "s", "a"),
                        auth);
    REQUIRE(pAuth.translate(req).error->category == ErrorCategory::Auth);
}

// ===========================================================================
// Compatible instances
// ===========================================================================
TEST_CASE("compatible: multi-instance identity and request shape", "[compatible]") {
    auto transport = std::make_shared<MockHttpTransport>();
    transport->routes["http://127.0.0.1:8000/v1/chat/completions"] = {
        200, R"({"choices":[{"message":{"content":"你好"},"finish_reason":"stop"}]})"};
    transport->routes["http://127.0.0.1:9000/v1/chat/completions"] = {
        200, R"({"choices":[{"message":{"content":"您好"},"finish_reason":"stop"}]})"};

    CompatibleConfig a;
    a.id = Uuid::random().toString();
    a.displayName = "LM Studio";
    a.baseURL = "http://localhost:8000/v1";
    a.modelName = "qwen2.5";
    a.hasAPIKey = false;
    CompatibleConfig b;
    b.id = Uuid::random().toString();
    b.displayName = "vLLM";
    b.baseURL = "http://localhost:9000/v1";
    b.modelName = "llama3.1";
    b.hasAPIKey = false;

    EngineConfig cfg = EngineConfig::make(TranslationEngineType::Custom);
    CompatibleChatProvider pa(a, cfg, std::make_shared<MockCredentialStore>(), transport);
    CompatibleChatProvider pb(b, cfg, std::make_shared<MockCredentialStore>(), transport);

    REQUIRE(pa.id() == a.id);
    REQUIRE(pa.name() == "LM Studio");
    REQUIRE(pb.name() == "vLLM");

    TranslateRequest req;
    req.texts = {"Hello"};
    req.targetLanguage = "zh-Hans";
    auto outA = pa.translate(req);
    auto outB = pb.translate(req);
    REQUIRE(outA.results.has_value());
    REQUIRE(outB.results.has_value());
    REQUIRE((*outA.results)[0].translatedText == "你好");
    REQUIRE((*outB.results)[0].translatedText == "您好");

    REQUIRE(transport->requests.size() == 2);
    REQUIRE(transport->requests[0].url != transport->requests[1].url);
    // Instance model names in the bodies.
    REQUIRE(transport->requests[0].body.find("\"model\":\"qwen2.5\"") != std::string::npos);
    REQUIRE(transport->requests[1].body.find("\"model\":\"llama3.1\"") != std::string::npos);
}

TEST_CASE("compatible: resolveConfigurationSnapshot prefers saved instance",
          "[compatible]") {
    std::vector<CompatibleConfig> saved;
    CompatibleConfig savedCfg;
    savedCfg.id = Uuid::random().toString();
    savedCfg.displayName = "latest-name";
    savedCfg.baseURL = "http://localhost:8000/v1";
    savedCfg.modelName = "latest-model";
    savedCfg.hasAPIKey = false;
    saved.push_back(savedCfg);

    CompatibleConfig stale = savedCfg;
    stale.displayName = "stale";
    stale.modelName = "stale-model";
    auto resolved = CompatibleChatProvider::resolveConfigurationSnapshot(stale, saved);
    REQUIRE(resolved.id == savedCfg.id);
    REQUIRE(resolved.displayName == "latest-name");  // latest saved wins
}

TEST_CASE("compatible: verifyConnection uses models endpoint", "[compatible]") {
    auto transport = std::make_shared<MockHttpTransport>();
    transport->routes["http://127.0.0.1:8000/v1/models"] = {200, R"({"data":[]})"};
    CompatibleConfig cfg;
    cfg.id = Uuid::random().toString();
    cfg.displayName = "local";
    cfg.baseURL = "http://localhost:8000/v1";
    cfg.modelName = "m";
    cfg.hasAPIKey = false;
    CompatibleChatProvider provider(cfg, EngineConfig::make(TranslationEngineType::Custom),
                                    std::make_shared<MockCredentialStore>(), transport);
    REQUIRE_FALSE(provider.verifyConnection({}).has_value());
    REQUIRE(transport->requests.front().method == "GET");
}
