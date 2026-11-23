// tf/ImageUploadProvider.hpp — FR-31 image-host upload protocol layer.
//
// Four channels, ported 1:1 from macOS TransFrame/Services/ImageUploadService.swift:
//   PicGo       local PicGo app: POST {endpoint} JSON {"list":["<temp png path>"]}
//               -> JSON {"result":["<url>",...]} (first entry wins)
//   Aliyun OSS  REST PutObject https://<bucket>.<endpoint>/<key>, header
//               signature v1 (HMAC-SHA1, see docs note below)
//   Tencent COS REST PutObject https://<bucket>.cos.<region>.myqcloud.com/<key>,
//               XML API q-sign-algorithm=sha1 multipart-style auth header
//   Qiniu Kodo  multipart/form-data POST https://upload.qiniup.com with an
//               upload token (scope=bucket:key, urlsafe-base64 HMAC-SHA1)
//
// Everything here is pure protocol logic (signature construction, request
// shape, response parsing, error classification) over the same IHttpTransport
// seam as the OCR/translation providers — IOcrEngine-style injection, so the
// Catch2 suite drives it with mocks and the app injects WinHttpTransport.
// The only filesystem touch is PicGo's temp file, itself injected as a
// TempFileProvider callback (tests use a fake; NativeShell uses std::filesystem).
//
// OSS signing version choice (v1, matching macOS): the Swift reference signs
// PutObject with the legacy header scheme ("OSS <id>:<b64 hmac-sha1>" over
// "PUT\n\n<type>\n<date>\n/<bucket>/<key>"). Windows keeps byte-identical
// signatures so both platforms share one set of test vectors and one
// diagnosis path. The V4 scheme (OSS4-HMAC-SHA256 canonical request) adds a
// canonical-request/signed-headers layer with no benefit for a single-shot
// PUT and no macOS parity; it remains a future upgrade if OSS retires v1.
//
// Settings live in settings.json under the same key the macOS app uses for
// its UserDefaults blob ("TransFrame_ImageHostSettings", camelCase Codable
// field names), sanitized: the three secrets never round-trip through JSON —
// they ride Credential Manager under "TransFrame/uploads/<channel>" (see
// uploadCredentialAccount), mirroring the Swift Keychain store.
#ifndef TF_IMAGE_UPLOAD_PROVIDER_HPP
#define TF_IMAGE_UPLOAD_PROVIDER_HPP

#include "tf/CoreModels.hpp"
#include "tf/HttpTransport.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tf {

// ---------------------------------------------------------------------------
// Channel model (Swift ImageHostType).
// ---------------------------------------------------------------------------

enum class ImageUploadChannel : std::uint8_t {
    None,        // "none"
    PicGo,       // "picgo"
    AliyunOSS,   // "aliyunOSS"
    TencentCOS,  // "tencentCOS"
    QiniuKodo,   // "qiniuKodo"
};

const char* imageUploadChannelRaw(ImageUploadChannel c);               // "picgo" ...
std::optional<ImageUploadChannel> imageUploadChannelFromRaw(const std::string& raw);
const char* imageUploadChannelDisplayName(ImageUploadChannel c);       // "PicGo (Local App)" ...

/// CredMan account under the shared "TransFrame/" prefix for the secret this
/// channel signs with ("" when the channel needs no secret: none/picgo).
std::string uploadCredentialAccount(ImageUploadChannel c);  // "uploads/aliyunOSS" ...

/// The three secrets, kept out of settings.json (Swift ImageHostSecretStore).
struct ImageUploadSecrets {
    std::string ossAccessKeySecret;
    std::string cosSecretKey;
    std::string kodoSecretKey;

    /// Secret the channel signs with; "" when the channel needs none.
    std::string secretFor(ImageUploadChannel c) const;
    bool operator==(const ImageUploadSecrets&) const = default;
};

// ---------------------------------------------------------------------------
// Settings model (Swift ImageHostSettings minus the secrets).
// ---------------------------------------------------------------------------

struct ImageUploadConfig {
    ImageUploadChannel activeHost = ImageUploadChannel::None;
    std::string picgoEndpoint = "http://127.0.0.1:36677/upload";
    bool copyMarkdownFormat = true;  // copy URL when false (mac default: true)

