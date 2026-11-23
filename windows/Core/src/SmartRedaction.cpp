// SmartRedaction.cpp — FR-10 smart privacy redaction (see header).
// Accepts chat-completion content, ```json fences, and plain array /
// {"results": [...]} / {"segments": [...]} envelopes. Bounding boxes use
// the privacy prompt's normalized object schema. Filtering, clipping and
// integral pixel rounding follow the smart-redaction contract.
#include "tf/SmartRedaction.hpp"

#include "tf/Json.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tf {

namespace {

/// Extracts the JSON payload the same way parseSegments does:
/// chat-completion content → ```json fence → object body.
std::string extractPayload(const std::string& rawJson) {
    std::string content = rawJson;
    try {
        Json j = Json::parse(rawJson);
        if (j.is_object() && j.contains("choices") && j["choices"].is_array() &&
            !j["choices"].empty() && j["choices"][0].is_object() &&
            j["choices"][0].contains("message") && j["choices"][0]["message"].is_object() &&
            j["choices"][0]["message"].contains("content") &&
            j["choices"][0]["message"]["content"].is_string()) {
            content = j["choices"][0]["message"]["content"].get<std::string>();
        }
    } catch (const Json::parse_error&) {
        // Fall through: raw content.
    }
    const std::size_t open = content.find("```json");
    if (open != std::string::npos) {
        const std::size_t start = open + 7;
        const std::size_t close = content.find("```", start);
        if (close != std::string::npos) content = content.substr(start, close - start);
    }
    return content;
}

/// Reads the segment array from any supported envelope; nullopt on failure.
/// Truncation salvage: if the array was cut mid-item (incomplete trailing
/// object), the complete leading items still parse — mirrors the Swift
/// tolerate-truncation path used by LLMResponseTruncation handling.
std::optional<Json> extractSegmentsArray(const std::string& content) {
    try {
        Json parsed = Json::parse(content);
        if (parsed.is_array()) return parsed;
        if (parsed.is_object()) {
            if (parsed.contains("segments") && parsed["segments"].is_array())
                return parsed["segments"];
            if (parsed.contains("results") && parsed["results"].is_array())
                return parsed["results"];
        }
    } catch (const Json::parse_error&) {
    }
    return std::nullopt;
}

/// Truncation-salvage array extractor. Locates the first '[' after any
/// envelope prefix, then walks items one at a time, keeping every fully
/// closed object. Stops at the first malformed item (unbalanced / non-JSON
/// tail). Returns nullopt when nothing salvageable was found.
std::optional<Json> extractSegmentsArrayTruncationSalvage(const std::string& content) {
    std::optional<Json> strict = extractSegmentsArray(content);
    if (strict) return strict;

    // Find the opening '[' of the segments array (skip envelope keys).
    const std::size_t open = content.find('[');
    if (open == std::string::npos) return std::nullopt;

    Json out = Json::array();
    std::size_t i = open + 1;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    std::size_t itemStart = std::string::npos;
    while (i < content.size()) {
        const char c = content[i];
        if (depth == 0 && (c == '{' || c == '[')) {
            itemStart = i;
            depth = 1;
            ++i;
            continue;
        }
        if (depth > 0) {
            if (inString) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                ++i;
                continue;
            }
            if (c == '"') {
                inString = true;
                ++i;
                continue;
            }
            if (c == '{' || c == '[') ++depth;
            else if (c == '}' || c == ']') --depth;
            ++i;
            if (depth == 0) {
                try {
                    out.push_back(Json::parse(content.substr(itemStart, i - itemStart)));
                } catch (const Json::parse_error&) {
                    // Unparseable item: skip and keep scanning.
                }
                itemStart = std::string::npos;
            }
            continue;
        }
        if (c == ']') break;  // clean close of the array
        ++i;
    }
    if (out.empty()) return std::nullopt;
    return out;
}

