// windows/Tests/manual/smoke_overlay.cpp — FR-26 translation-overlay render
// smoke (real D2D/WIC/DWrite run, synthetic input). Verifies the composite
// pixel pass end to end against the same renderTranslationOverlay() the app
// pipeline calls, in both modes (原位替换 / 下方追加), and exports PNGs for
// eyeball verification.
//
// Build (from windows/, after building TransFrame.sln Release x64) — or just
// run Tests\manual\build_smoke_overlay.bat which wraps this command:
//
//   cl /nologo /std:c++20 /EHsc /utf-8 /W4 /MD
//      /I NativeShell\include /I Core /I Core\include /I Core\vendor
//      Tests\manual\smoke_overlay.cpp /Fe:build\smoke_overlay.exe
//      /link build\x64\Release\TransFrame.NativeShell\TransFrame.NativeShell.lib
//            build\x64\Release\TransFrame.Core\TransFrame.Core.lib
//            d3d11.lib dxgi.lib dwmapi.lib shcore.lib d2d1.lib dwrite.lib
//            dcomp.lib windowscodecs.lib windowsapp.lib winhttp.lib
//            advapi32.lib user32.lib gdi32.lib ole32.lib
//   build\smoke_overlay.exe     (writes smoke_overlay_replace.png / _append.png)
//
// Synthetic scene (800x600): top half light background, bottom half dark;
// one fake bilingual segment per half (normalized bbox, macOS
// BilingualSegment parity: bbox in 0..1 + translated text).
//
// Checks (each prints PASS/FAIL; exit code != 0 on any FAIL):
//   O1 replace: output dims == input dims, tight stride
//   O2 replace: pixels changed inside both bboxes (fill + text drawn)
//   O3 replace: pixels OUTSIDE bboxes byte-identical to the input
//   O4 replace: adaptive polarity — light bbox got dark strokes, dark bbox
//      got light strokes (min/max luminance inside each bbox diverges from
//      the uniform base in the right direction)
//   O5 append: original bbox pixels untouched; band below bbox changed
//   O6 both modes export non-empty PNGs via the app's WIC path
//
// NOTE: the stroke-polarity check samples the bbox AFTER compositing; the
// semi-transparent fill shifts every pixel, so the check compares against
// the fill-only expectation via min/max (text strokes are strictly darker /
// lighter than the surrounding fill).

#ifdef _WIN32

#include "tf/CoreTypes.hpp"
#include "tf/ImageExporter.hpp"
#include "tf/OverlayRenderer.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

using namespace tf;

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

constexpr std::int32_t kWidth = 800;
constexpr std::int32_t kHeight = 600;

/// Uniform light top half (B,G,R = 232), dark bottom half (B,G,R = 16).
PixelBuffer makeBase() {
    PixelBuffer px;
    px.width = kWidth;
    px.height = kHeight;
    px.stride = kWidth * 4;
    px.data = std::shared_ptr<std::uint8_t[]>(
        new std::uint8_t[static_cast<std::size_t>(px.stride) * kHeight]);
    for (std::int32_t y = 0; y < kHeight; ++y) {
        std::uint8_t* row = px.data.get() + static_cast<std::size_t>(y) * px.stride;
        const std::uint8_t v = y < kHeight / 2 ? 232 : 16;
        for (std::int32_t x = 0; x < kWidth; ++x) {
            row[x * 4 + 0] = v;
            row[x * 4 + 1] = v;
            row[x * 4 + 2] = v;
            row[x * 4 + 3] = 255;
        }
    }
    return px;
}

/// Fake BilingualSegments (normalized bbox + translation), one per half.
std::vector<OverlaySegment> makeSegments() {
    const auto toPixel = [](double nx, double ny, double nw, double nh) {
        return Rect{static_cast<std::int64_t>(nx * kWidth),
                    static_cast<std::int64_t>(ny * kHeight),
                    static_cast<std::int64_t>(nw * kWidth),
                    static_cast<std::int64_t>(nh * kHeight)};
    };
    return {
        OverlaySegment{toPixel(0.10, 0.10, 0.50, 0.06), "你好，这是译文覆盖层"},
        OverlaySegment{toPixel(0.10, 0.55, 0.50, 0.06), "Hello translated overlay"},
    };
}

