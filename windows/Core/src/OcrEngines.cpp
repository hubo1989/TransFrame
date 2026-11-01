// OcrEngines.cpp — WindowsOcr mapping + PaddleOCR protocol + VLM parsing.
#include "tf/OcrEngines.hpp"
#include "tf/Json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace tf {

// ===========================================================================
// WindowsOcrEngine
// ===========================================================================
WindowsOcrEngine::WindowsOcrEngine(LanguageProvider languageProvider, Recognizer recognizer,
                                   float minimumConfidence)
    : languageProvider_(std::move(languageProvider)),
      recognizer_(std::move(recognizer)),
      minimumConfidence_(minimumConfidence) {}

bool WindowsOcrEngine::isAvailable() const {
    if (!languageProvider_) return false;
    return !languageProvider_().empty();
}

std::vector<WindowsOcrEngine::Language> WindowsOcrEngine::installedLanguages() const {
    return languageProvider_ ? languageProvider_() : std::vector<Language>{};
}

bool WindowsOcrEngine::hasLanguagePack(const std::string& bcp47Tag) const {
    const std::string want = normalizeLanguageTag(bcp47Tag);
    for (const auto& lang : installedLanguages()) {
        if (lang.tag == want) return true;
        // Base-code match: "zh-Hans" pack satisfies "zh".
        const std::size_t wd = want.find('-');
        const std::size_t ld = lang.tag.find('-');
        if (wd != std::string::npos && ld != std::string::npos &&
            want.substr(0, wd) == lang.tag.substr(0, ld)) {
            return true;
        }
        if (ld == std::string::npos && want == lang.tag) return true;
    }
    return false;
}

std::string WindowsOcrEngine::normalizeLanguageTag(const std::string& tag) {
    if (tag == "zh" || tag == "zh-Hans" || tag == "zh-CN" || tag == "zh_Hans") return "zh-Hans";
    if (tag == "zh-Hant" || tag == "zh-TW") return "zh-Hant";
    if (tag == "en" || tag == "en-US") return "en-US";
    if (tag == "ja" || tag == "ja-JP") return "ja";
    if (tag == "ko" || tag == "ko-KR") return "ko";
    if (tag.empty()) return "en-US";
    std::string out = tag;
    std::size_t dash = out.find('-');
    if (dash == std::string::npos) return out;  // fr/de/es already base codes
    return out;
}

std::vector<TextSegment> WindowsOcrEngine::mapLines(const std::vector<RawLine>& lines,
                                                    std::int64_t imageWidth,
                                                    std::int64_t imageHeight,
                                                    float minimumConfidence) {
    std::vector<TextSegment> segments;
    segments.reserve(lines.size());
    const double w = static_cast<double>(imageWidth);
    const double h = static_cast<double>(imageHeight);
    for (const auto& line : lines) {
        // Windows.Media.Ocr already reports top-left-origin pixel rects; the
        // mapping is a pure scale into the normalized schema (macOS parity:
        // Vision's bottom-left origin is converted on macOS, not here).
        if (line.text.empty()) continue;
        const float confidence = line.confidence <= 0.0f ? 1.0f : line.confidence;
        if (confidence < minimumConfidence) continue;
        NormRect box;
        box.x = w > 0 ? line.x / w : 0.0;
        box.y = h > 0 ? line.y / h : 0.0;
        box.width = w > 0 ? line.w / w : 0.0;
        box.height = h > 0 ? line.h / h : 0.0;
        segments.push_back(TextSegment(line.text, box, confidence));
    }
    return segments;
}

Outcome<OcrResult, ProviderError> WindowsOcrEngine::recognize(const OcrImage& image) {
    if (image.width <= 0 || image.height <= 0 || image.bgra.empty()) {
        return Outcome<OcrResult, ProviderError>::makeError(
            ProviderError::content("Invalid image"));
    }
    if (!isAvailable()) {
        return Outcome<OcrResult, ProviderError>::makeError(
            ProviderError::configuration("No OCR language packs installed"));
    }
    std::vector<RawLine> lines;
    std::vector<std::string> tags;
    for (const auto& lang : installedLanguages()) tags.push_back(lang.tag);
    if (auto err = recognizer_(image, tags, lines)) {
        return Outcome<OcrResult, ProviderError>::makeError(*err);
    }
    OcrResult result = OcrResult::empty(image.width, image.height);
    result.observations = mapLines(lines, image.width, image.height, minimumConfidence_);
    return Outcome<OcrResult, ProviderError>::makeOk(std::move(result));
}

