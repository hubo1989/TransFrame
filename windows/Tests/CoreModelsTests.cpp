// CoreModelsTests.cpp — schema alignment tests: every model JSON round-trips
// to the exact wire shape macOS Swift Codable produces (shapes observed in
// report-A.md probes: CGRect -> [[x,y],[w,h]], UUID uppercase string, Date ->
// seconds since 2001-01-01, Data -> base64, enum-with-payload ->
// {"case":{"_0":...}}, enum-keyed dictionary -> alternating array).
#include <catch2/catch_amalgamated.hpp>

#include "Core/Annotation.h"
#include "Core/CaptureDocument.h"
#include "Core/EngineSelectionV2.h"
#include "Core/Json.h"
#include "Core/KeyboardShortcut.h"
#include "Core/SceneEngineBinding.h"
#include "Core/SettingsKeys.h"
#include "Core/TranslationEngineConfig.h"
#include "Core/TranslationHistory.h"
#include "Core/TranslationResultBundle.h"
#include "Core/Uuid.h"
#include "Core/VLMProviderType.h"

using namespace tf::core;

namespace {
Uuid fixedUuid() {
    Uuid u;
    REQUIRE(Uuid::parse("E621E1F8-C36C-495A-93FC-0C247A3E6E5F", u));
    return u;
}
}  // namespace

// --------------------------------------------------------------- geometry --
TEST_CASE("CGRect encodes as nested arrays", "[schema]") {
    const Rect r{0.1, 0.2, 0.3, 0.4};
    const Json j = r;
    REQUIRE(j.dump() == "[[0.1,0.2],[0.3,0.4]]");
    const Rect back = j.get<Rect>();
    REQUIRE(back == r);
}

TEST_CASE("negative-size rects keep CGRect minX/maxX semantics", "[schema]") {
    const Rect r{10, 10, -4, -2};  // standardized origin (6,8)
    REQUIRE(r.minX() == Catch::Approx(6.0));
    REQUIRE(r.minY() == Catch::Approx(8.0));
    REQUIRE(r.maxX() == Catch::Approx(10.0));
    REQUIRE(r.maxY() == Catch::Approx(10.0));
}

// ------------------------------------------------------------------- uuid --
TEST_CASE("UUID parses and prints uppercase", "[schema]") {
    const Uuid u = fixedUuid();
    REQUIRE(u.toString() == "E621E1F8-C36C-495A-93FC-0C247A3E6E5F");
    // JSON wire form is the uppercase string.
    const Json j = u;
    REQUIRE(j.is_string());
    REQUIRE(j.get<std::string>() == u.toString());
}

TEST_CASE("UUID parse rejects malformed input", "[schema]") {
    Uuid u;
    REQUIRE_FALSE(Uuid::parse("not-a-uuid", u));
    REQUIRE_FALSE(Uuid::parse("{E621E1F8-C36C-495A-93FC-0C247A3E6E5", u));  // 31 hex
    REQUIRE(Uuid::parse("{E621E1F8-C36C-495A-93FC-0C247A3E6E5F}", u));      // braces tolerated
}

TEST_CASE("random UUIDs are v4-shaped and unique", "[schema]") {
    const Uuid a = Uuid::randomUuid();
    const Uuid b = Uuid::randomUuid();
    REQUIRE(a != b);
    const auto& bytes = a.bytes();
    REQUIRE(((bytes[6] >> 4) & 0xF) == 4);   // version 4
    REQUIRE(((bytes[8] >> 6) & 0x3) == 2);   // variant 10
}

TEST_CASE("nil UUID string form", "[schema]") {
    Uuid u;  // default = all-zero
    REQUIRE(u.toString() == "00000000-0000-0000-0000-000000000000");
}

// ------------------------------------------------------------------- date --
TEST_CASE("Date encodes as seconds since 2001-01-01", "[schema]") {
    // Swift: Date(timeIntervalSinceReferenceDate: 0).timeIntervalSince1970 == 978307200.
    using namespace std::chrono;
    const TimePoint epoch{duration_cast<system_clock::duration>(duration<double>(978307200.0))};
    const Json j = epoch;
    REQUIRE(j.get<double>() == Catch::Approx(0.0));
    const TimePoint back = j.get<TimePoint>();
    REQUIRE(back == epoch);
}

