// tests/OcrAndTextSelectionTests.cpp — OCR mapping/parsers (Windows/Paddle/
// VLM), VisualLineGrouping vectors, KeyStore account naming/masking,
// TextExtractor clipboard fallback, InsertionService re-validation matrix.
#include "TestSupport.hpp"
#include "tf/OcrEngines.hpp"
#include "tf/TextSelectionInsertion.hpp"
#include "tf/VisualLineGrouping.hpp"
#include "catch_amalgamated.hpp"

using namespace tf;
using namespace tf::tests;

// ===========================================================================
// WindowsOcrEngine
// ===========================================================================
TEST_CASE("windows ocr: normalized bbox mapping", "[ocr][windows]") {
    std::vector<WindowsOcrEngine::RawLine> lines = {
        {"Hello", 100, 200, 300, 40},
        {"世界", 200, 300, 100, 40},
    };
    auto segments = WindowsOcrEngine::mapLines(lines, 1000, 500);
    REQUIRE(segments.size() == 2);
    REQUIRE(segments[0].text == "Hello");
    REQUIRE(segments[0].boundingBox.x == Catch::Approx(0.1));
    REQUIRE(segments[0].boundingBox.y == Catch::Approx(0.4));
    REQUIRE(segments[0].boundingBox.width == Catch::Approx(0.3));
    REQUIRE(segments[0].boundingBox.height == Catch::Approx(0.08));
    // No confidence from Windows OCR → 1.0.
    REQUIRE(segments[0].confidence == Catch::Approx(1.0f));
}

TEST_CASE("windows ocr: language pack gating", "[ocr][windows]") {
    WindowsOcrEngine::LanguageProvider provider = [] {
        return std::vector<WindowsOcrEngine::Language>{{"en-US", "English"}};
    };
    WindowsOcrEngine engine(provider, [](const OcrImage&, const std::vector<std::string>&,
                                         std::vector<WindowsOcrEngine::RawLine>&) {
        return std::nullopt;
    });
    REQUIRE(engine.isAvailable());
    REQUIRE(engine.hasLanguagePack("en"));
    REQUIRE(engine.hasLanguagePack("en-US"));
    // No Chinese pack installed → gray gate (FR-15).
    REQUIRE_FALSE(engine.hasLanguagePack("zh-Hans"));
    REQUIRE_FALSE(engine.hasLanguagePack("zh"));
}

TEST_CASE("windows ocr: empty language list → unavailable", "[ocr][windows]") {
    WindowsOcrEngine engine([] { return std::vector<WindowsOcrEngine::Language>{}; },
                            [](const OcrImage&, const std::vector<std::string>&,
                               std::vector<WindowsOcrEngine::RawLine>&) {
                                return std::nullopt;
                            });
    REQUIRE_FALSE(engine.isAvailable());
}

TEST_CASE("windows ocr: recognize maps raw lines end-to-end", "[ocr][windows]") {
    WindowsOcrEngine engine(
        [] { return std::vector<WindowsOcrEngine::Language>{{"en-US", "English"}}; },
        [](const OcrImage&, const std::vector<std::string>& tags,
           std::vector<WindowsOcrEngine::RawLine>& out) {
            REQUIRE(tags.size() == 1);
            REQUIRE(tags[0] == "en-US");
            out.push_back({"line one", 0, 0, 500, 30});
            return std::nullopt;
        });
    OcrImage image;
    image.width = 1000;
    image.height = 100;
    image.bgra.assign(1000 * 100 * 4, 0x80);
    auto out = engine.recognize(image);
    REQUIRE(out.isOk());
    REQUIRE(out.value.observations.size() == 1);
    REQUIRE(out.value.observations[0].text == "line one");
    REQUIRE(out.value.observations[0].boundingBox.width == Catch::Approx(0.5));
}

TEST_CASE("windows ocr: tag normalization", "[ocr][windows]") {
    REQUIRE(WindowsOcrEngine::normalizeLanguageTag("zh") == "zh-Hans");
    REQUIRE(WindowsOcrEngine::normalizeLanguageTag("zh-CN") == "zh-Hans");
    REQUIRE(WindowsOcrEngine::normalizeLanguageTag("zh-TW") == "zh-Hant");
    REQUIRE(WindowsOcrEngine::normalizeLanguageTag("en") == "en-US");
    REQUIRE(WindowsOcrEngine::normalizeLanguageTag("ja-JP") == "ja");
    REQUIRE(WindowsOcrEngine::normalizeLanguageTag("fr") == "fr");
}

