// windows/Tests/manual/smoke_uploads_picker.cpp — image-host upload + screen
// color picker + color history smoke (PRD stage 5a, FR-31/FR-32/FR-41).
// Real-run verification, no fakes except the local PicGo stand-in server:
//
//   A1 PicGo request shape: POST endpoint, JSON {"list":["<temp>"]}
//   A2 OSS request shape: PUT bucket URL, "OSS <id>:<sig>" Authorization,
//      Content-Type image/png, Date == rfc1123Gmt(now)
//   A3 COS request shape: PUT bucket.cos.<region> URL, q-sign-algorithm=sha1
//   A4 Kodo request shape: POST upload.qiniup.com multipart token/key/file
//   B  full PicGo upload loop against a local python HTTP stand-in returning
//      fixed JSON (real WinHttpTransport, real temp file, cleanup verified)
//   C  picker: real topmost lens window, simulated mouse move/click samples a
//      real screen pixel (cross-checked against a direct GetPixel probe),
//      Esc cancels
//   D  color history window over a throwaway SQLite store: grid rows, HEX
//      search filter, favorite toggle, delete, RGB copy-format toggle,
//      clear-all leaves non-color entries intact
//
// Build: Tests\manual\build_smoke_uploads_picker.bat (cl /MD against the
// sln-built static libs). Run from windows/ — it cleans its temp dir.

#ifdef _WIN32

#include "tf/ColorHistoryWindow.hpp"
#include "tf/ColorPicker.hpp"
#include "tf/HistoryStore.hpp"
#include "tf/ImageUploadProvider.hpp"
#include "tf/WinHttpTransport.h"

#include <windows.h>
#include <objbase.h>  // CoInitializeEx (D2D/DComp picker stack)

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace tf;
using namespace tf::core;