// ------------------------------------------------------------------ base64 --
TEST_CASE("Base64 round-trips and matches Swift Data encoding", "[schema]") {
    const std::vector<std::uint8_t> data{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A};
    const std::string encoded = encodeBase64(data);
    REQUIRE(encoded == "iVBORw0K");  // observed Swift Data base64 for PNG header
    const auto decoded = decodeBase64(encoded);
    REQUIRE(decoded == data);
}

TEST_CASE("Base64 decode skips whitespace and padding", "[schema]") {
    const auto decoded = decodeBase64("iVBO\r\n Rw0K==");
    REQUIRE(decoded == std::vector<std::uint8_t>({0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A}));
}

// -------------------------------------------------------- EngineChainItem --
TEST_CASE("EngineChainItem kind encodes as Swift enum-with-payload", "[schema]") {
    EngineChainItem item = EngineChainItem::builtin(TranslationEngineType::OpenAI);
    item.id = fixedUuid();
    const Json j = item;
    REQUIRE(j.dump() ==
            R"({"id":"E621E1F8-C36C-495A-93FC-0C247A3E6E5F","kind":{"builtin":{"_0":"openai"}}})");
    const EngineChainItem back = j.get<EngineChainItem>();
    REQUIRE(back == item);
}

TEST_CASE("EngineChainItem compatible variant round-trips", "[schema]") {
    EngineChainItem item = EngineChainItem::compatible(fixedUuid());
    const Json j = item;
    REQUIRE(j.at("kind").at("compatible").at("_0").get<std::string>() ==
            "E621E1F8-C36C-495A-93FC-0C247A3E6E5F");
    const EngineChainItem back = j.get<EngineChainItem>();
    REQUIRE(back == item);
    REQUIRE(back.resolvedEngine() == TranslationEngineType::Custom);
}

TEST_CASE("EngineChainItem rejects unknown builtin engine", "[schema]") {
    const Json j = Json::parse(
        R"({"id":"E621E1F8-C36C-495A-93FC-0C247A3E6E5F","kind":{"builtin":{"_0":"apple"}}})");
    // apple does not exist on Windows; parsing must fail loudly so callers can
    // drop the entry (graceful degradation happens at the settings layer).
    REQUIRE_THROWS_AS(j.get<EngineChainItem>(), std::invalid_argument);
}

// --------------------------------------------------------- engine configs --
TEST_CASE("TranslationEngineConfig encodes Swift CodingKeys", "[schema]") {
    EngineOptions options;
    options.baseURL = "http://localhost:11434";
    options.modelName = "llava";
    options.timeout = 30.0;
    options.maxTokens = 4096;
    options.temperature = 0.2;
    TranslationEngineConfig config(TranslationEngineType::Ollama, true, options);
    const Json j = config;
    REQUIRE(j.at("id").get<std::string>() == "ollama");
    REQUIRE(j.at("isEnabled").get<bool>() == true);
    REQUIRE(j.at("options").at("baseURL").get<std::string>() == "http://localhost:11434");
    const TranslationEngineConfig back = j.get<TranslationEngineConfig>();
    REQUIRE(back == config);
}

TEST_CASE("TranslationEngineConfig optional members encode as null", "[schema]") {
    TranslationEngineConfig config(TranslationEngineType::DeepL, false);
    const Json j = config;
    REQUIRE(j.at("options").is_null());
    REQUIRE(j.at("customName").is_null());
    const TranslationEngineConfig back = j.get<TranslationEngineConfig>();
    REQUIRE(back == config);
}