// ===========================================================================
// PaddleOcrEngine protocol
// ===========================================================================
TEST_CASE("paddle: fast-mode JSON parsing with normalization", "[ocr][paddle]") {
    const std::string json =
        R"({"res":{"rec_texts":["设计","plugins"],"rec_scores":[0.98,0.75],
             "rec_boxes":[[10,20,110,60],[150,22,260,58]]}})";
    auto segments = PaddleOcrEngine::parseFastMode(json, 500, 200);
    REQUIRE(segments.size() == 2);
    REQUIRE(segments[0].text == "设计");
    REQUIRE(segments[0].confidence == Catch::Approx(0.98f));
    REQUIRE(segments[0].boundingBox.x == Catch::Approx(10.0 / 500));
    REQUIRE(segments[0].boundingBox.y == Catch::Approx(20.0 / 200));
    REQUIRE(segments[0].boundingBox.width == Catch::Approx(100.0 / 500));
    REQUIRE(segments[1].confidence == Catch::Approx(0.75f));
}

TEST_CASE("paddle: fast-mode confidence filter", "[ocr][paddle]") {
    const std::string json =
        R"({"res":{"rec_texts":["a","b"],"rec_scores":[0.95,0.10],"rec_boxes":[]}})";
    auto segments = PaddleOcrEngine::parseFastMode(json, 100, 100, 0.5f);
    REQUIRE(segments.size() == 1);
    REQUIRE(segments[0].text == "a");
}

TEST_CASE("paddle: fast-mode fallback bbox when boxes missing", "[ocr][paddle]") {
    const std::string json = R"({"res":{"rec_texts":["x","y"]}})";
    auto segments = PaddleOcrEngine::parseFastMode(json, 100, 100);
    REQUIRE(segments.size() == 2);
    REQUIRE(segments[1].boundingBox.y == Catch::Approx(0.1));  // stacked fallback
}

TEST_CASE("paddle: precise-mode parsing skips non-text blocks", "[ocr][paddle]") {
    const std::string json =
        R"({"res":{"parsing_res_list":[
             {"block_content":"正文文本","block_bbox":[0,0,400,50],"block_label":"text"},
             {"block_content":"figure 1","block_bbox":[0,60,400,200],"block_label":"figure"},
             {"block_content":"表格","block_bbox":[0,210,400,300],"block_label":"table"},
             {"block_content":"第二段","block_bbox":[0,310,400,360]}
           ]}})";
    auto segments = PaddleOcrEngine::parsePreciseMode(json, 500, 500);
    REQUIRE(segments.size() == 2);  // figure/table skipped
    REQUIRE(segments[0].text == "正文文本");
    REQUIRE(segments[0].confidence == Catch::Approx(0.9f));
    REQUIRE(segments[1].text == "第二段");
}

TEST_CASE("paddle: python literal → JSON conversion", "[ocr][paddle]") {
    const std::string py = "{'res': {'rec_texts': ['文本'], 'flag': True, 'none': None}}";
    const std::string converted = PaddleOcrEngine::pythonDictToJson(py);
    REQUIRE(converted.find("\"res\"") != std::string::npos);
    REQUIRE(converted.find("true") != std::string::npos);
    REQUIRE(converted.find("null") != std::string::npos);
    // Result must parse as JSON now.
    auto segments = PaddleOcrEngine::parseFastMode(converted, 100, 100);
    REQUIRE(segments.size() == 1);
    REQUIRE(segments[0].text == "文本");
}

TEST_CASE("paddle: trailing-dot floats normalized", "[ocr][paddle]") {
    const std::string py = "{'res': {'rec_scores': [8., 5.]}}";
    const std::string converted = PaddleOcrEngine::pythonDictToJson(py);
    REQUIRE(converted.find("8.0") != std::string::npos);
    REQUIRE(converted.find("5.0") != std::string::npos);
}