// ===========================================================================
// PaddleOcrEngine
// ===========================================================================
PaddleOcrEngine::PaddleOcrEngine(Configuration config, ProcessRunner runner,
                                 AvailabilityChecker availability)
    : config_(std::move(config)), runner_(std::move(runner)),
      availability_(std::move(availability)) {}

bool PaddleOcrEngine::isAvailable() const {
    if (config_.executablePath.empty()) return false;
    return availability_ ? availability_(config_.executablePath) : false;
}

std::vector<std::string> PaddleOcrEngine::buildArguments(const std::string& imagePath) const {
    std::vector<std::string> args;
    if (config_.mode == Mode::Fast) {
        // Chinese present → mixed "ch" (zh+en); otherwise first language.
        std::string lang = "ch";
        bool hasChinese = false;
        for (const auto& l : config_.languages) {
            if (l == "ch") {
                hasChinese = true;
                break;
            }
        }
        if (!hasChinese && !config_.languages.empty()) lang = config_.languages.front();
        args = {"ocr", "-i", imagePath, "--lang", lang,
                "--use_angle_cls", config_.useDirectionClassify ? "true" : "false"};
    } else {
        args = {"doc_parser", "-i", imagePath, "--pipeline_version", "v1.5",
                "--device", config_.useGPU ? "gpu" : "cpu"};
    }
    return args;
}

Outcome<OcrResult, ProviderError> PaddleOcrEngine::recognize(const OcrImage& image) {
    if (image.width <= 0 || image.height <= 0 || image.bgra.empty()) {
        return Outcome<OcrResult, ProviderError>::makeError(
            ProviderError::content("Invalid image"));
    }
    if (!isAvailable()) {
        return Outcome<OcrResult, ProviderError>::makeError(
            ProviderError::configuration("PaddleOCR executable not installed"));
    }
    // The image is written to a temp PNG by NativeShell; this layer receives
    // its path via the runner seam contract (first argv slot after -i).
    // For the pure-logic build the caller passes the path through the
    // runner's captured context; here we synthesize the protocol shape.
    std::string stdoutText, stderrText;
    // The imagePath is embedded in the argv; the runner owns temp-file
    // handling in NativeShell. Pure tests exercise parseFastMode/
    // parsePreciseMode directly.
    if (auto err = runner_(config_.executablePath, buildArguments("<image>"),
                           config_.mode == Mode::Fast ? 60 : 900, stdoutText, stderrText)) {
        return Outcome<OcrResult, ProviderError>::makeError(*err);
    }
    const std::string json = extractResultJson(stdoutText, stderrText);
    if (json.empty()) {
        return Outcome<OcrResult, ProviderError>::makeError(
            ProviderError::content("PaddleOCR produced no JSON output"));
    }
    OcrResult result = OcrResult::empty(image.width, image.height);
    if (config_.mode == Mode::Fast) {
        result.observations = parseFastMode(json, image.width, image.height,
                                            config_.minimumConfidence);
    } else {
        result.observations = parsePreciseMode(json, image.width, image.height);
    }
    return Outcome<OcrResult, ProviderError>::makeOk(std::move(result));
}