namespace {

int g_failures = 0;

void check(bool ok, const char* step, const std::string& detail = "") {
    if (ok) {
        std::printf("[PASS] %s %s\n", step, detail.c_str());
    } else {
        ++g_failures;
        std::printf("[FAIL] %s %s\n", step, detail.c_str());
    }
}

std::string narrow(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                        static_cast<int>(wide.size()), nullptr, 0,
                                        nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                          out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                        static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                          out.data(), n);
    return out;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string headerOf(const HttpRequest& r, const std::string& name) {
    const auto it = r.headers.find(name);
    return it == r.headers.end() ? "" : it->second;
}

// ---------------------------------------------------------------------------
// A. Four-channel pure request shapes (full vectors live in Catch2).
// ---------------------------------------------------------------------------

ImageUploadConfig smokeCloudConfig() {
    ImageUploadConfig c;
    c.activeHost = ImageUploadChannel::AliyunOSS;
    c.ossBucket = "tf-smoke-oss";
    c.ossEndpoint = "oss-cn-hangzhou.aliyuncs.com";
    c.ossAccessKeyId = "tf-smoke-key-id";
    c.cosBucket = "tf-smoke-cos";
    c.cosRegion = "ap-guangzhou";
    c.cosSecretId = "tf-smoke-secret-id";
    c.kodoBucket = "tf-smoke-kodo";
    c.kodoDomain = "https://cdn.tf-smoke.example";
    c.kodoAccessKey = "tf-smoke-kodo-ak";
    return c;
}

void runChannelShapes() {
    const std::int64_t now = 1794096000;  // 2026-11-08T00:00Z (test vector time)
    const std::string file = uploadFilename(now, "ab12cd");
    const std::string png = "\x89PNG\r\n\x1a\nsmoke-payload";
    const ImageUploadConfig cfg = smokeCloudConfig();
    const ImageUploadSecrets secrets{"tf-smoke-oss-sk", "tf-smoke-cos-sk",
                                     "tf-smoke-kodo-sk"};

    // A1 PicGo: JSON list body to the endpoint.
    const HttpRequest picgo = buildPicGoUploadRequest("http://127.0.0.1:39171/upload",
                                                      "C:/tmp/tf_smoke.png");
    check(picgo.method == "POST" && picgo.url == "http://127.0.0.1:39171/upload",
          "A1 picgo method+url", picgo.method + " " + picgo.url);
    check(contains(headerOf(picgo, "Content-Type"), "application/json") &&
              contains(picgo.body, "\"list\"") && contains(picgo.body, "tf_smoke.png"),
          "A1 picgo JSON list body");

    // A2 OSS: PUT, bucket host, v1 header signature, dated headers.
    const HttpRequest oss = buildOssPutObjectRequest(cfg, secrets.ossAccessKeySecret,
                                                     png, file, now);
    const std::string key = uploadObjectKey(cfg, ImageUploadChannel::AliyunOSS, file);
    check(oss.method == "PUT" &&
              oss.url == "https://" + cfg.ossBucket + "." + cfg.ossEndpoint + "/" + key,
          "A2 oss method+url", oss.url);
    const std::string auth = headerOf(oss, "Authorization");
    check(auth.rfind("OSS " + cfg.ossAccessKeyId + ":", 0) == 0, "A2 oss Authorization prefix",
          auth);
    check(headerOf(oss, "Content-Type") == "image/png" &&
              headerOf(oss, "Date") == rfc1123Gmt(now), "A2 oss type+date headers");

    // A3 COS: PUT on the bucket host, XML API q-sign chain.
    const HttpRequest cos = buildCosPutObjectRequest(cfg, secrets.cosSecretKey, png,
                                                     file, now);
    check(cos.method == "PUT" &&
              cos.url == "https://" + cfg.cosBucket + ".cos." + cfg.cosRegion + ".myqcloud.com/" +
                             uploadObjectKey(cfg, ImageUploadChannel::TencentCOS, file),
          "A3 cos method+url", cos.url);
    check(contains(headerOf(cos, "Authorization"), "q-sign-algorithm=sha1"),
          "A3 cos q-sign header");

    // A4 Kodo: multipart form to upload.qiniup.com with token/key/file parts.
    const HttpRequest kodo = buildKodoFormUploadRequest(cfg, secrets.kodoSecretKey, png,
                                                        file, now);
    check(kodo.method == "POST" && kodo.url == "https://upload.qiniup.com",
          "A4 kodo method+url", kodo.url);
    check(contains(headerOf(kodo, "Content-Type"), "multipart/form-data") &&
              contains(kodo.body, "name=\"token\"") &&
              contains(kodo.body, "name=\"key\"") && contains(kodo.body, file),
          "A4 kodo multipart parts");
}

// ---------------------------------------------------------------------------
// B. Full PicGo loop against a local python stand-in server.
// ---------------------------------------------------------------------------

const int kServerPort = 39171;
const wchar_t kPyScript[] = L"picgo_mock_server.py";

bool writeMockServer(const std::wstring& dir) {
    // Fixed-JSON PicGo stand-in: GET -> 200 for the readiness poll,
    // POST /upload -> {"success":true,"result":["<fixed url>"]}.
    const std::string py = R"PY(import json, sys
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(sys.argv[1])
UPLOAD_URL = "http://127.0.0.1:%d/mocks/picgo/TF_smoke.png" % PORT

class H(BaseHTTPRequestHandler):
    def do_GET(self):
        body = b"ready"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        data = self.rfile.read(n)
        try:
            payload = json.loads(data.decode("utf-8"))
        except Exception:
            payload = {}
        paths = payload.get("list") if isinstance(payload, dict) else None
        if not isinstance(paths, list) or not paths:
            body = json.dumps({"success": False, "message": "missing list"}).encode()
            code = 400
        else:
            body = json.dumps({"success": True, "result": [UPLOAD_URL]}).encode()
            code = 200
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass

HTTPServer(("127.0.0.1", PORT), H).serve_forever()
)PY";
    std::ofstream out(std::filesystem::path(dir) / kPyScript, std::ios::binary);
    if (!out) return false;
    out.write(py.data(), static_cast<std::streamsize>(py.size()));
    return out.good();
}