TEST_CASE("paddle: stderr {'res': ...} extraction with ANSI codes",
          "[ocr][paddle]") {
    const std::string ansi = "\x1b[32m{'res': {'rec_texts': [\"ok\"]}}\x1b[0m noise";
    const std::string extracted = PaddleOcrEngine::extractResultJson("", ansi);
    REQUIRE(extracted.find("\x1b") == std::string::npos);
    auto segments = PaddleOcrEngine::parseFastMode(extracted, 100, 100);
    REQUIRE(segments.size() == 1);
    REQUIRE(segments[0].text == "ok");
}

TEST_CASE("paddle: fast/precise argv shapes", "[ocr][paddle]") {
    PaddleOcrEngine::Configuration fast;
    fast.mode = PaddleOcrEngine::Mode::Fast;
    fast.executablePath = "paddleocr";
    PaddleOcrEngine fastEngine(fast, [](const std::string&, const std::vector<std::string>&,
                                        int, std::string&, std::string&) {
                                   return std::nullopt;
                               },
                               [](const std::string&) { return true; });
    auto fastArgs = fastEngine.buildArguments("img.png");
    REQUIRE(fastArgs[0] == "ocr");
    REQUIRE(std::find(fastArgs.begin(), fastArgs.end(), "img.png") != fastArgs.end());
    REQUIRE(std::find(fastArgs.begin(), fastArgs.end(), "--use_angle_cls") != fastArgs.end());

    PaddleOcrEngine::Configuration precise;
    precise.mode = PaddleOcrEngine::Mode::Precise;
    PaddleOcrEngine preciseEngine(precise, [](const std::string&, const std::vector<std::string>&,
                                              int, std::string&, std::string&) {
                                     return std::nullopt;
                                 },
                                  [](const std::string&) { return true; });
    auto preciseArgs = preciseEngine.buildArguments("img.png");
    REQUIRE(preciseArgs[0] == "doc_parser");
    REQUIRE(std::find(preciseArgs.begin(), preciseArgs.end(), "--pipeline_version") !=
            preciseArgs.end());
    REQUIRE(std::find(preciseArgs.begin(), preciseArgs.end(), "v1.5") != preciseArgs.end());
}

TEST_CASE("paddle: availability gates on executable", "[ocr][paddle]") {
    PaddleOcrEngine::Configuration cfg;
    cfg.executablePath = "";
    PaddleOcrEngine engine(cfg, [](const std::string&, const std::vector<std::string>&, int,
                                   std::string&, std::string&) { return std::nullopt; },
                           [](const std::string&) { return true; });
    REQUIRE_FALSE(engine.isAvailable());
}

TEST_CASE("paddle: health cache TTL", "[ocr][paddle]") {
    PaddleOcrHealthCache cache(5.0);
    REQUIRE_FALSE(cache.cached("paddleocr", 0.0).has_value());
    cache.store("paddleocr", true, 1.0);
    REQUIRE(cache.cached("paddleocr", 4.0).value() == true);
    REQUIRE_FALSE(cache.cached("paddleocr", 6.5).has_value());  // stale
    cache.invalidateAll();
    REQUIRE_FALSE(cache.cached("paddleocr", 6.6).has_value());
}

// ===========================================================================
// VlmOcrEngine
// ===========================================================================
TEST_CASE("vlm: five provider slots exposed", "[ocr][vlm]") {
    auto providers = VlmOcrEngine::supportedProviders();
    REQUIRE(providers.size() == 5);
    REQUIRE(std::find(providers.begin(), providers.end(), VlmProviderType::OpenAI) !=
            providers.end());
    REQUIRE(std::find(providers.begin(), providers.end(), VlmProviderType::Claude) !=
            providers.end());
    REQUIRE(std::find(providers.begin(), providers.end(), VlmProviderType::Gemini) !=
            providers.end());
    REQUIRE(std::find(providers.begin(), providers.end(), VlmProviderType::Ollama) !=
            providers.end());
    REQUIRE(std::find(providers.begin(), providers.end(), VlmProviderType::PaddleOCR) !=
            providers.end());
}