// ------------------------------------------------------- SceneEngineBinding --
TEST_CASE("SceneEngineBinding round-trips with customPrompt", "[schema]") {
    SceneEngineBinding binding;
    binding.scene = TranslationScene::TextSelection;
    binding.primaryEngine = TranslationEngineType::OpenAI;
    binding.fallbackEngine = TranslationEngineType::MtranServer;
    binding.fallbackEnabled = false;
    binding.customPrompt = "translate strictly {text}";
    const Json j = binding;
    REQUIRE(j.at("scene").get<std::string>() == "text_selection");
    REQUIRE(j.at("primaryEngine").get<std::string>() == "openai");
    REQUIRE(j.at("fallbackEngine").get<std::string>() == "mtran");
    REQUIRE(j.at("fallbackEnabled").get<bool>() == false);
    REQUIRE(j.at("customPrompt").get<std::string>() == "translate strictly {text}");
    const SceneEngineBinding back = j.get<SceneEngineBinding>();
    REQUIRE(back == binding);
}

TEST_CASE("SceneEngineBinding null optionals decode to nullopt", "[schema]") {
    const Json j = Json::parse(
        R"({"scene":"screenshot","primaryEngine":"mtran","fallbackEngine":null,)"
        R"("fallbackEnabled":true,"customPrompt":null})");
    const SceneEngineBinding back = j.get<SceneEngineBinding>();
    REQUIRE_FALSE(back.fallbackEngine.has_value());
    REQUIRE_FALSE(back.customPrompt.has_value());
}

// --------------------------------------------------------------- shortcut --
TEST_CASE("KeyboardShortcut encodes keyCode/modifiers", "[schema]") {
    const KeyboardShortcut shortcut = KeyboardShortcut::selectionDefault();
    const Json j = shortcut;
    REQUIRE(j.at("keyCode").get<std::uint32_t>() == 0x15);
    REQUIRE(j.at("modifiers").get<std::uint32_t>() ==
            (CarbonModifiers::cmdKey | CarbonModifiers::shiftKey));
    const KeyboardShortcut back = j.get<KeyboardShortcut>();
    REQUIRE(back == shortcut);
    REQUIRE(shortcut.hasRequiredModifiers());
    KeyboardShortcut shiftOnly{0x15, CarbonModifiers::shiftKey};
    REQUIRE_FALSE(shiftOnly.hasRequiredModifiers());
}

// ---------------------------------------------------------------- history --
TEST_CASE("TranslationHistory round-trips with category and thumbnail", "[schema]") {
    TranslationHistory h;
    h.id = fixedUuid();
    h.sourceText = "hello";
    h.translatedText = "你好";
    h.sourceLanguage = "English";
    h.targetLanguage = "Chinese (Simplified)";
    h.thumbnailData = std::vector<std::uint8_t>{0xFF, 0xD8, 0xFF};
    h.category = HistoryCategory::Color;
    h.isFavorite = true;
    const Json j = h;
    REQUIRE(j.at("category").get<std::string>() == "color");
    REQUIRE(j.at("isFavorite").get<bool>() == true);
    const TranslationHistory back = j.get<TranslationHistory>();
    REQUIRE(back == h);
}

TEST_CASE("TranslationHistory defaults match Swift decodeIfPresent", "[schema]") {
    const Json j = Json::parse(
        R"({"id":"E621E1F8-C36C-495A-93FC-0C247A3E6E5F","sourceText":"a","translatedText":"b",)"
        R"("sourceLanguage":"x","targetLanguage":"y","timestamp":100.5})");
    const TranslationHistory h = j.get<TranslationHistory>();
    REQUIRE(h.category == HistoryCategory::Translation);
    REQUIRE(h.isFavorite == false);
    REQUIRE_FALSE(h.thumbnailData.has_value());
}

TEST_CASE("color history entry convention", "[history]") {
    const auto h = TranslationHistory::colorPicked("#FF8800");
    REQUIRE(h.sourceText == "Color Picked");
    REQUIRE(h.translatedText == "#FF8800");
    REQUIRE(h.sourceLanguage == "RGB Hex");
    REQUIRE(h.targetLanguage == "#FF8800");
    REQUIRE(h.category == HistoryCategory::Color);
    REQUIRE(h.matches("#ff88"));  // case-insensitive
    REQUIRE_FALSE(h.matches("nope"));
}

