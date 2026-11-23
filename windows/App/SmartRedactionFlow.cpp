// SmartRedactionFlow.cpp — FR-10 智能打码 worker flow (see header). Pure
// App plumbing: no windows of its own, no tray, no settings UI — the single
// HWND argument is where the result lands.
#include "SmartRedactionFlow.hpp"

#include "tf/ImageExporter.hpp"  // encodePngToMemory
#include "tf/ImageUploadProvider.hpp"  // base64Encode

#include <winrt/base.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>
#include <utility>

namespace tf::app {
namespace {

/// OpenAI-compatible chat.completions vision call — covers the OpenAI slot
/// plus any OpenAI-mode endpoint (custom baseURL, Ollama /v1). Claude/Gemini
/// body shapes land with the FR-17 NativeShell VLM transport; until then
/// those slots surface a Configuration error pointing at EnginesPage.
SmartRedactionEngine::Analyzer visionAnalyzer(const std::string& pngBase64,
                                              const core::VlmConfig& vlm,
                                              const SmartRedactContext& ctx) {
    return [pngBase64, vlm, &ctx](VlmProviderType provider, const std::string& prompt,
                                  const OcrImage&, std::string& outRaw)
        -> std::optional<ProviderError> {
        if (provider != VlmProviderType::OpenAI && provider != VlmProviderType::Ollama) {
            return ProviderError::configuration(
                "VLM provider " + std::string(vlmProviderRaw(provider)) +
                " needs an OpenAI-compatible endpoint (设置 → 引擎 → VLM)");
        }
        std::string apiKey;
        if (ctx.credentials) {
            for (const std::string& account :
                 {SmartRedactionEngine::credentialAccount(provider),
                  "vlm_api_key_" + std::string(vlmProviderRaw(provider))}) {
                if (const auto stored = ctx.credentials->load(account);
                    stored && !stored->apiKey.empty()) {
                    apiKey = stored->apiKey;
                    break;
                }
            }
        }
        const std::string url =
            resolveLocalhost(appendingApiSuffix(vlm.baseURL, "chat/completions"));
        if (auto guard = guardApiKeyTransport(url, !apiKey.empty())) return *guard;
        const nlohmann::json body = {
            {"model", vlm.modelName},
            {"messages",
             nlohmann::json::array({
                 {{"role", "system"},
                  {"content", SmartRedactionEngine::privacySystemPrompt()}},
                 {{"role", "user"},
                  {"content",
                   nlohmann::json::array({
                       {{"type", "text"}, {"text", prompt}},
                       {{"type", "image_url"},
                        {"image_url", {{"url", "data:image/png;base64," + pngBase64}}}},
                   })}},
             })},
            {"max_tokens", 4096}};
        HttpRequest request;
        request.method = "POST";
        request.url = url;
        request.headers["Content-Type"] = "application/json";
        if (!apiKey.empty()) request.headers["Authorization"] = "Bearer " + apiKey;
        request.body = body.dump();
        request.timeoutSeconds = 60;  // vision calls outrun the 10s default
        if (!ctx.transport) return ProviderError::configuration("no HTTP transport");
        const auto outcome = ctx.transport->send(request);
        if (!outcome.response) return outcome.error;
        const auto& response = *outcome.response;
        if (!response.ok()) {
            const std::string excerpt = response.body.substr(0, 200);  // readable
            if (response.status == 401 || response.status == 403) {
                return ProviderError::auth("API key rejected (HTTP " +
                                           std::to_string(response.status) + ")");
            }
            if (response.status == 429) {
                return ProviderError::quota("rate limited (HTTP 429)");
            }
            return ProviderError::content("HTTP " + std::to_string(response.status) +
                                          ": " + excerpt);
        }
        outRaw = response.body;
        return std::nullopt;
    };
}

void post(HWND notify, UINT doneMsg, SmartRedactResult result) {
    ::PostMessageW(notify, doneMsg, 0,
                   reinterpret_cast<LPARAM>(new SmartRedactResult(std::move(result))));
}

}  // namespace

void runSmartRedaction(HWND notify, UINT doneMsg, SmartRedactContext context,
                       std::shared_ptr<PixelBuffer> pixels) {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
    }
    SmartRedactResult result;
    if (!pixels || !pixels->valid()) {
        result.noImage = true;
        post(notify, doneMsg, std::move(result));
        return;
    }
    const auto provider = vlmProviderFromRaw(context.vlm.provider);

    // PixelBuffer → compact OcrImage rows (stride may carry alignment padding;
    // a flat byteSize() copy would shift every row once stride != width*4).
    OcrImage img;
    img.width = pixels->width;
    img.height = pixels->height;
    const std::size_t rowBytes = static_cast<std::size_t>(pixels->width) * 4;
    img.bgra.resize(rowBytes * static_cast<std::size_t>(pixels->height));
    for (std::int32_t row = 0; row < pixels->height; ++row) {
        std::memcpy(img.bgra.data() + row * rowBytes,
                    pixels->data.get() + row * static_cast<std::size_t>(pixels->stride),
                    rowBytes);
    }
    const std::vector<std::uint8_t> png = encodePngToMemory(*pixels);
    const std::string pngBase64 =
        png.empty() ? std::string()
                    : base64Encode(std::string_view(reinterpret_cast<const char*>(png.data()),
                                                    png.size()));

    SmartRedactionEngine engine(visionAnalyzer(pngBase64, context.vlm, context),
                                context.credentials);
    const auto outcome =
        engine.detectRegions(img, provider.value_or(VlmProviderType::OpenAI));
    if (!outcome.isOk()) {
        result.error = outcome.error;
        post(notify, doneMsg, std::move(result));
        return;
    }
    // Regions → mosaic annotations; parseRegions already produced physical
    // pixel rects. blockSize tracks region scale (model clamps 4..128).
    for (const auto& r : outcome.value) {
        const int blockSize =
            std::clamp(static_cast<int>(std::max(r.width, r.height)) / 6, 4, 128);
        result.batch.push_back(core::Annotation::mosaic(core::MosaicAnnotation(
            core::Uuid::randomUuid(), core::Rect{r.x, r.y, r.width, r.height}, blockSize)));
    }
    result.ok = true;
    post(notify, doneMsg, std::move(result));
}

}  // namespace tf::app