TEST_CASE("vlm: segment parsing from chat completion with fenced JSON",
          "[ocr][vlm]") {
    // Chat completion content embeds a ```json fenced array; the inner quotes
    // are JSON-escaped inside the content string (real VLM output shape).
    const std::string fenced = "[{\"text\":\"hello\",\"confidence\":0.9,"
                               "\"box\":{\"x\":0.1,\"y\":0.2,\"width\":0.3,\"height\":0.1}}]";
    std::string escaped;
    for (char c : fenced) {
        if (c == '"' || c == '\\') escaped += '\\';
        escaped += c;
    }
    const std::string raw = std::string(R"({"choices":[{"message":{"content":"```json\n)") +
                            escaped + R"(\n```"},"finish_reason":"stop"}]})";
    auto segments = VlmOcrEngine::parseSegments(raw, 100, 100);
    REQUIRE(segments.size() == 1);
    REQUIRE(segments[0].text == "hello");
    REQUIRE(segments[0].confidence == Catch::Approx(0.9f));
    REQUIRE(segments[0].boundingBox.x == Catch::Approx(0.1));
}

TEST_CASE("vlm: pixel bbox array normalized", "[ocr][vlm]") {
    const std::string raw =
        R"([{"text":"px","confidence":0.8,"box":[100,50,300,90]}])";
    auto segments = VlmOcrEngine::parseSegments(raw, 1000, 500);
    REQUIRE(segments.size() == 1);
    REQUIRE(segments[0].boundingBox.x == Catch::Approx(0.1));
    REQUIRE(segments[0].boundingBox.y == Catch::Approx(0.1));
    REQUIRE(segments[0].boundingBox.width == Catch::Approx(0.2));
}

TEST_CASE("vlm: ollama slot keyless-available, openai slot needs key",
          "[ocr][vlm]") {
    auto store = std::make_shared<MockCredentialStore>();
    VlmOcrEngine engine(
        [](VlmProviderType, const std::string&, const OcrImage&, std::string& out) {
            out = R"([{"text":"x"}])";
            return std::nullopt;
        },
        store);
    engine.setActiveProvider(VlmProviderType::Ollama);
    REQUIRE(engine.isAvailable());
    engine.setActiveProvider(VlmProviderType::OpenAI);
    REQUIRE_FALSE(engine.isAvailable());
    store->save("vlm_api_key_openai", StoredCredentials{"sk", "", {}});
    REQUIRE(engine.isAvailable());
}

// ===========================================================================
// VisualLineGrouping (macOS-shared vectors)
// ===========================================================================
TEST_CASE("visual line grouping: inline code fragments join into one line",
          "[grouping]") {
    // Same visual line: y-overlap full, small gaps.
    std::vector<TextSegment> fragments = {
        TextSegment::make("main", NormRect{0.10, 0.50, 0.08, 0.03}),
        TextSegment::make("(", NormRect{0.19, 0.50, 0.01, 0.03}),
        TextSegment::make(")", NormRect{0.21, 0.50, 0.01, 0.03}),
        TextSegment::make("{", NormRect{0.23, 0.50, 0.01, 0.03}),
        TextSegment::make("return", NormRect{0.25, 0.50, 0.09, 0.03}),
        TextSegment::make("0;", NormRect{0.35, 0.50, 0.03, 0.03}),
    };
    auto merged = VisualLineGrouping::mergeLines(fragments);
    REQUIRE(merged.size() == 1);
    REQUIRE(merged[0].text == "main ( ) { return 0;");
}

TEST_CASE("visual line grouping: two separate lines stay separate",
          "[grouping]") {
    std::vector<TextSegment> fragments = {
        TextSegment::make("first line", NormRect{0.1, 0.10, 0.5, 0.05}),
        TextSegment::make("second line", NormRect{0.1, 0.40, 0.5, 0.05}),
    };
    auto merged = VisualLineGrouping::mergeLines(fragments);
    REQUIRE(merged.size() == 2);
    REQUIRE(merged[0].text == "first line");
    REQUIRE(merged[1].text == "second line");
}

TEST_CASE("visual line grouping: underscore fragments glue without space",
          "[grouping]") {
    // Nearly touching (gap ≤ 25% height) + identifier boundary → no space.
    std::vector<TextSegment> fragments = {
        TextSegment::make("DESIGMA", NormRect{0.10, 0.50, 0.20, 0.04}),
        TextSegment::make("_PLUGINS_TOKEN", NormRect{0.299, 0.50, 0.30, 0.04}),
    };
    auto merged = VisualLineGrouping::mergeLines(fragments);
    REQUIRE(merged.size() == 1);
    REQUIRE(merged[0].text == "DESIGMA_PLUGINS_TOKEN");
}

TEST_CASE("visual line grouping: CJK fragments glue without space", "[grouping]") {
    std::vector<TextSegment> fragments = {
        TextSegment::make("屏幕", NormRect{0.10, 0.50, 0.10, 0.04}),
        TextSegment::make("翻译", NormRect{0.21, 0.50, 0.10, 0.04}),
    };
    auto merged = VisualLineGrouping::mergeLines(fragments);
    REQUIRE(merged.size() == 1);
    REQUIRE(merged[0].text == "屏幕翻译");
}

TEST_CASE("visual line grouping: wide gap splits columns", "[grouping]") {
    // Gap 3× average height keeps them together at the boundary; beyond splits.
    std::vector<TextSegment> close = {
        TextSegment::make("colA", NormRect{0.10, 0.50, 0.10, 0.04}),
        TextSegment::make("colB", NormRect{0.22, 0.50, 0.10, 0.04}),  // gap 0.02 < 3*0.04
    };
    REQUIRE(VisualLineGrouping::mergeLines(close).size() == 1);

    std::vector<TextSegment> far = {
        TextSegment::make("colA", NormRect{0.10, 0.50, 0.10, 0.04}),
        TextSegment::make("colB", NormRect{0.60, 0.50, 0.10, 0.04}),  // gap 0.40 > 3*0.04
    };
    REQUIRE(VisualLineGrouping::mergeLines(far).size() == 2);
}

TEST_CASE("visual line grouping: averaged confidence + union bbox", "[grouping]") {
    std::vector<TextSegment> fragments = {
        TextSegment::make("A", NormRect{0.10, 0.50, 0.10, 0.04}, 0.8f),
        TextSegment::make("B", NormRect{0.25, 0.49, 0.10, 0.06}, 0.6f),
    };
    auto merged = VisualLineGrouping::mergeLines(fragments);
    REQUIRE(merged.size() == 1);
    REQUIRE(merged[0].confidence == Catch::Approx(0.7f));
    REQUIRE(merged[0].boundingBox.x == Catch::Approx(0.10));
    REQUIRE(merged[0].boundingBox.y == Catch::Approx(0.49));
    REQUIRE(merged[0].boundingBox.maxX() == Catch::Approx(0.35));
    REQUIRE(merged[0].boundingBox.height == Catch::Approx(0.06));  // 0.55 − 0.49 union
}

// ===========================================================================
// KeyStore naming + masking
// ===========================================================================
TEST_CASE("keystore: account naming TransFrame/<engine>/<instance>",
          "[keystore]") {
    REQUIRE(credentialAccount(TranslationEngineType::OpenAI) == "openai");
    REQUIRE(credentialAccountForCompatible("ABC-123") == "custom:ABC-123");
    // Full target name assembled by CredManKeyStore as "TransFrame/" + account.
    REQUIRE(maskApiKey("sk-abcdefghijklmnop") == "sk-****mnop");
    REQUIRE(maskApiKey("short") == "****");
    REQUIRE(maskApiKey("exactly8c") == "exa****ly8c");  // first3 + **** + last4
}

TEST_CASE("keystore: mock store roundtrip", "[keystore]") {
    MockCredentialStore store;
    StoredCredentials in;
    in.apiKey = "sk-test";
    in.appID = "app-1";
    in.additional["extra"] = "v";
    store.save(credentialAccount(TranslationEngineType::Baidu), in);
    REQUIRE(store.has(credentialAccount(TranslationEngineType::Baidu)));
    auto out = store.load(credentialAccount(TranslationEngineType::Baidu));
    REQUIRE(out.has_value());
    REQUIRE(out->apiKey == "sk-test");
    REQUIRE(out->appID == "app-1");
    REQUIRE(out->additional.at("extra") == "v");
    REQUIRE(store.remove(credentialAccount(TranslationEngineType::Baidu)));
    REQUIRE_FALSE(store.has(credentialAccount(TranslationEngineType::Baidu)));
}

// ===========================================================================
// TextExtractor — UIA first, clipboard fallback with restore
// ===========================================================================
namespace {

class MockUiaElement final : public IUiaElement {
public:
    MockUiaElement(std::string selection, bool editable, std::string runtimeId)
        : selectionText_(std::move(selection)),
          editable_(editable),
          runtimeId_(std::move(runtimeId)) {}
    std::optional<std::string> getSelectionText() const override {
        if (selectionProvided_) return selectionText_;
        return std::nullopt;
    }
    bool isEditable() const override { return editable_; }
    std::string runtimeId() const override { return runtimeId_; }

    bool selectionProvided_ = true;  // false = no TextPattern
    std::string selectionText_;
    bool editable_;
    std::string runtimeId_;
};

class MockHost : public TextExtractorHost {
public:
    MockHost() {
        // Wire the focused-element seam: reads the uia* fields at call time
        // so tests can flip them after construction.
        focusedElement = [this] { return makeFocused(); };
    }

    std::string uiaSelection;
    bool uiaHasPattern = true;
    bool uiaEditable = true;

    // Clipboard simulation.
    std::string clipboardTextValue;
    std::int64_t sequence = 100;
    std::string pendingAfterCopy;  // text that appears after sendCopyShortcut
    std::vector<std::string> clipboardHistory;  // what the "app" set
    int copyCalls = 0;
    int pasteCalls = 0;
    bool failCopy = false;
    bool failPaste = false;

    ClipboardBackup backupClipboard() override {
        ClipboardBackup b;
        b.text = clipboardTextValue;
        b.sequenceNumber = sequence;
        return b;
    }
    void restoreClipboard(const ClipboardBackup& backup) override {
        clipboardTextValue = backup.text;
        sequence += 1;
    }
    void clearClipboard() override {
        clipboardTextValue.clear();
        sequence += 1;
    }
    std::int64_t setClipboardText(const std::string& text) override {
        clipboardTextValue = text;
        clipboardHistory.push_back(text);
        sequence += 1;
        return sequence;
    }
    std::int64_t clipboardSequenceNumber() override { return sequence; }
    std::optional<std::string> clipboardText() override {
        if (clipboardTextValue.empty()) return std::nullopt;
        return clipboardTextValue;
    }
    bool sendCopyShortcut() override {
        ++copyCalls;
        if (failCopy) return false;
        clipboardTextValue = pendingAfterCopy;
        sequence += 1;
        return true;
    }
    bool sendPasteShortcut() override {
        ++pasteCalls;
        return !failPaste;
    }
    std::string foregroundProcessName() override { return "TestApp.exe"; }
    std::uint32_t foregroundProcessId() override { return 4242; }
    void sleepMilliseconds(int) override {}

    UiaElementPtr makeFocused() {
        auto el = std::make_unique<MockUiaElement>(uiaSelection, uiaEditable, "rt-1");
        el->selectionProvided_ = uiaHasPattern;
        return el;
    }
};

}  // namespace

TEST_CASE("text extraction: UIA GetSelection preferred (clipboard untouched)",
          "[selection]") {
    MockHost host;
    host.uiaSelection = "selected text";
    host.pendingAfterCopy = "SHOULD NOT APPEAR";
    TextExtractor extractor(host);
    auto out = extractor.captureSelectedText(false);
    REQUIRE(out.selection.has_value());
    REQUIRE(out.selection->text == "selected text");
    REQUIRE(out.selection->sourceProcessId == 4242);
    REQUIRE(host.copyCalls == 0);  // UIA path never touches the clipboard
}

TEST_CASE("text extraction: UIA unsupported falls back to Ctrl+C and restores",
          "[selection]") {
    MockHost host;
    host.uiaHasPattern = false;
    host.clipboardTextValue = "original clipboard";
    host.pendingAfterCopy = "copied selection";
    TextExtractor extractor(host);
    auto out = extractor.captureSelectedText(false);
    REQUIRE(out.selection.has_value());
    REQUIRE(out.selection->text == "copied selection");
    REQUIRE(host.copyCalls == 1);
    // Original clipboard restored (Swift PasteboardBackup.restore).
    REQUIRE(host.clipboardTextValue == "original clipboard");
}

TEST_CASE("text extraction: clipboard fallback disabled → no copy attempt",
          "[selection]") {
    MockHost host;
    host.uiaHasPattern = false;
    host.pendingAfterCopy = "x";
    TextExtractor extractor(host, /*allowClipboardFallback=*/false);
    auto out = extractor.captureSelectedText(false);
    REQUIRE(out.error.has_value());
    REQUIRE(host.copyCalls == 0);
}

TEST_CASE("text extraction: no selection → error after polling", "[selection]") {
    MockHost host;
    host.uiaHasPattern = false;
    host.pendingAfterCopy = "";  // nothing gets copied
    TextExtractor extractor(host, true, 3, 0);
    auto out = extractor.captureSelectedText(false);
    REQUIRE(out.error.has_value());
    REQUIRE(out.error->category == ErrorCategory::Content);
}

TEST_CASE("text extraction: bindForInsertion captures editable target id",
          "[selection]") {
    MockHost host;
    host.uiaSelection = "hello";
    TextExtractor extractor(host);
    auto out = extractor.captureSelectedText(true);
    REQUIRE(out.selection.has_value());
    REQUIRE(out.selection->insertionTargetId == "rt-1");
}

// ===========================================================================
// InsertionService — re-validation matrix (FR-29 绝不粘贴)
// ===========================================================================
TEST_CASE("insertion: valid target pastes translated text then restores",
          "[insertion]") {
    MockHost host;
    host.uiaSelection = "hello";
    TextSelection captured;
    captured.text = "hello";
    captured.sourceProcessId = 4242;
    captured.insertionTargetId = "rt-1";
    host.clipboardTextValue = "user's clipboard";

    InsertionService service(host);
    InsertRequest req;
    req.translatedText = "你好";
    req.capturedSelection = captured;
    std::string error;
    auto result = service.insert(req, error);
    REQUIRE(result == InsertionOutcome::Completed);
    REQUIRE(host.pasteCalls == 1);
    REQUIRE(host.clipboardHistory.size() == 1);
    REQUIRE(host.clipboardHistory.front() == "你好");
    // Clipboard restored after the paste window.
    REQUIRE(host.clipboardTextValue == "user's clipboard");
}

TEST_CASE("insertion: selection changed → abort, never paste", "[insertion]") {
    MockHost host;
    host.uiaSelection = "DIFFERENT TEXT NOW";  // user edited during translation
    TextSelection captured;
    captured.text = "hello";
    captured.insertionTargetId = "rt-1";
    InsertionService service(host);
    InsertRequest req;
    req.translatedText = "你好";
    req.capturedSelection = captured;
    std::string error;
    auto result = service.insert(req, error);
    REQUIRE(result == InsertionOutcome::Failed);
    REQUIRE(error.find("TargetChanged") != std::string::npos);
    REQUIRE(host.pasteCalls == 0);  // 绝不粘贴
    REQUIRE(host.clipboardHistory.empty());
}

TEST_CASE("insertion: focused element changed → abort", "[insertion]") {
    MockHost host;
    host.uiaHasPattern = false;  // no focused element with pattern
    TextSelection captured;
    captured.text = "hello";
    captured.insertionTargetId = "rt-1";
    InsertionService service(host);
    InsertRequest req;
    req.translatedText = "你好";
    req.capturedSelection = captured;
    std::string error;
    REQUIRE(service.insert(req, error) == InsertionOutcome::Failed);
    REQUIRE(host.pasteCalls == 0);
}

TEST_CASE("insertion: fallback anchor PID changed → abort", "[insertion]") {
    class ShiftedHost final : public MockHost {
    public:
        std::uint32_t foregroundProcessId() override { return 9999; }
    };
    ShiftedHost host;
    host.uiaHasPattern = false;
    TextSelection captured;  // clipboard-fallback mode: no insertionTargetId
    captured.text = "hello";
    captured.sourceProcessId = 4242;
    InsertionService service(host);
    InsertRequest req;
    req.translatedText = "你好";
    req.capturedSelection = captured;
    std::string error;
    REQUIRE(service.insert(req, error) == InsertionOutcome::Failed);
    REQUIRE(host.pasteCalls == 0);
}

TEST_CASE("insertion: empty translation rejected early", "[insertion]") {
    MockHost host;
    InsertionService service(host);
    InsertRequest req;
    req.translatedText = "";
    std::string error;
    REQUIRE(service.insert(req, error) == InsertionOutcome::Failed);
    REQUIRE(host.pasteCalls == 0);
}

// ===========================================================================
// Prompt config layering + URL helpers
// ===========================================================================
TEST_CASE("prompt: layering engine < compatible < scene < defaults",
          "[prompt]") {
    TranslationPromptConfig config;
    config.enginePrompts.push_back({TranslationEngineType::OpenAI, "engine-prompt"});
    config.compatibleEnginePrompts.push_back({"uuid-1", "instance-prompt"});
    config.scenePrompts.push_back({TranslationScene::Screenshot, "scene-prompt"});

    // Scene beats engine.
    REQUIRE(config.resolve(TranslationEngineType::OpenAI, TranslationScene::Screenshot) ==
            "scene-prompt");
    // Without a scene prompt, compatible beats engine for Custom.
    TranslationPromptConfig noScene;
    noScene.enginePrompts.push_back({TranslationEngineType::Custom, "engine-prompt"});
    noScene.compatibleEnginePrompts.push_back({"uuid-1", "instance-prompt"});
    REQUIRE(noScene.resolve(TranslationEngineType::Custom, TranslationScene::TextSelection,
                            "uuid-1") == "instance-prompt");
    // Engine prompt used when no scene/instance match.
    REQUIRE(config.resolve(TranslationEngineType::OpenAI, TranslationScene::TextSelection) ==
            "engine-prompt");
    // Insert scene default when nothing custom.
    REQUIRE(TranslationPromptConfig().resolve(TranslationEngineType::OpenAI,
                                              TranslationScene::TranslateAndInsert) ==
            TranslationPromptConfig::defaultInsertPrompt());
    // Global default otherwise.
    REQUIRE(TranslationPromptConfig().resolve(TranslationEngineType::OpenAI,
                                              TranslationScene::Screenshot) ==
            TranslationPromptConfig::defaultPrompt());
}

TEST_CASE("prompt: placeholder substitution", "[prompt]") {
    auto out = TranslationPromptConfig::substitute(
        "From {source_language} to {target_language}: {text}", "English", "Chinese", "Hi");
    REQUIRE(out == "From English to Chinese: Hi");
}

TEST_CASE("url: appendingApiSuffix dedupes version segments", "[url]") {
    REQUIRE(appendingApiSuffix("https://api.openai.com/v1", "chat/completions") ==
            "https://api.openai.com/v1/chat/completions");
    REQUIRE(appendingApiSuffix("https://api.openai.com/v1/", "chat/completions") ==
            "https://api.openai.com/v1/chat/completions");
    REQUIRE(appendingApiSuffix("https://api.openai.com/v1/chat/completions",
                               "chat/completions") ==
            "https://api.openai.com/v1/chat/completions");
    REQUIRE(appendingApiSuffix("http://localhost:11434", "v1/chat/completions") ==
            "http://localhost:11434/v1/chat/completions");
    REQUIRE(appendingApiSuffix("http://localhost:11434/v1", "v1/chat/completions") ==
            "http://localhost:11434/v1/chat/completions");
    // Case-insensitive overlap.
    REQUIRE(appendingApiSuffix("https://x.example/V1", "v1/models") == "https://x.example/V1/models");
}

TEST_CASE("url: localhost resolution and guards", "[url]") {
    REQUIRE(resolveLocalhost("http://localhost:8989/translate") ==
            "http://127.0.0.1:8989/translate");
    REQUIRE(resolveLocalhost("http://localhost/x") == "http://127.0.0.1/x");
    REQUIRE(resolveLocalhost("https://api.openai.com/v1") == "https://api.openai.com/v1");

    REQUIRE(isLocalhostHost("localhost"));
    REQUIRE(isLocalhostHost("127.0.0.1"));
    REQUIRE(isLocalhostHost("[::1]"));
    REQUIRE(isLocalhostHost("machine.local"));
    REQUIRE_FALSE(isLocalhostHost("api.openai.com"));

    REQUIRE(guardApiKeyTransport("http://api.openai.com/v1", true).has_value());
    REQUIRE_FALSE(guardApiKeyTransport("https://api.openai.com/v1", true).has_value());
    REQUIRE_FALSE(guardApiKeyTransport("http://localhost:1234/v1", true).has_value());
}

TEST_CASE("models: engine raw values match macOS strings", "[models]") {
    REQUIRE(std::string(translationEngineRaw(TranslationEngineType::MtranServer)) == "mtran");
    REQUIRE(translationEngineFromRaw("mtran").has_value());
    REQUIRE(translationEngineFromRaw("apple").has_value() == false);  // no apple
    REQUIRE(allTranslationEngines().size() == 9);
}