std::string PaddleOcrEngine::extractResultJson(const std::string& stdoutText,
                                               const std::string& stderrText) {
    // stdout JSON wins; else pull {'res': ...} out of stderr (Swift rule).
    if (!stdoutText.empty()) {
        const std::size_t start = stdoutText.find("{\"res\"");
        const std::size_t startSingle = stdoutText.find("{'res'");
        std::size_t at = std::string::npos;
        bool single = false;
        if (start != std::string::npos && (startSingle == std::string::npos || start < startSingle)) {
            at = start;
        } else if (startSingle != std::string::npos) {
            at = startSingle;
            single = true;
        }
        if (at == std::string::npos) return stdoutText;
        std::string tail = stdoutText.substr(at);
        // Matching closing brace scan (Swift findMatchingBrace).
        int depth = 0;
        bool inStr = false;
        std::size_t end = std::string::npos;
        for (std::size_t i = 0; i < tail.size(); ++i) {
            const char c = tail[i];
            if (inStr) {
                if (c == '\\') {
                    ++i;
                } else if (c == (single ? '\'' : '"')) {
                    inStr = false;
                }
                continue;
            }
            if (c == (single ? '\'' : '"')) {
                inStr = true;
            } else if (c == '{') {
                ++depth;
            } else if (c == '}') {
                --depth;
                if (depth == 0) {
                    end = i;
                    break;
                }
            }
        }
        if (end != std::string::npos) tail = tail.substr(0, end + 1);
        return pythonDictToJson(tail);
    }
    const std::size_t at = stderrText.find("{'res'");
    if (at == std::string::npos) return "";
    std::string tail = stderrText.substr(at);
    int depth = 0;
    bool inStr = false;
    std::size_t end = std::string::npos;
    for (std::size_t i = 0; i < tail.size(); ++i) {
        const char c = tail[i];
        if (inStr) {
            if (c == '\\') ++i;
            else if (c == '\'') inStr = false;
            continue;
        }
        if (c == '\'') inStr = true;
        else if (c == '{') ++depth;
        else if (c == '}') {
            --depth;
            if (depth == 0) {
                end = i;
                break;
            }
        }
    }
    if (end != std::string::npos) tail = tail.substr(0, end + 1);
    // Strip ANSI escapes.
    std::string cleaned;
    for (std::size_t i = 0; i < tail.size(); ++i) {
        if (tail[i] == 0x1B && i + 1 < tail.size() && tail[i + 1] == '[') {
            std::size_t j = i + 2;
            while (j < tail.size() &&
                   !(tail[j] >= '@' && tail[j] <= '~')) {
                ++j;
            }
            i = j;
            continue;
        }
        cleaned += tail[i];
    }
    return pythonDictToJson(cleaned);
}

std::string PaddleOcrEngine::pythonDictToJson(const std::string& pythonLiteral) {
    // Pass 1: single-quoted strings → double-quoted, True/False/None →
    // true/false/null (outside strings), tuple parens → brackets.
    std::string out;
    out.reserve(pythonLiteral.size() + 16);
    bool inSingle = false, inDouble = false, escaped = false;
    for (std::size_t i = 0; i < pythonLiteral.size(); ++i) {
        const char c = pythonLiteral[i];
        if (inSingle) {
            if (escaped) {
                if (c == '\'') out += '\'';
                else if (c == '"') out += "\\\"";
                else {
                    out += '\\';
                    out += c;
                }
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '\'') {
                out += '"';
                inSingle = false;
            } else if (c == '"') {
                out += "\\\"";
            } else {
                out += c;
            }
            continue;
        }
        if (inDouble) {
            if (escaped) {
                out += c;
                escaped = false;
            } else if (c == '\\') {
                out += c;
                escaped = true;
            } else if (c == '"') {
                out += c;
                inDouble = false;
            } else {
                out += c;
            }
            continue;
        }
        if (c == '\'') {
            out += '"';
            inSingle = true;
        } else if (c == '"') {
            out += '"';
            inDouble = true;
        } else if (c == '(') {
            out += '[';
        } else if (c == ')') {
            out += ']';
        } else if (c == 'T' && pythonLiteral.compare(i, 4, "True") == 0) {
            out += "true";
            i += 3;
        } else if (c == 'F' && pythonLiteral.compare(i, 5, "False") == 0) {
            out += "false";
            i += 4;
        } else if (c == 'N' && pythonLiteral.compare(i, 4, "None") == 0) {
            out += "null";
            i += 3;
        } else {
            out += c;
        }
    }
    // Pass 2: numpy array(...) wrappers → bare brackets.
    std::string denump;
    static const std::string kArrayOpen = "array(";
    std::size_t pos = 0;
    while (pos < out.size()) {
        const std::size_t hit = out.find(kArrayOpen, pos);
        if (hit == std::string::npos) {
            denump += out.substr(pos);
            break;
        }
        denump += out.substr(pos, hit - pos);
        // Skip "array(" and find its matching close paren, dropping the tail
        // ", dtype=...)" part.
        std::size_t depth = 1;
        std::size_t i = hit + kArrayOpen.size();
        bool inStr = false;
        std::size_t contentEnd = std::string::npos;
        std::size_t closeParen = std::string::npos;
        for (; i < out.size(); ++i) {
            const char c = out[i];
            if (inStr) {
                if (c == '\\') ++i;
                else if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') inStr = true;
            else if (c == '(') ++depth;
            else if (c == ')') {
                --depth;
                if (depth == 0) {
                    closeParen = i;
                    break;
                }
            } else if (c == ',' && depth == 1) {
                // Potential ", dtype" start — record the last comma before close.
                contentEnd = i;
            }
        }
        if (closeParen == std::string::npos) {
            denump += out.substr(hit);
            break;
        }
        // If the segment after the last comma looks like dtype=, drop it.
        std::size_t stop = closeParen;
        if (contentEnd != std::string::npos) {
            std::string tail = out.substr(contentEnd + 1, closeParen - contentEnd - 1);
            bool allSpace = true;
            for (char c : tail) {
                if (!std::isspace(static_cast<unsigned char>(c))) {
                    allSpace = false;
                    break;
                }
            }
            if (!tail.empty() && !allSpace && tail.find("dtype") != std::string::npos) {
                stop = contentEnd;
            }
        }
        denump += out.substr(hit + kArrayOpen.size(), stop - hit - kArrayOpen.size());
        pos = closeParen + 1;
    }
    out = denump;

    // Pass 3: "8." → "8.0", "-5." → "-5.0" (Swift float fix).
    std::string fixed;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const char c = out[i];
        fixed += c;
        if (c == '.' && i > 0 && std::isdigit(static_cast<unsigned char>(out[i - 1])) &&
            i + 1 < out.size()) {
            const char next = out[i + 1];
            if (next == ',' || next == ']' || next == '}' || next == ' ' || next == ')') {
                // Look ahead: only append 0 when the next non-space is a delimiter.
                fixed += '0';
            }
        }
    }
    return fixed;
}