void runPicGoRoundTrip(const std::wstring& workDir) {
    std::wstring pyPath = (std::filesystem::path(workDir) / kPyScript).wstring();
    check(writeMockServer(workDir), "B1 write python stand-in", narrow(pyPath));

    // B2 launch python (console subsystem so TerminateProcess is deterministic).
    std::wstring cmd = L"python.exe \"" + pyPath + L"\" " + std::to_wstring(kServerPort);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');
    const BOOL launched = ::CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                                           CREATE_NO_WINDOW, nullptr, workDir.c_str(),
                                           &si, &pi);
    check(launched != 0, "B2 launch python stand-in");
    if (!launched) return;

    // B3 readiness poll: GET until 200 (or 12s).
    WinHttpTransport poller;
    HttpRequest probe;
    probe.method = "GET";
    probe.url = "http://127.0.0.1:" + std::to_string(kServerPort) + "/";
    bool ready = false;
    for (int i = 0; i < 60 && !ready; ++i) {
        const HttpOutcome o = poller.send(probe);
        ready = o.response.has_value() && o.response->ok();
        if (!ready) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    check(ready, "B3 stand-in ready (GET 200)");

    // B4 full upload: real transport, real temp file under the work dir.
    ImageUploadConfig cfg;
    cfg.activeHost = ImageUploadChannel::PicGo;
    cfg.picgoEndpoint = "http://127.0.0.1:" + std::to_string(kServerPort) + "/upload";
    const std::string png = "\x89PNG\r\n\x1a\nsmoke-payload";
    const std::string filename = uploadFilename(1794096000, "smkpkr");
    std::filesystem::path tempPath;
    const TempFileProvider writer = [&](const std::string& name, std::string_view bytes) {
        tempPath = std::filesystem::path(workDir) / std::filesystem::path(name);
        std::ofstream out(tempPath, std::ios::binary);
        if (!out) return std::string();  // empty = write failure
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out.good()) return std::string();
        const std::u8string u8p = tempPath.u8string();
        return std::string(u8p.begin(), u8p.end());
    };
    const TempFileRemover remover = [&](const std::string& pathUtf8) {
        std::error_code ec;
        std::filesystem::remove(
            std::filesystem::path(reinterpret_cast<const char8_t*>(pathUtf8.c_str())), ec);
    };

    WinHttpTransport transport;
    ImageUploadService service(std::shared_ptr<IHttpTransport>(&transport,
                                                            [](IHttpTransport*) {}),
                               writer, remover);
    const ImageUploadOutcome outcome = service.upload(cfg, ImageUploadSecrets{}, png,
                                                      filename, 1794096000);
    check(outcome.result.has_value(), "B4 upload outcome ok",
          outcome.error.has_value() ? outcome.error->message : "result present");
    if (outcome.result.has_value()) {
        const ImageUploadResult& r = *outcome.result;
        const std::string wantUrl =
            "http://127.0.0.1:" + std::to_string(kServerPort) + "/mocks/picgo/TF_smoke.png";
        check(r.url == wantUrl, "B5 url from server JSON", r.url);
        check(r.markdown == "![" + filename + "](" + wantUrl + ")", "B5 markdown shape",
              r.markdown);
        check(r.provider == ImageUploadChannel::PicGo &&
                  ImageUploadService::clipboardText(cfg, r) == r.markdown,
              "B5 provider+clipboard");
    }
    check(!std::filesystem::exists(tempPath), "B6 temp file removed after upload",
          narrow(tempPath.wstring()));

    // B7 stop the stand-in.
    ::TerminateProcess(pi.hProcess, 0);
    ::WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = STILL_ACTIVE;
    ::GetExitCodeProcess(pi.hProcess, &exitCode);
    check(exitCode != STILL_ACTIVE, "B7 stand-in stopped");
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
}