TEST_CASE("upload and screenshot history conventions", "[history]") {
    const auto up = TranslationHistory::uploaded("![img](x.png)", "https://host/x.png", "SM.MS");
    REQUIRE(up.category == HistoryCategory::Upload);
    REQUIRE(up.sourceLanguage == "SM.MS");
    REQUIRE(up.targetLanguage == "Public URL");

    const auto shot = TranslationHistory::screenshotSaved("full screen", "C:/shots/a.png");
    REQUIRE(shot.category == HistoryCategory::Screenshot);
}

// ---------------------------------------------------------- bundle/segments --
TEST_CASE("BilingualSegment and EngineResult round-trip", "[schema]") {
    TextSegment original;
    original.id = fixedUuid();
    original.text = "hello";
    original.boundingBox = Rect{0.1, 0.2, 0.3, 0.04};
    original.confidence = 0.9f;
    BilingualSegment segment = BilingualSegment::fromSegment(
        original, "你好", "English", "Chinese (Simplified)");
    const Json j = segment;
    REQUIRE(j.at("original").at("text").get<std::string>() == "hello");
    const BilingualSegment back = j.get<BilingualSegment>();
    REQUIRE(back == segment);
}

TEST_CASE("TranslationResultBundle primary result logic", "[bundle]") {
    TranslationResultBundle bundle;
    EngineResult failed = EngineResult::failed(
        TranslationEngineType::MtranServer, {TranslationErrorCategory::NetworkError, "down"});
    std::vector<BilingualSegment> segments;
    segments.push_back(BilingualSegment::fromSegment(TextSegment{}, "你好", std::nullopt, "zh"));
    EngineResult ok(TranslationEngineType::OpenAI, segments, 0.4);
    bundle.results = {failed, ok};
    bundle.primaryEngine = TranslationEngineType::MtranServer;

    REQUIRE(bundle.hasErrors());
    REQUIRE_FALSE(bundle.allFailed());
    REQUIRE(bundle.primaryResult().size() == 1);  // falls back to the successful engine
    REQUIRE(bundle.successfulEngines() ==
            std::vector<TranslationEngineType>{TranslationEngineType::OpenAI});
    REQUIRE(bundle.failedEngines() ==
            std::vector<TranslationEngineType>{TranslationEngineType::MtranServer});
}

// --------------------------------------------------------------- settings --
TEST_CASE("settings keys match macOS one-to-one", "[settings]") {
    REQUIRE(std::string(SettingsKeys::saveLocation) == "TransFrame.saveLocation");
    REQUIRE(std::string(SettingsKeys::translationHistory) == "TransFrame.translationHistory");
    REQUIRE(std::string(SettingsKeys::engineChainV2) == "TransFrame.engineChainV2");
    REQUIRE(std::string(SettingsKeys::compareEnginesV2) == "TransFrame.compareEnginesV2");
    REQUIRE(std::string(SettingsKeys::paddleOCRMode) == "TransFrame.paddleOCRMode");
    REQUIRE(SettingsKeys::vlmBaseURLFor("openai") == "TransFrame.vlmBaseURL.openai");
    REQUIRE(SettingsKeys::vlmModelNameFor("claude") == "TransFrame.vlmModelName.claude");
}

// ---------------------------------------------------------------- engines --
TEST_CASE("engine raw values match macOS strings", "[schema]") {
    REQUIRE(toRaw(TranslationEngineType::MtranServer) == "mtran");
    REQUIRE(toRaw(TranslationEngineType::Custom) == "custom");
    REQUIRE(fromRaw(TranslationEngineType{}, "apple") == TranslationEngineType::Invalid);
    REQUIRE(fromRaw(TranslationEngineType{}, "mtran") == TranslationEngineType::MtranServer);
    // No apple in the Windows inventory.
    bool hasApple = false;
    for (const auto e : TranslationEngineTypes::kAll) {
        hasApple = hasApple || toRaw(e) == "apple";
    }
    REQUIRE_FALSE(hasApple);
    REQUIRE(TranslationEngineTypes::kCount == 9);
}