std::vector<TextSegment> PaddleOcrEngine::parseFastMode(const std::string& json,
                                                        std::int64_t imageWidth,
                                                        std::int64_t imageHeight,
                                                        float minimumConfidence) {
    std::vector<TextSegment> segments;
    Json root;
    try {
        root = Json::parse(json);
    } catch (const Json::parse_error&) {
        return segments;
    }
    const Json* res = nullptr;
    if (root.is_object() && root.contains("res") && root["res"].is_object()) {
        res = &root["res"];
    } else if (root.is_object()) {
        res = &root;  // tolerate already-unwrapped payloads
    } else {
        return segments;
    }
    if (!res->contains("rec_texts") || !(*res)["rec_texts"].is_array()) return segments;
    const auto& texts = (*res)["rec_texts"];
    const Json* boxes = nullptr;
    const Json* scores = nullptr;
    if (res->contains("rec_boxes") && (*res)["rec_boxes"].is_array()) boxes = &(*res)["rec_boxes"];
    if (res->contains("rec_scores") && (*res)["rec_scores"].is_array())
        scores = &(*res)["rec_scores"];

    const double w = static_cast<double>(imageWidth);
    const double h = static_cast<double>(imageHeight);
    for (std::size_t i = 0; i < texts.size(); ++i) {
        if (!texts[i].is_string()) continue;
        const std::string text = texts[i].get<std::string>();
        if (text.empty()) continue;
        float confidence = 0.9f;
        if (scores && i < scores->size() && (*scores)[i].is_number()) {
            confidence = static_cast<float>((*scores)[i].get<double>());
        }
        if (confidence < minimumConfidence) continue;
        NormRect box;
        bool haveBox = false;
        if (boxes && i < boxes->size() && (*boxes)[i].is_array() && (*boxes)[i].size() >= 4) {
            const double x1 = (*boxes)[i][0].get<double>();
            const double y1 = (*boxes)[i][1].get<double>();
            const double x2 = (*boxes)[i][2].get<double>();
            const double y2 = (*boxes)[i][3].get<double>();
            box.x = w > 0 ? x1 / w : 0;
            box.y = h > 0 ? y1 / h : 0;
            box.width = w > 0 ? (x2 - x1) / w : 0;
            box.height = h > 0 ? (y2 - y1) / h : 0;
            haveBox = true;
        }
        if (!haveBox) {
            // Fallback: stack vertically (Swift).
            box = NormRect{0.0, static_cast<double>(i) * 0.1, 1.0, 0.1};
        }
        segments.push_back(TextSegment(text, box, confidence));
    }
    return segments;
}