    // Aliyun OSS (ossEndpoint e.g. "oss-cn-hangzhou.aliyuncs.com")
    std::string ossBucket;
    std::string ossEndpoint;
    std::string ossAccessKeyId;    // non-secret id, stays in settings.json
    std::string ossPathPrefix = "transframe/";

    // Tencent COS (cosRegion e.g. "ap-guangzhou")
    std::string cosBucket;
    std::string cosRegion;
    std::string cosSecretId;       // non-secret id, stays in settings.json
    std::string cosPathPrefix = "transframe/";

    // Qiniu Kodo (kodoDomain e.g. "https://cdn.example.com")
    std::string kodoBucket;
    std::string kodoDomain;
    std::string kodoAccessKey;     // non-secret id, stays in settings.json
    std::string kodoPathPrefix = "transframe/";

    bool operator==(const ImageUploadConfig&) const = default;
};

/// Pure: parse the "TransFrame_ImageHostSettings" section of settings.json.
/// Missing file/section -> defaults; malformed JSON -> defaults; wrong types
/// -> defaults per field (same tolerance as parseEngineConfig). Secret fields
/// are accepted (legacy plaintext migration source) into *legacySecrets when
/// provided, never required.
ImageUploadConfig parseImageUploadConfig(const std::string& settingsJsonUtf8,
                                         ImageUploadSecrets* legacySecrets = nullptr);

/// Pure: merge the section back into settings.json text, preserving every
/// other key. Secret fields are always written as "" (sanitized, matching the
/// Swift save path which blanks them before persisting).
std::string serializeImageUploadConfig(const std::string& existingJsonUtf8,
                                       const ImageUploadConfig& config);

/// Configuration check before any network: every field the channel's request
/// needs (bucket/endpoint/ids/prefix/secret). Nullopt = ready to sign.
std::optional<ProviderError> validateUploadConfig(const ImageUploadConfig& config,
                                                  const ImageUploadSecrets& secrets);

// ---------------------------------------------------------------------------
// Portable crypto / format primitives (shared by tests as signature vectors).
// ---------------------------------------------------------------------------

std::string sha1Hex(std::string_view message);
std::string hmacSha1Hex(std::string_view key, std::string_view message);
std::string hmacSha1Base64(std::string_view key, std::string_view message);
std::string base64Encode(std::string_view bytes);
/// base64 with '+','/' replaced by '-','_' (Qiniu urlsafe; '=' padding kept).
std::string urlSafeBase64(std::string_view bytes);
/// "Tue, 08 Nov 2026 03:04:05 GMT" — the OSS Date header form (RFC 1123, GMT).
std::string rfc1123Gmt(std::int64_t epochSeconds);
/// "TF_<epoch>_<tag>.png" — the Swift upload filename shape.
std::string uploadFilename(std::int64_t epochSeconds, const std::string& tag);

// ---------------------------------------------------------------------------
// Request builders (pure; every input explicit so tests are deterministic).
// ---------------------------------------------------------------------------

/// Object key below the bucket: "<trimmed prefix>/<filename>"; slashes trimmed
/// off both ends of the prefix (Swift trimmingCharacters(in: "/")).
std::string uploadObjectKey(const ImageUploadConfig& config, ImageUploadChannel channel,
                            const std::string& filename);

/// Public URL of the uploaded object (PicGo: server-decided, returns "").
std::string uploadPublicUrl(const ImageUploadConfig& config, ImageUploadChannel channel,
                            const std::string& filename);

/// PicGo: JSON body {"list":["<tempPath>"]} posted to the configured endpoint.
HttpRequest buildPicGoUploadRequest(const std::string& endpoint,
                                    const std::string& tempFilePathUtf8);

/// OSS PutObject: PUT https://<bucket>.<endpoint>/<key>, Host/Date/Content-
/// Type/Authorization headers, body = image bytes. Signature v1 (header docs).
HttpRequest buildOssPutObjectRequest(const ImageUploadConfig& config,
                                     const std::string& accessKeySecret,
                                     std::string_view imageBytes,
                                     const std::string& filename,
                                     std::int64_t nowEpochSeconds);