TEST_CASE("ocr engine migration maps vision to windows", "[schema]") {
    REQUIRE(ocrEngineFromRaw("vision") == OCREngineType::Windows);
    REQUIRE(ocrEngineFromRaw("windows") == OCREngineType::Windows);
    REQUIRE(ocrEngineFromRaw("paddleocr") == OCREngineType::PaddleOCR);
    REQUIRE(ocrEngineFromRaw("bogus") == OCREngineType::Invalid);
}

TEST_CASE("export format raw values and estimates", "[schema]") {
    REQUIRE(toRaw(ExportFormat::Heic) == "heic");
    REQUIRE(exportFormatFromRaw("jpeg") == ExportFormat::Jpeg);
    REQUIRE(estimatedBytesPerPixel(ExportFormat::Png) == Catch::Approx(1.5));
    REQUIRE(supportsQuality(ExportFormat::Png) == false);
    REQUIRE(supportsQuality(ExportFormat::Jpeg) == true);
}

// --------------------------------------------------------------- VLM types --
TEST_CASE("VLM provider raw values and defaults", "[schema]") {
    REQUIRE(toRaw(VLMProviderType::PaddleOCR) == "paddleocr");
    REQUIRE(vlmProviderFromRaw("ollama") == VLMProviderType::Ollama);
    REQUIRE(vlmDefaultBaseURL(VLMProviderType::OpenAI) == "https://api.openai.com/v1");
    REQUIRE(vlmDefaultModelName(VLMProviderType::Gemini) == "gemini-2.0-flash");
    REQUIRE(vlmTypicallyRequiresAPIKey(VLMProviderType::Ollama) == false);
}

TEST_CASE("CompatibleConfig round-trips and default matches Swift", "[schema]") {
    const CompatibleConfig def = CompatibleConfig::defaultConfig();
    REQUIRE(def.id.toString() == "00000000-0000-0000-0000-000000000000");
    REQUIRE(def.displayName == "Custom");
    REQUIRE(def.baseURL == "http://localhost:8000/v1");
    REQUIRE(def.keychainId() == "custom:00000000-0000-0000-0000-000000000000");
    const Json j = def;
    const CompatibleConfig back = j.get<CompatibleConfig>();
    REQUIRE(back == def);
}

// ------------------------------------------------------------- annotations --
TEST_CASE("all 11 annotation payloads round-trip", "[annotation]") {
    const Uuid id = fixedUuid();
    int checked = 0;
    auto roundTrip = [&](const Annotation& a) {
        const Json j = a;
        const Annotation back = j.get<Annotation>();
        REQUIRE(back == a);
        REQUIRE(back.id() == id);
        ++checked;
    };

    RectangleAnnotation r;  r.id = id; r.rect = Rect{1, 2, 30, 20}; r.isFilled = true;
    r.fillColor = CodableColor::blueColor();
    roundTrip(Annotation::rectangle(r));

    FreehandAnnotation f;  f.id = id; f.points = {{0, 0}, {10, 5}, {20, 8}};
    roundTrip(Annotation::freehand(f));

    ArrowAnnotation a;  a.id = id; a.startPoint = {0, 0}; a.endPoint = {10, 10};
    a.controlPoint = Point{5, 5};
    roundTrip(Annotation::arrow(a));

    TextAnnotation t;  t.id = id; t.position = {4, 6}; t.content = "note"; t.wrapWidth = 120;
    roundTrip(Annotation::text(t));

    EllipseAnnotation e;  e.id = id; e.rect = Rect{0, 0, 40, 20};
    roundTrip(Annotation::ellipse(e));

    LineAnnotation l;  l.id = id; l.startPoint = {0, 0}; l.endPoint = {40, 30};
    roundTrip(Annotation::line(l));

    MosaicAnnotation m;  m.id = id; m.rect = Rect{0, 0, 50, 50}; m.blockSize = 10;
    roundTrip(Annotation::mosaic(m));

    HighlightAnnotation h;  h.id = id; h.rect = Rect{0, 0, 60, 15};
    roundTrip(Annotation::highlight(h));

    NumberLabelAnnotation n;  n.id = id; n.position = {10, 10}; n.number = 3;
    n.leadPoint = Point{40, 12};
    roundTrip(Annotation::numberLabel(n));

    PrivacyRedactionAnnotation p;  p.id = id; p.rect = Rect{5, 5, 80, 12};
    roundTrip(Annotation::privacyRedaction(p));

    CalloutBubbleAnnotation c;  c.id = id; c.rect = Rect{0, 0, 100, 40};
    c.targetPoint = Point{120, 60}; c.text = "see this";
    roundTrip(Annotation::calloutBubble(c));

    REQUIRE(checked == 11);
}