std::vector<TextSegment> PaddleOcrEngine::parsePreciseMode(const std::string& json,
                                                           std::int64_t imageWidth,
                                                           std::int64_t imageHeight) {
    std::vector<TextSegment> segments;
    Json root;
    try {
        root = Json::parse(json);
    } catch (const Json::parse_error&) {
        return segments;
    }
    const Json* res = nullptr;
    if (root.is_object() && root.contains("res") && root["res"].is_object()) {
        res = &root["res"];
    } else if (root.is_object()) {
        res = &root;
    } else {
        return segments;
    }
    if (!res->contains("parsing_res_list") || !(*res)["parsing_res_list"].is_array()) {
        return segments;
    }
    const double w = static_cast<double>(imageWidth);
    const double h = static_cast<double>(imageHeight);
    int index = 0;
    for (const auto& block : (*res)["parsing_res_list"]) {
        ++index;
        if (!block.is_object() || !block.contains("block_content") ||
            !block["block_content"].is_string()) {
            continue;
        }
        // Skip non-text blocks (chart/seal/image/table/figure — Swift).
        if (block.contains("block_label") && block["block_label"].is_string()) {
            std::string label = block["block_label"].get<std::string>();
            for (auto& c : label) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (label.find("chart") != std::string::npos ||
                label.find("seal") != std::string::npos ||
                label.find("image") != std::string::npos ||
                label.find("table") != std::string::npos ||
                label.find("figure") != std::string::npos) {
                continue;
            }
        }
        const std::string text = block["block_content"].get<std::string>();
        NormRect box;
        bool haveBox = false;
        if (block.contains("block_bbox") && block["block_bbox"].is_array() &&
            block["block_bbox"].size() >= 4) {
            const double x1 = block["block_bbox"][0].get<double>();
            const double y1 = block["block_bbox"][1].get<double>();
            const double x2 = block["block_bbox"][2].get<double>();
            const double y2 = block["block_bbox"][3].get<double>();
            box.x = w > 0 ? x1 / w : 0;
            box.y = h > 0 ? y1 / h : 0;
            box.width = w > 0 ? (x2 - x1) / w : 0;
            box.height = h > 0 ? (y2 - y1) / h : 0;
            haveBox = true;
        }
        if (!haveBox) box = NormRect{0.0, static_cast<double>(index) * 0.1, 1.0, 0.1};
        segments.push_back(TextSegment(text, box, 0.9f));  // doc_parser has no score
    }
    return segments;
}

PaddleOcrHealthCache::PaddleOcrHealthCache(double ttlSeconds) : ttl_(ttlSeconds) {}

std::optional<bool> PaddleOcrHealthCache::cached(const std::string& executable, double now) const {
    auto it = cache_.find(executable);
    if (it == cache_.end()) return std::nullopt;
    if (now - it->second.second >= ttl_) return std::nullopt;
    return it->second.first;
}

void PaddleOcrHealthCache::store(const std::string& executable, bool available, double now) {
    cache_[executable] = {available, now};
}

void PaddleOcrHealthCache::invalidateAll() { cache_.clear(); }

// ===========================================================================
// VlmOcrEngine
// ===========================================================================
VlmOcrEngine::VlmOcrEngine(Analyzer analyzer, CredentialStorePtr credentials)
    : analyzer_(std::move(analyzer)), credentials_(std::move(credentials)) {}

const std::string& VlmOcrEngine::extractionPrompt() {
    static const std::string p =
        "Please recognize all text in this image and return the results in JSON format with "
        "'text', 'confidence', and 'box' (x, y, width, height) for each text region. Coordinates "
        "are normalized to 0-1 relative to the image.";
    return p;
}

std::vector<VlmProviderType> VlmOcrEngine::supportedProviders() {
    return {VlmProviderType::OpenAI, VlmProviderType::Claude, VlmProviderType::Gemini,
            VlmProviderType::Ollama, VlmProviderType::PaddleOCR};
}

bool VlmOcrEngine::isAvailable() const {
    // Ollama slot is keyless-local → available; others need stored keys
    // (account "vlm_api_key_<provider>", Swift KeychainService).
    if (activeProvider_ == VlmProviderType::Ollama) return true;
    return credentials_ && credentials_->has("vlm_api_key_" + std::string(vlmProviderRaw(activeProvider_)));
}

