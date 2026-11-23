// tf/SmartRedaction.hpp
// FR-10 智能打码 (macOS SmartPrivacyRedactionService + performSmartRedaction
// port). Detects sensitive regions with the configured VLM provider
// (phone numbers, passwords, amounts, faces, API keys, emails, IDs — same
// prompt semantics as TransFrame/Services/VLMProvider.swift VLMPromptPair
// .privacy) and overlays mosaic annotations on every detected region.
//
// Prompt alignment (macOS): one system + one user prompt, segments JSON
// schema (text = category, boundingBox normalized 0..1, confidence).
//
// Parser contract: VLM output goes through the same robustness pipeline as
// VlmOcrEngine::parseSegments (chat-completion content, ```json fences,
// plain array / {"results": [...]} / {"segments": [...]} shapes), then
// filtering + clamping specific to redaction:
//   - trunc/NaN/out-of-range boxes are dropped (clamped first when partial)
//   - confidence below the configurable threshold is dropped
//   - zero/negative-size boxes are dropped
// Output rects are in image PIXEL coordinates (top-left origin), mirroring
// the macOS service which returns CGRect pixel rects to the view model.
//
// Batch undo: callers convert regions to mosaics and use AnnotationDocument
// addAll() for one history entry (one Ctrl+Z clears the batch, PRD FR-10).
//
// Pure logic: no UI, no Win32; Catch2 drives every branch with stub
// analyzers (no real VLM key required).
#ifndef TF_SMART_REDACTION_HPP
#define TF_SMART_REDACTION_HPP

#include "tf/OcrEngines.hpp"  // OcrImage, VlmOcrEngine (Analyzer seam)
#include "tf/TranslationProvider.hpp"  // CredentialStorePtr

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tf {

/// One detected sensitive region (macOS CGRect in pixel space + category).
struct RedactionRegion {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    std::string category;    // VLM "text" field, e.g. "phone" / "face"
    double confidence = 0.0;
};

struct SmartRedactionOptions {
    /// Minimum confidence to keep a region. macOS parity: the Swift service
    /// filters `segment.confidence >= 0.5` (SmartPrivacyRedactionService);
    /// segments missing a confidence value are dropped by that filter.
    double minConfidence = 0.5;
    /// Minimum pixel size after conversion — mirrors the Swift
    /// `guard rect.width >= 4, rect.height >= 4` degenerate-box drop.
    double minSizePx = 4.0;
    /// Padding added around each region in pixels (Swift: none; keeps the
    /// mosaic tight by default, matching "make boxes tightly cover").
    double padPx = 0.0;
    /// Round pixel rects to integers (Swift: rect.integral). Default on for
    /// macOS parity; tests can disable to assert exact float mapping.
    bool integral = true;
};

class SmartRedactionEngine {
public:
    /// Same per-provider analyze primitive as VlmOcrEngine (NativeShell
    /// implements base64/URL upload per provider). Tests stub it.
    using Analyzer = VlmOcrEngine::Analyzer;

    SmartRedactionEngine(Analyzer analyzer, CredentialStorePtr credentials);

    /// Credential account naming: the EnginesPage settings UI schema
    /// ("TransFrame/vlm/<provider>"). isConfigured() also accepts the legacy
    /// "vlm_api_key_<provider>" account VlmOcrEngine::isAvailable reads, so
    /// keys written by either path enable smart redaction.
    static std::string credentialAccount(VlmProviderType provider);

    /// Whether any provider slot has credentials (Ollama keyless-local).
    /// Swift SmartPrivacyRedactionService.isConfigured parity.
    bool isConfigured(VlmProviderType provider) const;

    /// The privacy-detection prompt pair (macOS VLMPromptPair.privacy).
    static const std::string& privacySystemPrompt();
    static const std::string& privacyUserPrompt();

    /// Runs detection over `image` and returns pixel-space regions.
    /// Error categories mirror ProviderError classification: configuration
    /// (provider not configured / no credentials) vs content (invalid image)
    /// vs network (transport-level, surfaced by the analyzer). Unparseable
    /// payloads yield an empty region list.
    Outcome<std::vector<RedactionRegion>, ProviderError> detectRegions(
        const OcrImage& image, VlmProviderType provider,
        const SmartRedactionOptions& options = {}) const;

    /// Parses raw VLM JSON into normalized regions, then converts to pixels
    /// and applies filtering + clamping. Exposed for pure-function tests;
    /// detectRegions() funnels through here.
    ///
    /// Robustness contract (VLM output is untrusted text):
    ///   - trunc/NaN boxes: complete leading boxes still land (truncation
    ///     salvage); NaN/inf boxes are dropped
    ///   - out-of-range boxes clamp to [0,1]; wholly-outside boxes collapse
    ///     to zero size and drop
    ///   - confidence < minConfidence drops (missing confidence → 0 → drops
    ///     under the macOS 0.5 default)
    ///   - pixel rects below minSizePx drop; integral=true rounds outward
    ///     like CGRect.integral
    static std::vector<RedactionRegion> parseRegions(const std::string& rawJson,
                                                     std::int64_t imageWidth,
                                                     std::int64_t imageHeight,
                                                     const SmartRedactionOptions& options = {});

private:
    Analyzer analyzer_;
    CredentialStorePtr credentials_;
};

}  // namespace tf

#endif  // TF_SMART_REDACTION_HPP
