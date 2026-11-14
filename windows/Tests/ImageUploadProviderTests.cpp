// tests/ImageUploadProviderTests.cpp — FR-31 image-host upload protocol layer
// (PicGo / Aliyun OSS / Tencent COS / Qiniu Kodo). Pure Catch2 coverage:
// crypto primitives on public vectors, config codec, per-channel request
// shapes (signature recomputed independently in the test), response parsing
// and error classification, and the service orchestration via MockHttpTransport.
#include "catch_amalgamated.hpp"

#include "tf/ImageUploadProvider.hpp"
#include "TestSupport.hpp"

#include <nlohmann/json.hpp>

#include <cstring>
#include <optional>
#include <string>

using namespace tf;
using tf::tests::MockHttpTransport;

namespace {

// Single point of truth for every channel identifier: config helpers and
// assertions reference these constants, so both sides always agree.
const std::string kOssBucket = "tf-oss-bucket";
const std::string kOssEndpoint = "oss-cn-hangzhou.aliyuncs.com";
const std::string kOssKeyId = "oss-test-key-id";
const std::string kOssHost = kOssBucket + "." + kOssEndpoint;
const std::string kCosBucket = "tf-cos-bucket-1250000000";
const std::string kCosRegion = "ap-guangzhou";
const std::string kCosSecretId = "cos-test-secret-id";
const std::string kCosHost = kCosBucket + ".cos." + kCosRegion + ".myqcloud.com";
const std::string kKodoBucket = "tf-kodo";
const std::string kKodoDomain = "https://cdn.example.com";
const std::string kKodoAccessKey = "kodo-test-access-key";
const std::string kKodoHost = "upload.qiniup.com";

ImageUploadConfig ossConfig() {
    ImageUploadConfig c;
    c.activeHost = ImageUploadChannel::AliyunOSS;
    c.ossBucket = kOssBucket;
    c.ossEndpoint = kOssEndpoint;
    c.ossAccessKeyId = kOssKeyId;
    c.ossPathPrefix = "transframe/";
    return c;
}

ImageUploadConfig cosConfig() {
    ImageUploadConfig c;
    c.activeHost = ImageUploadChannel::TencentCOS;
    c.cosBucket = kCosBucket;
    c.cosRegion = kCosRegion;
    c.cosSecretId = kCosSecretId;
    c.cosPathPrefix = "transframe/";
    return c;
}

ImageUploadConfig kodoConfig() {
    ImageUploadConfig c;
    c.activeHost = ImageUploadChannel::QiniuKodo;
    c.kodoBucket = kKodoBucket;
    c.kodoDomain = kKodoDomain;
    c.kodoAccessKey = kKodoAccessKey;
    c.kodoPathPrefix = "transframe/";
    return c;
}

constexpr std::int64_t kNow = 1794096000;  // 2026-11-08 00:00:00 GMT (Sunday)
const std::string kFilename = uploadFilename(kNow, "ab12cd");
const std::string kPngBytes = "\x89PNG\r\n\x1a\nfake-bytes-for-tests";
const std::string kOssUrl =
    "https://" + kOssHost + "/transframe/" + kFilename;
const std::string kCosUrl =
    "https://" + kCosHost + "/transframe/" + kFilename;

ImageUploadSecrets secretsWith(const std::string& oss, const std::string& cos,
                               const std::string& kodo) {
    ImageUploadSecrets s;
    s.ossAccessKeySecret = oss;
    s.cosSecretKey = cos;
    s.kodoSecretKey = kodo;
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// Crypto / format primitives (public vectors)
// ---------------------------------------------------------------------------
TEST_CASE("sha1 and hmac-sha1 match reference vectors", "[uploads][crypto]") {
    CHECK(sha1Hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(sha1Hex("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    // RFC 2202 HMAC-SHA1 case 2 ("Jefe" key).
    CHECK(hmacSha1Hex("Jefe", "what do ya want for nothing?") ==
          "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79");
    // Common interop vector.
    CHECK(hmacSha1Hex("key", "The quick brown fox jumps over the lazy dog") ==
          "de7c9b85b8b78aa6bc8a7a36f70a90701c9db4d9");
}

TEST_CASE("base64 and urlsafe base64", "[uploads][crypto]") {
    CHECK(base64Encode("hello") == "aGVsbG8=");
    CHECK(base64Encode("") == "");
    CHECK(base64Encode(std::string_view("a\0b", 3)) == "YQBi");
    // 0xFB 0xFF 0xBF -> "+/+" in standard base64; urlsafe swaps to "-_-".
    const unsigned char bytes[] = {0xFB, 0xFF, 0xBF};
    const std::string raw(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    CHECK(base64Encode(raw) == "+/+/");  // (see below: exact padding checked)
    CHECK(urlSafeBase64(raw) == "-_-_");
}

TEST_CASE("rfc1123Gmt formats RFC 1123 GMT dates", "[uploads][crypto]") {
    CHECK(rfc1123Gmt(0) == "Thu, 01 Jan 1970 00:00:00 GMT");
    CHECK(rfc1123Gmt(kNow) == "Sun, 08 Nov 2026 00:00:00 GMT");
    CHECK(rfc1123Gmt(1709164800) == "Thu, 29 Feb 2024 00:00:00 GMT");  // leap day
    CHECK(rfc1123Gmt(kNow + 45) == "Sun, 08 Nov 2026 00:00:45 GMT");
    CHECK(uploadFilename(kNow, "ab12cd") == kFilename);
}

// ---------------------------------------------------------------------------
// Channel model
// ---------------------------------------------------------------------------
TEST_CASE("image upload channel raw names round-trip", "[uploads]") {
    for (auto c : {ImageUploadChannel::None, ImageUploadChannel::PicGo,
                   ImageUploadChannel::AliyunOSS, ImageUploadChannel::TencentCOS,
                   ImageUploadChannel::QiniuKodo}) {
        CAPTURE(imageUploadChannelRaw(c));
        REQUIRE(imageUploadChannelFromRaw(imageUploadChannelRaw(c)) == c);
    }
    CHECK(imageUploadChannelFromRaw("bogus") == std::nullopt);
    CHECK(std::strcmp(imageUploadChannelDisplayName(ImageUploadChannel::PicGo),
                      "PicGo (Local App)") == 0);
}

TEST_CASE("credential accounts follow TransFrame/uploads/<channel>", "[uploads]") {
    CHECK(uploadCredentialAccount(ImageUploadChannel::AliyunOSS) == "uploads/aliyunOSS");
    CHECK(uploadCredentialAccount(ImageUploadChannel::TencentCOS) == "uploads/tencentCOS");
    CHECK(uploadCredentialAccount(ImageUploadChannel::QiniuKodo) == "uploads/qiniuKodo");
    CHECK(uploadCredentialAccount(ImageUploadChannel::PicGo).empty());
    CHECK(uploadCredentialAccount(ImageUploadChannel::None).empty());
    CHECK(secretsWith("o", "c", "k").secretFor(ImageUploadChannel::AliyunOSS) == "o");
    CHECK(secretsWith("o", "c", "k").secretFor(ImageUploadChannel::TencentCOS) == "c");
    CHECK(secretsWith("o", "c", "k").secretFor(ImageUploadChannel::QiniuKodo) == "k");
    CHECK(secretsWith("o", "c", "k").secretFor(ImageUploadChannel::PicGo).empty());
}

// ---------------------------------------------------------------------------
// Settings codec
// ---------------------------------------------------------------------------
TEST_CASE("image upload config parse: defaults and full section", "[uploads][config]") {
    const ImageUploadConfig def = parseImageUploadConfig("");
    CHECK(def.activeHost == ImageUploadChannel::None);
    CHECK(def.picgoEndpoint == "http://127.0.0.1:36677/upload");
    CHECK(def.copyMarkdownFormat);
    CHECK(def.ossPathPrefix == "transframe/");
    CHECK(def.cosPathPrefix == "transframe/");
    CHECK(def.kodoPathPrefix == "transframe/");

    CHECK(parseImageUploadConfig("not json {").activeHost == ImageUploadChannel::None);
    CHECK(parseImageUploadConfig(R"({"other": 1})").activeHost ==
          ImageUploadChannel::None);

    const std::string json = R"({
      "TransFrame_ImageHostSettings": {
        "activeHost": "tencentCOS",
        "picgoEndpoint": "http://127.0.0.1:37777/upload",
        "copyMarkdownFormat": false,
        "ossBucket": "b1", "ossEndpoint": "oss-cn-hangzhou.aliyuncs.com",
        "ossAccessKeyId": "id1", "ossPathPrefix": "shots/",
        "cosBucket": "b2", "cosRegion": "ap-shanghai",
        "cosSecretId": "id2", "cosPathPrefix": "",
        "kodoBucket": "b3", "kodoDomain": "http://q.cdn.io",
        "kodoAccessKey": "id3", "kodoPathPrefix": "/deep/dir/",
        "ossAccessKeySecret": "legacyOss", "cosSecretKey": "legacyCos",
        "kodoSecretKey": "legacyKodo"
      }
    })";
    ImageUploadSecrets legacy;
    const ImageUploadConfig cfg = parseImageUploadConfig(json, &legacy);
    CHECK(cfg.activeHost == ImageUploadChannel::TencentCOS);
    CHECK(cfg.picgoEndpoint == "http://127.0.0.1:37777/upload");
    CHECK_FALSE(cfg.copyMarkdownFormat);
    CHECK(cfg.ossBucket == "b1");
    CHECK(cfg.ossPathPrefix == "shots/");
    CHECK(cfg.cosPathPrefix == "");
    CHECK(cfg.kodoPathPrefix == "/deep/dir/");
    CHECK(legacy.ossAccessKeySecret == "legacyOss");
    CHECK(legacy.cosSecretKey == "legacyCos");
    CHECK(legacy.kodoSecretKey == "legacyKodo");
}

TEST_CASE("image upload config serialize round-trips and sanitizes secrets",
          "[uploads][config]") {
    ImageUploadConfig cfg = cosConfig();
    cfg.copyMarkdownFormat = false;
    const std::string existing =
        R"({"saveLocation": "C:\\shots", "TransFrame_ImageHostSettings": {"activeHost": "none", "ossAccessKeySecret": "old"}})";
    const std::string merged = serializeImageUploadConfig(existing, cfg);

    const nlohmann::json root = nlohmann::json::parse(merged);
    REQUIRE(root.contains("saveLocation"));
    CHECK(root.at("saveLocation") == "C:\\shots");
    REQUIRE(root.contains("TransFrame_ImageHostSettings"));
    const auto& node = root.at("TransFrame_ImageHostSettings");
    CHECK(node.at("ossAccessKeySecret") == "");  // sanitized
    CHECK(node.at("cosSecretKey") == "");
    CHECK(node.at("kodoSecretKey") == "");

    const ImageUploadConfig back = parseImageUploadConfig(merged);
    CHECK(back == cfg);
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
TEST_CASE("validate upload config rejects incomplete channels", "[uploads][validate]") {
    const ImageUploadSecrets full = secretsWith("os", "cs", "ks");

    ImageUploadConfig none;
    CHECK(validateUploadConfig(none, full)->category == ErrorCategory::Configuration);

    ImageUploadConfig oss = ossConfig();
    oss.ossBucket.clear();
    CHECK(validateUploadConfig(oss, full)->category == ErrorCategory::Configuration);
    CHECK(validateUploadConfig(ossConfig(), secretsWith("", "cs", "ks"))->category ==
          ErrorCategory::Auth);  // complete config, missing secret

    ImageUploadConfig cos = cosConfig();
    cos.cosRegion.clear();
    CHECK(validateUploadConfig(cos, full)->category == ErrorCategory::Configuration);
    CHECK(validateUploadConfig(cosConfig(), secretsWith("os", "", "ks"))->category ==
          ErrorCategory::Auth);

    ImageUploadConfig kodo = kodoConfig();
    kodo.kodoDomain.clear();
    CHECK(validateUploadConfig(kodo, full)->category == ErrorCategory::Configuration);
    CHECK(validateUploadConfig(kodoConfig(), secretsWith("os", "cs", ""))->category ==
          ErrorCategory::Auth);

    ImageUploadConfig picgo;
    picgo.activeHost = ImageUploadChannel::PicGo;
    picgo.picgoEndpoint.clear();
    CHECK(validateUploadConfig(picgo, full)->category == ErrorCategory::Configuration);

    ImageUploadConfig picgoOk;
    picgoOk.activeHost = ImageUploadChannel::PicGo;
    CHECK_FALSE(validateUploadConfig(picgoOk, full).has_value());

    CHECK_FALSE(validateUploadConfig(ossConfig(), full).has_value());
    CHECK_FALSE(validateUploadConfig(cosConfig(), full).has_value());
    CHECK_FALSE(validateUploadConfig(kodoConfig(), full).has_value());
}

// ---------------------------------------------------------------------------
// PicGo
// ---------------------------------------------------------------------------
TEST_CASE("picgo request posts the temp file path list as JSON", "[uploads][picgo]") {
    const HttpRequest r =
        buildPicGoUploadRequest("http://127.0.0.1:36677/upload", "C:/tmp/x.png");
    CHECK(r.method == "POST");
    CHECK(r.url == "http://127.0.0.1:36677/upload");
    CHECK(r.headers.at("Content-Type") == "application/json");
    const nlohmann::json body = nlohmann::json::parse(r.body);
    REQUIRE(body.at("list").is_array());
    REQUIRE(body.at("list").size() == 1);
    CHECK(body.at("list").at(0) == "C:/tmp/x.png");
}

TEST_CASE("picgo response parsing: result array, empty payload, HTTP errors",
          "[uploads][picgo]") {
    HttpResponse ok;
    ok.status = 200;
    ok.body = R"({"result":["https://pic.local/a.png","https://pic.local/b.png"]})";
    const auto a = parsePicGoResponse(ok);
    REQUIRE(a.url.has_value());
    CHECK(*a.url == "https://pic.local/a.png");
    CHECK_FALSE(a.error.has_value());

    HttpResponse noResult;
    noResult.status = 200;
    noResult.body = R"({"success":true})";
    auto b = parsePicGoResponse(noResult);
    CHECK_FALSE(b.url.has_value());
    REQUIRE(b.error.has_value());
    CHECK(b.error->category == ErrorCategory::Content);

    HttpResponse emptyString;
    emptyString.status = 200;
    emptyString.body = R"({"result":[""]})";
    CHECK(parsePicGoResponse(emptyString).error.has_value());

    HttpResponse notFound;
    notFound.status = 404;
    notFound.body = "";
    auto c = parsePicGoResponse(notFound);
    REQUIRE(c.error.has_value());
    CHECK(c.error->category == ErrorCategory::Content);
    CHECK(c.error->message.find("PicGo HTTP 404") != std::string::npos);

    HttpResponse serverError;
    serverError.status = 500;
    auto d = parsePicGoResponse(serverError);
    REQUIRE(d.error.has_value());
    CHECK(d.error->category == ErrorCategory::Network);
}

// ---------------------------------------------------------------------------
// Aliyun OSS
// ---------------------------------------------------------------------------
TEST_CASE("oss put request shape and v1 signature", "[uploads][oss]") {
    const std::string secret = "oss-test-secret";
    const HttpRequest r =
        buildOssPutObjectRequest(ossConfig(), secret, kPngBytes, kFilename, kNow);

    CHECK(r.method == "PUT");
    CHECK(r.url == kOssUrl);
    CHECK(r.headers.at("Host") == kOssHost);
    CHECK(r.headers.at("Date") == "Sun, 08 Nov 2026 00:00:00 GMT");
    CHECK(r.headers.at("Content-Type") == "image/png");
    CHECK(r.body == kPngBytes);

    // Independent recomputation: stringToSign = "PUT\n\n<type>\n<date>\n/<bucket>/<key>".
    const std::string stringToSign = "PUT\n\nimage/png\nSun, 08 Nov 2026 00:00:00 GMT\n/" +
                                     kOssBucket + "/transframe/" + kFilename;
    const std::string expected = "OSS " + kOssKeyId + ":" + hmacSha1Base64(secret, stringToSign);
    CHECK(r.headers.at("Authorization") == expected);
}

TEST_CASE("oss prefix trimming and public url", "[uploads][oss]") {
    ImageUploadConfig c = ossConfig();
    c.ossPathPrefix = "/shots//";
    const HttpRequest r = buildOssPutObjectRequest(c, "s", kPngBytes, kFilename, kNow);
    CHECK(r.url == "https://" + kOssHost + "/shots/" + kFilename);

    c.ossPathPrefix = "/";
    CHECK(uploadObjectKey(c, ImageUploadChannel::AliyunOSS, kFilename) == kFilename);
    CHECK(uploadPublicUrl(c, ImageUploadChannel::AliyunOSS, kFilename) ==
          "https://" + kOssHost + "/" + kFilename);
}

// ---------------------------------------------------------------------------
// Tencent COS
// ---------------------------------------------------------------------------
TEST_CASE("cos put request shape and q-sign signature", "[uploads][cos]") {
    const std::string secretKey = "cos-test-secret-key";
    const HttpRequest r =
        buildCosPutObjectRequest(cosConfig(), secretKey, kPngBytes, kFilename, kNow);

    CHECK(r.method == "PUT");
    CHECK(r.url == kCosUrl);
    CHECK(r.headers.at("Host") == kCosHost);
    CHECK(r.headers.at("Content-Type") == "image/png");
    CHECK(r.body == kPngBytes);

    // Independent recomputation of the XML API signature chain.
    const std::string keyTime = std::to_string(kNow) + ";" + std::to_string(kNow + 3600);
    const std::string signKey = hmacSha1Hex(secretKey, keyTime);
    const std::string formatString =
        "put\n/transframe/" + kFilename + "\n\nhost=" + kCosHost + "\n";
    const std::string stringToSign =
        "sha1\n" + keyTime + "\n" + sha1Hex(formatString) + "\n";
    const std::string expectedSig = hmacSha1Hex(signKey, stringToSign);
    const std::string expectedAuth =
        "q-sign-algorithm=sha1&q-ak=" + kCosSecretId + "&q-sign-time=" + keyTime +
        "&q-key-time=" + keyTime +
        "&q-header-list=host&q-url-param-list=&q-signature=" + expectedSig;
    CHECK(r.headers.at("Authorization") == expectedAuth);
}

// ---------------------------------------------------------------------------
// Qiniu Kodo
// ---------------------------------------------------------------------------
TEST_CASE("kodo form request: token, multipart body, boundary", "[uploads][kodo]") {
    const std::string secretKey = "kodo-test-secret-key";
    const HttpRequest r =
        buildKodoFormUploadRequest(kodoConfig(), secretKey, kPngBytes, kFilename, kNow);

    CHECK(r.method == "POST");
    CHECK(r.url == "https://" + kKodoHost);
    const std::string contentType = r.headers.at("Content-Type");
    REQUIRE(contentType.rfind("multipart/form-data; boundary=", 0) == 0);
    const std::string boundary =
        contentType.substr(std::strlen("multipart/form-data; boundary="));

    // Boundary self-consistency: body opens and closes with it.
    const std::string dash = "--" + boundary;
    CHECK(r.body.rfind(dash, 0) == 0);
    CHECK(r.body.substr(r.body.size() - 4) == "--\r\n");

    // Extract the token field and re-derive the upload token.
    const std::size_t tokenPos = r.body.find("name=\"token\"\r\n\r\n");
    REQUIRE(tokenPos != std::string::npos);
    const std::size_t tokenStart = tokenPos + std::strlen("name=\"token\"\r\n\r\n");
    const std::size_t tokenEnd = r.body.find("\r\n", tokenStart);
    const std::string token = r.body.substr(tokenStart, tokenEnd - tokenStart);
    const std::size_t d1 = token.find(':');
    const std::size_t d2 = token.find(':', d1 + 1);
    REQUIRE(d1 != std::string::npos);
    REQUIRE(d2 != std::string::npos);
    CHECK(token.substr(0, d1) == kKodoAccessKey);
    const std::string encodedSign = token.substr(d1 + 1, d2 - d1 - 1);
    const std::string encodedPolicy = token.substr(d2 + 1);
    CHECK(encodedSign == urlSafeBase64(hmacSha1Base64(secretKey, encodedPolicy)));
    // Policy: {"deadline":now+3600,"scope":"bucket:key"} in urlsafe base64.
    const std::string policy =
        "{\"deadline\":" + std::to_string(kNow + 3600) + ",\"scope\":\"" + kKodoBucket +
        ":transframe/" + kFilename + "\"}";
    CHECK(encodedPolicy == urlSafeBase64(policy));

    // Key field and file bytes present.
    CHECK(r.body.find("name=\"key\"\r\n\r\ntransframe/" + kFilename) != std::string::npos);
    CHECK(r.body.find("filename=\"" + kFilename + "\"") != std::string::npos);
    CHECK(r.body.find(kPngBytes) != std::string::npos);
    // URL: domain + key.
    CHECK(uploadPublicUrl(kodoConfig(), ImageUploadChannel::QiniuKodo, kFilename) ==
          kKodoDomain + "/transframe/" + kFilename);
}

TEST_CASE("kodo domain gets https scheme when bare", "[uploads][kodo]") {
    ImageUploadConfig c = kodoConfig();
    c.kodoDomain = "http://q.cdn.io";
    CHECK(uploadPublicUrl(c, ImageUploadChannel::QiniuKodo, kFilename) ==
          "http://q.cdn.io/transframe/" + kFilename);
    c.kodoDomain = "q.cdn.io";
    CHECK(uploadPublicUrl(c, ImageUploadChannel::QiniuKodo, kFilename) ==
          "https://q.cdn.io/transframe/" + kFilename);
}

// ---------------------------------------------------------------------------
// Error classification
// ---------------------------------------------------------------------------
TEST_CASE("classify upload response maps status to error categories",
          "[uploads][errors]") {
    HttpResponse r;
    r.status = 403;
    r.body = R"(<?xml version="1.0"?><Error><Code>SignatureDoesNotMatch</Code><Message>bad sign</Message></Error>)";
    auto e = classifyUploadResponse(r, "Aliyun OSS");
    CHECK(e.category == ErrorCategory::Auth);
    CHECK(e.message.find("Aliyun OSS HTTP 403") != std::string::npos);
    CHECK(e.message.find("SignatureDoesNotMatch") != std::string::npos);

    r.status = 429;
    r.body.clear();
    CHECK(classifyUploadResponse(r, "X").category == ErrorCategory::Quota);

    r.status = 502;
    CHECK(classifyUploadResponse(r, "X").category == ErrorCategory::Network);

    r.status = 400;
    r.body = R"({"error":"file exists"})";
    const auto e400 = classifyUploadResponse(r, "Qiniu Kodo");
    CHECK(e400.category == ErrorCategory::Content);
    CHECK(e400.message.find("file exists") != std::string::npos);

    r.status = 401;
    r.body.clear();
    CHECK(cloudXmlErrorCode(r.body).empty());
    CHECK(classifyUploadResponse(r, "X").category == ErrorCategory::Auth);

    r.body = "<Error><Code>NodeNotFound</Code></Error>";
    CHECK(cloudXmlErrorCode(r.body) == "NodeNotFound");
}

// ---------------------------------------------------------------------------
// Service orchestration (mock transport)
// ---------------------------------------------------------------------------
TEST_CASE("service: picgo end-to-end with temp file lifecycle", "[uploads][service]") {
    auto mock = std::make_shared<MockHttpTransport>();
    mock->routes["http://127.0.0.1:36677/upload"] = {
        200, R"({"result":["https://pic.local/u/abc.png"]})", std::nullopt, 0};

    std::string writtenPath, removedPath, writtenBytes;
    ImageUploadService service(
        mock,
        [&](const std::string& filename, std::string_view bytes) {
            writtenBytes.assign(bytes.begin(), bytes.end());
            writtenPath = "C:/tmp/" + filename;
            return writtenPath;
        },
        [&](const std::string& p) { removedPath = p; });

    ImageUploadConfig cfg;
    cfg.activeHost = ImageUploadChannel::PicGo;
    const auto out = service.upload(cfg, {}, kPngBytes, kFilename, kNow);
    REQUIRE(out.result.has_value());
    CHECK(out.result->url == "https://pic.local/u/abc.png");
    CHECK(out.result->markdown == "![" + kFilename + "](https://pic.local/u/abc.png)");
    CHECK(out.result->provider == ImageUploadChannel::PicGo);
    CHECK(writtenBytes == kPngBytes);
    CHECK(removedPath == writtenPath);
    REQUIRE(mock->requests.size() == 1);
    CHECK(mock->requests[0].body.find(writtenPath) != std::string::npos);
    // Markdown vs plain URL per settings.
    ImageUploadConfig urlCfg = cfg;
    urlCfg.copyMarkdownFormat = false;
    const auto out2 = service.upload(urlCfg, {}, kPngBytes, kFilename, kNow);
    REQUIRE(out2.result.has_value());
    CHECK(ImageUploadService::clipboardText(urlCfg, *out2.result) == out2.result->url);
    CHECK(ImageUploadService::clipboardText(cfg, *out.result) == out.result->markdown);
}

TEST_CASE("service: oss success builds public url; failure classifies auth",
          "[uploads][service]") {
    auto mock = std::make_shared<MockHttpTransport>();
    mock->routes[kOssUrl] = {200, "", std::nullopt, 0};

    ImageUploadService service(mock, nullptr);
    const auto ok =
        service.upload(ossConfig(), secretsWith("os", "", ""), kPngBytes, kFilename, kNow);
    REQUIRE(ok.result.has_value());
    CHECK(ok.result->url == kOssUrl);
    CHECK(ok.error == std::nullopt);

    auto failMock = std::make_shared<MockHttpTransport>();
    failMock->routes[kOssUrl] = {
        403, "<Error><Code>SignatureDoesNotMatch</Code><Message>x</Message></Error>",
        std::nullopt, 0};
    ImageUploadService failing(failMock, nullptr);
    const auto bad = failing.upload(ossConfig(), secretsWith("wrong", "", ""),
                                    kPngBytes, kFilename, kNow);
    REQUIRE(bad.error.has_value());
    CHECK(bad.error->category == ErrorCategory::Auth);
    CHECK(bad.error->message.find("SignatureDoesNotMatch") != std::string::npos);
    CHECK(bad.result == std::nullopt);
}

TEST_CASE("service: none host and missing secret never hit the network",
          "[uploads][service]") {
    auto mock = std::make_shared<MockHttpTransport>();
    mock->routes["https://" + kKodoHost] = {
        200, R"({"hash":"frggREvUbhN5FnMlX5FtKc_I1B1","key":"transframe/x.png"})",
        std::nullopt, 0};
    ImageUploadService service(mock, nullptr);

    ImageUploadConfig none;
    const auto a =
        service.upload(none, secretsWith("o", "c", "k"), kPngBytes, kFilename, kNow);
    REQUIRE(a.error.has_value());
    CHECK(a.error->category == ErrorCategory::Configuration);

    const auto b = service.upload(ossConfig(), {}, kPngBytes, kFilename, kNow);
    REQUIRE(b.error.has_value());
    CHECK(b.error->category == ErrorCategory::Auth);

    const auto c = service.upload(kodoConfig(), secretsWith("", "", "kodo"), kPngBytes,
                                  kFilename, kNow + 1);
    REQUIRE_FALSE(c.error.has_value());  // cos secret untouched, kodo valid
    REQUIRE(c.result.has_value());
    CHECK(c.result->provider == ImageUploadChannel::QiniuKodo);
    CHECK(mock->requests.size() == 1);  // only the kodo request was sent
    CHECK(mock->requests[0].url == "https://" + kKodoHost);
}

TEST_CASE("service: transport failure propagates; picgo temp write fails cleanly",
          "[uploads][service]") {
    auto mock = std::make_shared<MockHttpTransport>();
    ImageUploadService service(mock, nullptr);

    ImageUploadConfig cfg;
    cfg.activeHost = ImageUploadChannel::PicGo;
    const auto out = service.upload(cfg, {}, kPngBytes, kFilename, kNow);
    REQUIRE(out.error.has_value());
    CHECK(out.error->category == ErrorCategory::Configuration);  // no temp provider

    ImageUploadService brokenTemp(
        mock, [](const std::string&, std::string_view) { return ""; }, nullptr);
    const auto w = brokenTemp.upload(cfg, {}, kPngBytes, kFilename, kNow);
    REQUIRE(w.error.has_value());
    CHECK(w.error->category == ErrorCategory::Network);
    CHECK(w.error->message.find("temp file") != std::string::npos);
    CHECK(mock->requests.empty());
}