Outcome<OcrResult, ProviderError> VlmOcrEngine::recognize(const OcrImage& image) {
    if (image.width <= 0 || image.height <= 0 || image.bgra.empty()) {
        return Outcome<OcrResult, ProviderError>::makeError(
            ProviderError::content("Invalid image"));
    }
    if (!isAvailable()) {
        return Outcome<OcrResult, ProviderError>::makeError(
            ProviderError::configuration("VLM provider not configured"));
    }
    std::string rawJson;
    if (auto err = analyzer_(activeProvider_, extractionPrompt(), image, rawJson)) {
        return Outcome<OcrResult, ProviderError>::makeError(*err);
    }
    OcrResult result = OcrResult::empty(image.width, image.height);
    result.observations = parseSegments(rawJson, image.width, image.height);
    if (result.observations.empty()) {
        return Outcome<OcrResult, ProviderError>::makeError(
            ProviderError::content("VLM returned no text segments"));
    }
    return Outcome<OcrResult, ProviderError>::makeOk(std::move(result));
}

std::vector<TextSegment> VlmOcrEngine::parseSegments(const std::string& rawJson,
                                                     std::int64_t imageWidth,
                                                     std::int64_t imageHeight) {
    (void)imageWidth;
    (void)imageHeight;
    // Pull the JSON array out of chat-completion content / markdown fences.
    std::string content = rawJson;
    {
        // Chat completion shape {"choices":[{"message":{"content": "..."}}]}.
        try {
            Json j = Json::parse(rawJson);
            if (j.is_object() && j.contains("choices") && j["choices"].is_array() &&
                !j["choices"].empty() && j["choices"][0].is_object() &&
                j["choices"][0].contains("message") &&
                j["choices"][0]["message"].is_object() &&
                j["choices"][0]["message"].contains("content") &&
                j["choices"][0]["message"]["content"].is_string()) {
                content = j["choices"][0]["message"]["content"].get<std::string>();
            }
        } catch (const Json::parse_error&) {
            // Fall through: raw content.
        }
    }
    {
        // ```json fenced block.
        const std::size_t open = content.find("```json");
        if (open != std::string::npos) {
            const std::size_t start = open + 7;
            const std::size_t close = content.find("```", start);
            if (close != std::string::npos) {
                content = content.substr(start, close - start);
            }
        }
    }
    Json arr;
    try {
        Json parsed = Json::parse(content);
        if (parsed.is_array()) {
            arr = parsed;
        } else if (parsed.is_object() && parsed.contains("results") &&
                   parsed["results"].is_array()) {
            arr = parsed["results"];
        } else {
            return {};
        }
    } catch (const Json::parse_error&) {
        return {};
    }

    std::vector<TextSegment> segments;
    for (const auto& item : arr) {
        if (!item.is_object() || !item.contains("text") || !item["text"].is_string()) continue;
        const std::string text = item["text"].get<std::string>();
        if (text.empty()) continue;
        float confidence = 1.0f;
        if (item.contains("confidence") && item["confidence"].is_number()) {
            confidence = static_cast<float>(item["confidence"].get<double>());
        }
        NormRect box;
        if (item.contains("box") && item["box"].is_object()) {
            const Json& b = item["box"];
            auto read = [&b](const char* k) -> std::optional<double> {
                if (b.contains(k) && b[k].is_number()) return b[k].get<double>();
                return std::nullopt;
            };
            box.x = read("x").value_or(0);
            box.y = read("y").value_or(0);
            box.width = read("width").value_or(0);
            box.height = read("height").value_or(0);
        } else if (item.contains("box") && item["box"].is_array() &&
                   item["box"].size() >= 4) {
            // Pixel [x1,y1,x2,y2] tolerance: normalize when > 1.
            const double x1 = item["box"][0].get<double>();
            const double y1 = item["box"][1].get<double>();
            const double x2 = item["box"][2].get<double>();
            const double y2 = item["box"][3].get<double>();
            const double w = static_cast<double>(imageWidth);
            const double h = static_cast<double>(imageHeight);
            box.x = w > 0 ? x1 / w : x1;
            box.y = h > 0 ? y1 / h : y1;
            box.width = w > 0 ? (x2 - x1) / w : x2 - x1;
            box.height = h > 0 ? (y2 - y1) / h : y2 - y1;
        }
        segments.push_back(TextSegment(text, box, confidence));
    }
    return segments;
}

}  // namespace tf