/// Reads a finite optional double; NaN / inf are dropped, not clamped (the
/// VLM produced garbage, not an edge case). JSON numbers are IEEE doubles,
/// so a literal `NaN` inside a box would fail parse upstream — this guard
/// covers computed edge cases (e.g. huge negative normalized values that
/// multiply to -inf) plus any future non-strict loader.
bool finiteNumber(const Json& v) {
    if (!v.is_number()) return false;
    return std::isfinite(v.get<double>());
}

/// Clamps a normalized box into [0,1] (partial out-of-range boxes survive;
/// wholly-out boxes collapse to zero size and are dropped downstream).
void clamp01(double& v) { v = std::clamp(v, 0.0, 1.0); }

}  // namespace

const std::string& SmartRedactionEngine::privacySystemPrompt() {
    // macOS VLMPromptPair.privacy system (VLMProvider.swift).
    static const std::string p =
        "You are a privacy detection assistant. Locate regions containing sensitive personal "
        "information in the image: phone numbers, passwords, amounts/prices, faces, API "
        "keys/tokens, emails, and ID/card numbers.\n"
        "Return JSON: {\"segments\":[{\"text\":\"<category>\",\"boundingBox\":{\"x\":0.0,"
        "\"y\":0.0,\"width\":0.0,\"height\":0.0},\"confidence\":0.9}]}";
    return p;
}

const std::string& SmartRedactionEngine::privacyUserPrompt() {
    // macOS VLMPromptPair.privacy user (VLMProvider.swift).
    static const std::string p =
        "Detect all sensitive privacy regions in this image (phone numbers, passwords, amounts, "
        "faces, API keys/tokens, emails, ID numbers).\n"
        "Return each region in JSON format:\n"
        "{\"segments\":[{\"text\":\"<category>\",\"boundingBox\":{\"x\":0.0,\"y\":0.0,"
        "\"width\":0.0,\"height\":0.0},\"confidence\":0.9}]}\n"
        "\n"
        "- text: the category of the sensitive content (e.g. \"phone\", \"face\", \"email\")\n"
        "- x,y: top-left corner (0.0-1.0 normalized to image size)\n"
        "- width,height: box dimensions (0.0-1.0), make boxes tightly cover the content\n"
        "- confidence: 0.0-1.0\n"
        "If nothing sensitive is found, return {\"segments\":[]}.";
    return p;
}

SmartRedactionEngine::SmartRedactionEngine(Analyzer analyzer, CredentialStorePtr credentials)
    : analyzer_(std::move(analyzer)), credentials_(std::move(credentials)) {}

std::string SmartRedactionEngine::credentialAccount(VlmProviderType provider) {
    // EnginesPage (the settings UI users actually configure) writes
    // "TransFrame/vlm/<provider>"; VlmOcrEngine::isAvailable reads the legacy
    // "vlm_api_key_<provider>". FR-10 uses the UI schema and isConfigured()
    // below accepts BOTH so either writer works.
    return "TransFrame/vlm/" + std::string(vlmProviderRaw(provider));
}

bool SmartRedactionEngine::isConfigured(VlmProviderType provider) const {
    // Swift SmartPrivacyRedactionService.isConfigured: Ollama is keyless-local;
    // cloud slots need a stored per-provider key (Swift KeychainService).
    if (provider == VlmProviderType::Ollama) return true;
    if (!credentials_) return false;
    return credentials_->has(credentialAccount(provider)) ||
           credentials_->has("vlm_api_key_" + std::string(vlmProviderRaw(provider)));
}