std::uint64_t changedPixels(const PixelBuffer& a, const PixelBuffer& b,
                            const Rect* onlyInside, const Rect* onlyOutside) {
    std::uint64_t changed = 0;
    for (std::int32_t y = 0; y < a.height; ++y) {
        const std::uint8_t* ra = a.data.get() + static_cast<std::size_t>(y) * a.stride;
        const std::uint8_t* rb = b.data.get() + static_cast<std::size_t>(y) * b.stride;
        for (std::int32_t x = 0; x < a.width; ++x) {
            const bool in = onlyInside && onlyInside->contains({x, y});
            const bool out = onlyOutside && !onlyOutside->contains({x, y});
            if (onlyInside && !in) continue;
            if (onlyOutside && !out) continue;
            if (ra[x * 4] != rb[x * 4] || ra[x * 4 + 1] != rb[x * 4 + 1] ||
                ra[x * 4 + 2] != rb[x * 4 + 2]) {
                ++changed;
            }
        }
    }
    return changed;
}

/// Min/max luma (BT.601-ish) inside a rect.
void lumaRange(const PixelBuffer& px, const Rect& r, int& minV, int& maxV) {
    minV = 255;
    maxV = 0;
    for (std::int64_t y = r.top(); y < r.bottom(); ++y) {
        const std::uint8_t* row =
            px.data.get() + static_cast<std::size_t>(y) * px.stride;
        for (std::int64_t x = r.left(); x < r.right(); ++x) {
            const int l = (row[x * 4] * 299 + row[x * 4 + 1] * 587 +
                           row[x * 4 + 2] * 114) /
                          1000;
            minV = std::min(minV, l);
            maxV = std::max(maxV, l);
        }
    }
}

Rect unionOf(const std::vector<OverlaySegment>& segs) {
    std::int64_t l = INT64_MAX, t = INT64_MAX, r = 0, b = 0;
    for (const auto& s : segs) {
        l = std::min(l, s.pixelBox.left());
        t = std::min(t, s.pixelBox.top());
        r = std::max(r, s.pixelBox.right());
        b = std::max(b, s.pixelBox.bottom());
    }
    return Rect{l, t, r - l, b - t};
}