/// COS PutObject: PUT https://<bucket>.cos.<region>.myqcloud.com/<key> with the
/// q-sign-algorithm=sha1 Authorization header (XML API signature).
HttpRequest buildCosPutObjectRequest(const ImageUploadConfig& config,
                                     const std::string& secretKey,
                                     std::string_view imageBytes,
                                     const std::string& filename,
                                     std::int64_t nowEpochSeconds);

/// Kodo form upload: multipart/form-data (token/key/file) to
/// https://upload.qiniup.com; token = ak:urlsafe(hmac(sk, policy)).
HttpRequest buildKodoFormUploadRequest(const ImageUploadConfig& config,
                                       const std::string& secretKey,
                                       std::string_view imageBytes,
                                       const std::string& filename,
                                       std::int64_t nowEpochSeconds);

// ---------------------------------------------------------------------------
// Response parsing / error classification (pure).
// ---------------------------------------------------------------------------

/// PicGo: 2xx + JSON {"result":[url,...]} -> first URL; 2xx without a usable
/// result -> Content error ("unexpected PicGo response payload"); non-2xx ->
/// "PicGo HTTP <n>" (Content for 4xx, Network for 5xx).
struct PicGoParseOutcome {
    std::optional<std::string> url;
    std::optional<ProviderError> error;
};
PicGoParseOutcome parsePicGoResponse(const HttpResponse& response);

/// OSS/COS XML error body "<Error><Code>x</Code><Message>y</Message></Error>":
/// first Code/Message pair, empty when absent (best-effort detail enrichment).
std::string cloudXmlErrorCode(const std::string& body);

/// Status -> category for cloud PUT uploads (OSS/COS/Kodo): 401/403 Auth,
/// 429 Quota, >=500 Network, else Content. Message carries provider + status
/// + XML/JSON error code when present.
ProviderError classifyUploadResponse(const HttpResponse& response, const char* provider);

// ---------------------------------------------------------------------------
// Upload service (orchestration over the injected transport).
// ---------------------------------------------------------------------------

struct ImageUploadResult {
    std::string url;         // public URL of the uploaded object
    std::string markdown;    // "![<filename>](<url>)"
    std::string filename;    // "TF_...png"
    ImageUploadChannel provider = ImageUploadChannel::None;
};

struct ImageUploadOutcome {
    std::optional<ImageUploadResult> result;
    std::optional<ProviderError> error;

    static ImageUploadOutcome ok(ImageUploadResult r) {
        return ImageUploadOutcome{std::move(r), std::nullopt};
    }
    static ImageUploadOutcome fail(ProviderError e) {
        return ImageUploadOutcome{std::nullopt, std::move(e)};
    }
};

/// PicGo needs the PNG on disk: write bytes to "<dir>/<filename>", return the
/// path (UTF-8). Empty string = write failure (upload aborts with a network
/// error naming PicGo). Called on the worker thread right before the request;
/// the matching remover runs right after the response (Swift's defer cleanup).
using TempFileProvider =
    std::function<std::string(const std::string& filename, std::string_view pngBytes)>;
using TempFileRemover = std::function<void(const std::string& tempFilePathUtf8)>;

class ImageUploadService {
public:
    /// transport: production WinHTTP (or a mock in tests). tempFiles/remover:
    /// only used by the PicGo channel; may be null when PicGo is never active.
    ImageUploadService(HttpTransportPtr transport, TempFileProvider tempFiles,
                       TempFileRemover tempRemover = nullptr);

    /// Full upload for the configured active host. Mirrors Swift upload():
    /// validate config/secret -> encode request -> send -> classify ->
    /// URL + markdown. Blocking; cancellation via the request's token.
    ImageUploadOutcome upload(const ImageUploadConfig& config,
                              const ImageUploadSecrets& secrets,
                              std::string_view pngBytes, const std::string& filename,
                              std::int64_t nowEpochSeconds,
                              const CancellationToken& cancelToken = {});

    /// Text that should hit the clipboard: markdown when
    /// config.copyMarkdownFormat, else the URL (Swift upload() copy step).
    static std::string clipboardText(const ImageUploadConfig& config,
                                     const ImageUploadResult& result);

private:
    HttpTransportPtr transport_;
    TempFileProvider tempFiles_;
    TempFileRemover tempRemover_;
};

}  // namespace tf

#endif  // TF_IMAGE_UPLOAD_PROVIDER_HPP
