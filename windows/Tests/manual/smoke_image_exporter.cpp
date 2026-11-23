// Pixel regression for the preview's renderComposited -> PNG export path.
// No capture, windows, clipboard changes, OCR, or network calls. Build with
// build_smoke_image_exporter.bat, then run windows/build/smoke_image_exporter.exe.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "tf/ImageExporter.hpp"
#include "tf/ThumbnailCodec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* label) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

tf::PixelBuffer patternedImage() {
    tf::PixelBuffer image;
    image.width = 37;
    image.height = 29;
    image.stride = image.width * 4 + 20;  // exercise non-tight source rows
    image.dpiX = 120;
    image.dpiY = 144;
    image.data = std::shared_ptr<std::uint8_t[]>(new std::uint8_t[image.byteSize()]);
    std::memset(image.data.get(), 0xD3, image.byteSize());
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            auto* p = image.data.get() + y * image.stride + x * 4;
            p[0] = static_cast<std::uint8_t>((x * 11 + y * 3) % 193);
            p[1] = static_cast<std::uint8_t>((x * 5 + y * 13) % 211);
            p[2] = static_cast<std::uint8_t>((x * 17 + y * 7) % 227);
            p[3] = 255;
        }
    }
    return image;
}

const std::uint8_t* pixel(const tf::PixelBuffer& image, int x, int y) {
    return image.data.get() + y * image.stride + x * 4;
}

bool samePixels(const tf::PixelBuffer& a, const tf::PixelBuffer& b) {
    if (!a.valid() || !b.valid() || a.width != b.width || a.height != b.height) {
        return false;
    }
    for (int y = 0; y < a.height; ++y) {
        if (std::memcmp(pixel(a, 0, y), pixel(b, 0, y),
                        static_cast<std::size_t>(a.width) * 4) != 0) {
            return false;
        }
    }
    return true;
}

bool unchangedOutsideAnnotation(const tf::PixelBuffer& base,
                                 const tf::PixelBuffer& annotated) {
    if (!base.valid() || !annotated.valid() || base.width != annotated.width ||
        base.height != annotated.height) {
        return false;
    }
    for (int y = 0; y < base.height; ++y) {
        for (int x = 0; x < base.width; ++x) {
            // Rectangle [8,24] x [7,19], plus stroke/antialias allowance.
            if (x >= 6 && x < 26 && y >= 5 && y < 21) continue;
            if (std::memcmp(pixel(base, x, y), pixel(annotated, x, y), 4) != 0) {
                return false;
            }
        }
    }
    return true;
}

void checkPngRoundTrips(const tf::PixelBuffer& image, const char* label) {
    if (!image.valid()) {
        check(false, label);
        return;
    }
    const auto encoded = tf::encodePngToMemory(image);
    const auto decoded = tf::decodeImageMemory(encoded.data(), encoded.size());
    check(samePixels(image, decoded),
          (std::string(label) + ": memory PNG retains every pixel").c_str());

    const auto path = std::filesystem::temp_directory_path() /
        (L"tf_export_smoke_" + std::to_wstring(::GetCurrentProcessId()) +
         L"_" + std::to_wstring(::GetTickCount64()) + L".png");
    std::wstring error;
    const bool saved = tf::encodeToFile(image, path.wstring(), tf::ExportFormat::Png,
                                         100, error);
    if (!saved) std::fwprintf(stderr, L"PNG export error: %ls\n", error.c_str());
    std::vector<std::uint8_t> fileBytes;
    if (saved) {
        std::ifstream input(path, std::ios::binary);
        fileBytes.assign(std::istreambuf_iterator<char>(input),
                         std::istreambuf_iterator<char>());
    }
    const auto fromFile = tf::decodeImageMemory(fileBytes.data(), fileBytes.size());
    check(saved && samePixels(image, fromFile),
          (std::string(label) + ": file PNG retains every pixel").c_str());
    std::error_code removeError;
    std::filesystem::remove(path, removeError);
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const HRESULT com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) {
        std::printf("[FAIL] COM initialization: 0x%08lX\n",
                    static_cast<unsigned long>(com));
        return 1;
    }

    const tf::PixelBuffer base = patternedImage();
    const std::vector<std::uint8_t> originalBytes(base.data.get(),
                                                 base.data.get() + base.byteSize());
    tf::core::AnnotationDocument document;
    const auto plain = tf::renderComposited(base, document);
    check(plain.valid() && plain.width == base.width && plain.height == base.height &&
              plain.stride == base.width * 4 && plain.dpiX == base.dpiX &&
              plain.dpiY == base.dpiY,
          "empty document preserves dimensions/DPI and produces tight rows");
    check(samePixels(base, plain),
          "empty document preserves every opaque source pixel with padded rows");
    checkPngRoundTrips(plain, "empty document");

    tf::core::RectangleAnnotation rectangle;
    rectangle.rect = tf::core::Rect{8, 7, 16, 12};
    rectangle.style.color = tf::core::CodableColor{0, 1, 0, 1};
    rectangle.style.lineWidth = 2;
    rectangle.isFilled = true;
    rectangle.fillColor = tf::core::CodableColor{1, 0, 0, 1};
    document.add(tf::core::Annotation::rectangle(rectangle));
    const auto annotated = tf::renderComposited(base, document);
    const std::array<std::uint8_t, 4> red{0, 0, 255, 255};
    check(annotated.valid() && std::memcmp(pixel(annotated, 16, 13), red.data(), 4) == 0,
          "filled annotation paints its expected center pixel");
    check(unchangedOutsideAnnotation(base, annotated),
          "annotation preserves all source pixels outside its bounds");
    checkPngRoundTrips(annotated, "annotated document");
    check(std::memcmp(base.data.get(), originalBytes.data(), base.byteSize()) == 0,
          "compositing and PNG export leave source pixels and row padding untouched");

    // PixelBuffer is straight BGRA. Check conversion to premultiplied D2D
    // input once, then the exporter's existing flattening onto opaque white.
    auto translucent = patternedImage();
    auto* transparent = translucent.data.get();
    transparent[0] = 32;
    transparent[1] = 96;
    transparent[2] = 160;
    transparent[3] = 0;
    auto* half = translucent.data.get() + 4;
    half[0] = 32;
    half[1] = 96;
    half[2] = 160;
    half[3] = 128;
    const auto flattened = tf::renderComposited(translucent, tf::core::AnnotationDocument{});
    const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
    check(flattened.valid() && std::memcmp(pixel(flattened, 0, 0), white.data(), 4) == 0,
          "transparent straight-BGRA pixel flattens to white");
    bool alphaCorrect = flattened.valid();
    if (alphaCorrect) {
        const auto* actual = pixel(flattened, 1, 0);
        for (int channel = 0; channel < 3; ++channel) {
            const int expected = (half[channel] * 128 + 127) / 255 + 127;
            alphaCorrect = alphaCorrect && std::abs(static_cast<int>(actual[channel]) -
                                                    expected) <= 1;
        }
        alphaCorrect = alphaCorrect && actual[3] == 255;
    }
    check(alphaCorrect, "half-alpha source is premultiplied once before compositing");

    ::CoUninitialize();
    std::printf("== image exporter smoke: %d failure(s) ==\n", failures);
    return failures == 0 ? 0 : 1;
}