std::string narrow(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0,
                                        nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n > 0 ? n : 1), '\0');
    if (n > 0) ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), n,
                                     nullptr, nullptr);
    out.resize(out.size() - 1);
    return out;
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // crash triage: no lost output
    std::printf("== TransFrame Windows overlay render smoke (FR-26) ==\n");
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    const PixelBuffer base = makeBase();
    const std::vector<OverlaySegment> segments = makeSegments();
    const Rect lightBox = segments[0].pixelBox;
    const Rect darkBox = segments[1].pixelBox;
    const Rect allBoxes = unionOf(segments);

    // ---------------- replace mode ----------------
    const PixelBuffer replaced =
        renderTranslationOverlay(base, segments, OverlayTextMode::ReplaceOriginal);

    check(replaced.valid() && replaced.width == base.width &&
              replaced.height == base.height && replaced.stride == base.width * 4,
          "O1 replace: dims/stride match input",
          std::to_string(replaced.width) + "x" + std::to_string(replaced.height));

    const std::uint64_t changedLight = changedPixels(base, replaced, &lightBox, nullptr);
    const std::uint64_t changedDark = changedPixels(base, replaced, &darkBox, nullptr);
    const std::int64_t boxPixels = lightBox.width * lightBox.height;
    check(changedLight > static_cast<std::uint64_t>(boxPixels / 10) &&
              changedDark > static_cast<std::uint64_t>(boxPixels / 10),
          "O2 replace: text+fill changed pixels inside bboxes",
          "light=" + std::to_string(changedLight) + " dark=" + std::to_string(changedDark) +
              " box=" + std::to_string(boxPixels));

    const std::uint64_t changedOutside =
        changedPixels(base, replaced, nullptr, &allBoxes);
    check(changedOutside == 0, "O3 replace: outside bboxes untouched",
          "changed=" + std::to_string(changedOutside));

    {
        int minL = 0, maxL = 0, minD = 0, maxD = 0;
        lumaRange(replaced, lightBox, minL, maxL);
        lumaRange(replaced, darkBox, minD, maxD);
        // Light background: fill stays light, strokes dip dark.
        const bool lightOk = maxL > 150 && minL < 100;
        // Dark background: fill stays dark, strokes rise light.
        const bool darkOk = minD < 110 && maxD > 160;
        check(lightOk && darkOk, "O4 replace: adaptive text polarity",
              "light[" + std::to_string(minL) + "," + std::to_string(maxL) + "] dark[" +
                  std::to_string(minD) + "," + std::to_string(maxD) + "]");
    }

    // ---------------- append mode ----------------
    const PixelBuffer appended =
        renderTranslationOverlay(base, segments, OverlayTextMode::AppendBelow);
    check(appended.valid() && appended.width == base.width &&
              appended.height == base.height,
          "O5 append: dims match input",
          std::to_string(appended.width) + "x" + std::to_string(appended.height));
    {
        // Append must not touch EITHER original bbox (union would wrongly
        // include the band below the first segment).
        const std::uint64_t lightUntouched = changedPixels(base, appended, &lightBox, nullptr);
        const std::uint64_t darkUntouched = changedPixels(base, appended, &darkBox, nullptr);
        check(lightUntouched == 0 && darkUntouched == 0,
              "O5 append: original bboxes untouched",
              "light=" + std::to_string(lightUntouched) +
                  " dark=" + std::to_string(darkUntouched));
        // Band below the light segment: [lightBox.bottom()+2, +bandHeight].
        const OverlayStyle style;
        const std::int64_t bandH = std::max<std::int64_t>(
            static_cast<std::int64_t>(static_cast<float>(lightBox.height) *
                                      style.appendBandFactor),
            static_cast<std::int64_t>(style.minFontPx * 1.6f));
        const Rect band{lightBox.x, lightBox.bottom() + style.appendGapPx,
                        lightBox.width, bandH};
        const std::uint64_t bandChanged = changedPixels(base, appended, &band, nullptr);
        check(bandChanged > static_cast<std::uint64_t>(band.width * band.height / 10),
              "O5 append: band below line carries translation",
              "changed=" + std::to_string(bandChanged) + " band=" +
                  std::to_string(band.width * band.height));
    }

    // ---------------- PNG export ----------------
    {
        const std::wstring names[2] = {L"smoke_overlay_replace.png",
                                       L"smoke_overlay_append.png"};
        const PixelBuffer* bufs[2] = {&replaced, &appended};
        const char* tags[2] = {"O6 replace PNG", "O6 append PNG"};
        for (int i = 0; i < 2; ++i) {
            std::wstring error;
            const bool saved = encodeToFile(*bufs[i], names[i], ExportFormat::Png, 100,
                                            error);
            WIN32_FILE_ATTRIBUTE_DATA attrs{};
            const bool exists = saved &&
                                ::GetFileAttributesExW(names[i].c_str(),
                                                       GetFileExInfoStandard, &attrs) &&
                                attrs.nFileSizeLow > 0;
            check(exists, tags[i],
                  exists ? "smoke file, " +
                               std::to_string(attrs.nFileSizeLow / 1024) + " KiB"
                         : " error=" + narrow(error));
        }
    }

    std::printf("== smoke complete: %d failure(s) ==\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}

#else  // !_WIN32

#include <cstdio>

int main() {
    std::printf(
        "smoke_overlay: Windows-only manual smoke (build with MSVC on Windows; "
        "this host is not Windows — NOT RUN).\n");
    return 0;
}

#endif  // _WIN32