std::vector<RedactionRegion> SmartRedactionEngine::parseRegions(const std::string& rawJson,
                                                                std::int64_t imageWidth,
                                                                std::int64_t imageHeight,
                                                                const SmartRedactionOptions& options) {
    const std::string content = extractPayload(rawJson);
    const std::optional<Json> arr = extractSegmentsArrayTruncationSalvage(content);
    if (!arr) return {};

    const double w = static_cast<double>(imageWidth);
    const double h = static_cast<double>(imageHeight);
    if (w <= 0.0 || h <= 0.0) return {};

    std::vector<RedactionRegion> out;
    for (const auto& item : *arr) {
        if (!item.is_object() || !item.contains("boundingBox") ||
            !item["boundingBox"].is_object()) {
            continue;  // redaction schema requires the object bbox form
        }
        const Json& b = item["boundingBox"];
        auto readBox = [&b](const char* key) -> double {
            if (!b.contains(key)) return 0.0;
            const Json& v = b.at(key);
            if (!finiteNumber(v)) return std::numeric_limits<double>::quiet_NaN();
            return v.get<double>();
        };
        double x = readBox("x");
        double y = readBox("y");
        double width = readBox("width");
        double height = readBox("height");
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
            !std::isfinite(height)) {
            continue;  // NaN / inf box: drop
        }

        double confidence = 0.0;  // missing confidence → 0 → dropped by the
                                  // macOS >= 0.5 default filter
        if (item.contains("confidence") && item["confidence"].is_number()) {
            confidence = item["confidence"].get<double>();
            if (!std::isfinite(confidence)) continue;
        }
        if (confidence < options.minConfidence) continue;

        // Clamp partial out-of-range boxes into the image; a box that lands
        // wholly outside collapses to zero size and is dropped below.
        if (width <= 0.0 || height <= 0.0) continue;
        double right = x + width;
        double bottom = y + height;
        clamp01(right);
        clamp01(bottom);
        clamp01(x);
        clamp01(y);
        width = std::min(width, right - x);
        height = std::min(height, bottom - y);
        if (width <= 0.0 || height <= 0.0) continue;

        std::string category;
        if (item.contains("text") && item["text"].is_string()) {
            category = item["text"].get<std::string>();
        }

        RedactionRegion r;
        r.x = std::clamp(x * w - options.padPx, 0.0, w);
        r.y = std::clamp(y * h - options.padPx, 0.0, h);
        // Intersect both endpoints: padding clipped at the left/top edge
        // must not shift the opposite edge farther into the image.
        r.width = std::clamp(x * w + width * w + options.padPx, 0.0, w) - r.x;
        r.height = std::clamp(y * h + height * h + options.padPx, 0.0, h) - r.y;
        // macOS degenerate-box drop: sub-4px regions are discarded.
        if (r.width < options.minSizePx || r.height < options.minSizePx) continue;
        if (options.integral) {
            // CGRect.integral: round outward so the mosaic fully covers the
            // detected region.
            const double x0 = std::floor(r.x);
            const double y0 = std::floor(r.y);
            r.width = std::ceil(r.x + r.width) - x0;
            r.height = std::ceil(r.y + r.height) - y0;
            r.x = x0;
            r.y = y0;
        }
        r.category = std::move(category);
        r.confidence = confidence;
        out.push_back(std::move(r));
    }
    return out;
}

Outcome<std::vector<RedactionRegion>, ProviderError> SmartRedactionEngine::detectRegions(
    const OcrImage& image, VlmProviderType provider, const SmartRedactionOptions& options) const {
    if (image.width <= 0 || image.height <= 0 || image.bgra.empty()) {
        return Outcome<std::vector<RedactionRegion>, ProviderError>::makeError(
            ProviderError::content("Invalid image"));
    }
    if (!isConfigured(provider)) {
        // FR-10 UX contract: unconfigured VLM routes the user to the engine
        // settings page via the Configuration error class.
        return Outcome<std::vector<RedactionRegion>, ProviderError>::makeError(
            ProviderError::configuration("VLM provider not configured"));
    }

    // The existing analyzer seam accepts one prompt. The privacy user
    // template carries the categories, coordinate schema and empty result.
    std::string rawJson;
    if (auto err = analyzer_(provider, privacyUserPrompt(), image, rawJson)) {
        return Outcome<std::vector<RedactionRegion>, ProviderError>::makeError(std::move(*err));
    }
    return Outcome<std::vector<RedactionRegion>, ProviderError>::makeOk(
        parseRegions(rawJson, image.width, image.height, options));
}

}  // namespace tf