// ---------------------------------------------------------------------------
// C. Picker: real lens window, simulated input, real pixel cross-check.
// ---------------------------------------------------------------------------

void pumpMessages(int ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    MSG msg;
    for (;;) {
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        if (std::chrono::steady_clock::now() >= until) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void runPickerSmoke() {
    ColorPickerController& picker = ColorPickerController::instance();
    check(ColorPickerController::registerClass(::GetModuleHandleW(nullptr)) &&
              picker.precreate(),
          "C1 picker class + precreate");
    check(!picker.isActive(), "C1 picker starts inactive");

    // C2 begin session -> visible topmost lens across the desktop.
    ColorPickResult picked{};
    std::atomic<bool> pickedDone{false};
    const bool began = picker.beginSession([&](const ColorPickResult& r) {
        picked = r;
        pickedDone.store(true);
    });
    check(began && picker.isActive(), "C2 beginSession");
    HWND lens = static_cast<HWND>(picker.hwndForTest());
    check(lens && ::IsWindow(lens) && ::IsWindowVisible(lens), "C2 lens window visible");

    // C3 pick at the primary-monitor center: move, click, cross-check pixel.
    const long sx = ::GetSystemMetrics(SM_CXSCREEN) / 2;
    const long sy = ::GetSystemMetrics(SM_CYSCREEN) / 2;
    ::SetCursorPos(static_cast<int>(sx), static_cast<int>(sy));
    RECT wr{};
    ::GetWindowRect(lens, &wr);
    const LPARAM click = MAKELPARAM(static_cast<SHORT>(sx - wr.left),
                                    static_cast<SHORT>(sy - wr.top));
    ::SendMessageW(lens, WM_MOUSEMOVE, 0, click);
    ::SendMessageW(lens, WM_LBUTTONDOWN, MK_LBUTTON, click);
    pumpMessages(100);
    check(pickedDone.load() && picked.picked, "C3 click pick completed");
    const auto hexParsed = colorPickParseHex(picked.hex);
    check(hexParsed.has_value() && picked.hex.size() == 7 && picked.hex[0] == '#',
          "C3 hex form", picked.hex);
    std::uint8_t rr = 0, gg = 0, bb = 0;
    const bool probed =
        colorPickSamplePixel(picked.screenX, picked.screenY, &rr, &gg, &bb);
    check(probed && hexParsed.has_value() && (*hexParsed)[0] == rr &&
              (*hexParsed)[1] == gg && (*hexParsed)[2] == bb,
          "C3 pixel cross-check vs direct probe",
          picked.hex + " vs " + colorPickHex(rr, gg, bb));
    check(!picker.isActive(), "C3 session closed after pick");

    // C4 Esc cancels a second session.
    ColorPickResult cancelled{};
    std::atomic<bool> cancelDone{false};
    check(picker.beginSession([&](const ColorPickResult& r) {
              cancelled = r;
              cancelDone.store(true);
          }),
          "C4 begin second session");
    ::SendMessageW(static_cast<HWND>(picker.hwndForTest()), WM_KEYDOWN, VK_ESCAPE, 0);
    pumpMessages(100);
    check(cancelDone.load() && !cancelled.picked && !picker.isActive(),
          "C4 Esc cancels");
}

// ---------------------------------------------------------------------------
// D. Color history window over a throwaway store.
// ---------------------------------------------------------------------------

void runColorHistorySmoke(const std::wstring& workDir) {
    const std::wstring dbPath = std::filesystem::path(workDir) / L"colors.db";
    std::error_code ec;
    std::filesystem::remove(dbPath, ec);
    std::string openError;
    auto store = HistoryStore::open(narrow(dbPath), &openError);
    check(store != nullptr, "D1 temp store open", openError);

    const char* hexes[] = {"#1A2B3C", "#AB34EF", "#00FF88"};
    for (const char* hex : hexes) {
        TranslationHistory e = TranslationHistory::colorPicked(hex);
        e.timestamp = tf::core::now();
        store->insert(e);
    }
    TranslationHistory tr;
    tr.id = core::Uuid::randomUuid();
    tr.sourceText = "hello";
    tr.translatedText = "你好";
    tr.timestamp = tf::core::now();
    store->insert(tr);
    check(store->colorCount() == 3 && store->totalEntries() == 4, "D1 rows inserted");

    // D2 window over the store.
    check(ColorHistoryWindowController::registerClass(::GetModuleHandleW(nullptr)),
          "D2 register window class");
    ColorHistoryWindowController window(store.get());
    window.show();
    pumpMessages(200);
    check(window.isVisible() && window.rowCount() == 3, "D2 grid rows",
          std::to_string(window.rowCount()));
    check(window.hexAt(0) == "#00FF88", "D2 newest first", window.hexAt(0));

    // D3 search: bare fragment and '#'-prefixed behave the same.
    window.applySearchForTest(L"AB");
    check(window.rowCount() == 1 && window.hexAt(0) == "#AB34EF", "D3 search AB",
          std::to_string(window.rowCount()));
    window.applySearchForTest(L"#ab");
    check(window.rowCount() == 1, "D3 search #ab case-insensitive");
    window.applySearchForTest(L"");
    check(window.rowCount() == 3, "D3 clear search");

    // D4 favorite toggle persists to the store.
    const std::string firstId = window.visibleRows()[0].id.toString();
    window.toggleFavoriteAtForTest(0);
    check(store->entryById(firstId).has_value() &&
              store->entryById(firstId)->isFavorite,
          "D4 favorite persisted");
    window.toggleFavoriteAtForTest(0);
    check(!store->entryById(firstId)->isFavorite, "D4 favorite toggled back");

    // D5 delete one row.
    window.deleteAtForTest(0);
    check(window.rowCount() == 2 && store->colorCount() == 2, "D5 delete one");

    // D6 RGB copy-format toggle (persisted setting; restored to HEX default).
    window.setCopyFormatRgbForTest(true);
    check(window.copyFormatRgb(), "D6 rgb format on");
    window.setCopyFormatRgbForTest(false);
    check(!window.copyFormatRgb(), "D6 rgb format back to HEX default");

    // D7 clear-all removes only color entries.
    window.clearAllForTest();
    check(window.rowCount() == 0 && store->colorCount() == 0 &&
              store->totalEntries() == 1,
          "D7 clear-all leaves non-color rows",
          "totalEntries=" + std::to_string(store->totalEntries()));

    window.close();
    pumpMessages(100);
    check(!window.isVisible(), "D8 window closed");
}

}  // namespace

int main() {
    std::printf("== TransFrame uploads + color picker smoke (FR-31/32/41) ==\n");
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Throwaway work dir; removed on exit.
    wchar_t tempRoot[MAX_PATH]{};
    ::GetTempPathW(MAX_PATH, tempRoot);
    const std::wstring workDir =
        std::filesystem::path(tempRoot) / L"TransFrameSmokeUploadsPicker";
    std::error_code ec;
    std::filesystem::create_directories(workDir, ec);

    runChannelShapes();
    runPicGoRoundTrip(workDir);
    runPickerSmoke();
    runColorHistorySmoke(workDir);

    std::filesystem::remove_all(workDir, ec);
    std::printf("== %s (%d failures) ==\n", g_failures == 0 ? "SMOKE PASS" : "SMOKE FAIL",
                g_failures);
    ::CoUninitialize();
    return g_failures == 0 ? 0 : 1;
}

#else
int main() { return 0; }
#endif  // _WIN32