TEST_CASE("annotation bounds match Swift geometry", "[annotation]") {
    NumberLabelAnnotation n;  n.id = fixedUuid(); n.position = {100, 100}; n.size = 24;
    REQUIRE(n.circleRect().minX() == Catch::Approx(88.0));
    REQUIRE(n.circleRect().maxY() == Catch::Approx(112.0));

    ArrowAnnotation a;  a.id = fixedUuid(); a.startPoint = {0, 0}; a.endPoint = {10, 10};
    a.style.lineWidth = 2;
    const Rect b = a.bounds();
    REQUIRE(b.minX() == Catch::Approx(-6.0));  // padding = 2*3
    REQUIRE(b.maxX() == Catch::Approx(16.0));

    MosaicAnnotation m;
    m.id = fixedUuid();
    (void)m;
    // clamp
    REQUIRE(MosaicAnnotation::clampBlockSize(2) == 4);
    REQUIRE(MosaicAnnotation::clampBlockSize(999) == 128);
    REQUIRE(NumberLabelAnnotation::clampedSize(300) == Catch::Approx(256.0));
}

TEST_CASE("annotation type names cover all cases", "[annotation]") {
    RectangleAnnotation r;  r.id = fixedUuid();
    REQUIRE(annotationTypeName(Annotation::rectangle(r)) == "rectangle");
    CalloutBubbleAnnotation c;  c.id = fixedUuid();
    REQUIRE(annotationTypeName(Annotation::calloutBubble(c)) == "calloutBubble");
    PrivacyRedactionAnnotation p;  p.id = fixedUuid();
    REQUIRE(annotationTypeName(Annotation::privacyRedaction(p)) == "privacyRedaction");
}

// ---------------------------------------------------------- ScreenAnalysis --
TEST_CASE("ScreenAnalysisResult filter and recovery", "[analysis]") {
    TextSegment low;  low.id = fixedUuid(); low.text = "low"; low.confidence = 0.3f;
    TextSegment high;  high.id = fixedUuid(); high.text = "high"; high.confidence = 0.9f;
    ScreenAnalysisResult result{{high, low}, {}, {}, 100, 50};

    REQUIRE(result.fullText() == "high\nlow");
    const auto filtered = result.filtered(0.5f);
    REQUIRE(filtered.segments.size() == 1);
    REQUIRE(filtered.filteredOutSegments.size() == 1);

    const auto recovered = filtered.recoveringFilteredSegments();
    REQUIRE(recovered.segments.size() == 2);
    REQUIRE(recovered.filteredOutSegments.empty());

    const auto updated = result.updatingSegment(low.id, "edited");
    REQUIRE(updated.segments[1].text == "edited");
    REQUIRE(updated.segments[1].isUserEdited);
}

TEST_CASE("OCRResult fullText sorts by minY", "[ocr]") {
    OCRResult ocr;
    ocr.imageWidth = 100;
    ocr.imageHeight = 50;
    OCRText bottom;  bottom.id = fixedUuid(); bottom.text = "bottom";
    bottom.boundingBox = Rect{0, 0.8, 0.5, 0.02};
    OCRText top;  top.id = fixedUuid(); top.text = "top";
    top.boundingBox = Rect{0, 0.1, 0.5, 0.02};
    ocr.observations = {bottom, top};
    REQUIRE(ocr.fullText() == "top\nbottom");
    REQUIRE(ocr.filteredBy(1.1f).observations.empty());
    const auto analysis = ScreenAnalysisResult::fromOcr(ocr);
    REQUIRE(analysis.segments.size() == 2);
    REQUIRE(analysis.imageWidth == 100);
}
