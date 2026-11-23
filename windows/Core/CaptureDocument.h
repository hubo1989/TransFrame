#pragma once
// CaptureDocument.h — capture session models (macOS Screenshot.swift +
// CaptureDocument.swift). The CGImage payload becomes an ImageRef handle:
// Core never touches pixels; NativeShell (task B) owns D2D/WIC bitmaps and
// hands out opaque 64-bit handles, so this header stays platform-clean.

#include "Annotation.h"
#include "ExportFormat.h"
#include "Geometry.h"
#include "Json.h"
#include "OCREngineType.h"
#include "Uuid.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tf::core {

/// Opaque image handle (NativeShell CGImage analogue). 0 = no image.
using ImageRef = std::uint64_t;

/// Connected display descriptor (macOS DisplayInfo.swift). On Windows the
/// id is the HMONITOR-cast value; name from EnumDisplayDevices; frame in
/// virtual-screen coordinates; scaleFactor the DPI ratio.
struct DisplayInfo {
    std::uint32_t id = 0;
    std::string name;
    Rect frame;             // points, global coordinates
    double scaleFactor = 1.0;
    bool isPrimary = false;

    /// "2560 x 1440"
    std::string resolution() const;
    bool isRetina() const { return scaleFactor > 1.0; }
    Size pixelSize() const {
        return Size{frame.width * scaleFactor, frame.height * scaleFactor};
    }

    friend bool operator==(const DisplayInfo&, const DisplayInfo&) = default;
};

/// OCR text observation (macOS OCRText) + result (macOS OCRResult).
/// Normalized bounding boxes: top-left origin, 0..1, y grows down.
struct OCRText {
    Uuid id;
    std::string text;
    Rect boundingBox;
    float confidence = 1.0f;

    friend bool operator==(const OCRText&, const OCRText&) = default;
};

struct OCRResult {
    std::vector<OCRText> observations;
    std::int64_t imageWidth = 0;
    std::int64_t imageHeight = 0;
    TimePoint timestamp{};

    std::size_t count() const { return observations.size(); }
    bool hasResults() const { return !observations.empty(); }
    /// Observations sorted by minY, joined with newlines (Swift fullText).
    std::string fullText() const;
    OCRResult filteredBy(float minimumConfidence) const;
    std::vector<OCRText> observationsIn(const Rect& rect) const;
    static OCRResult empty(std::int64_t width, std::int64_t height) {
        return OCRResult{{}, width, height, {}};
    }

    friend bool operator==(const OCRResult&, const OCRResult&) = default;
};

/// VLM analysis segment (macOS TextSegment in ScreenAnalysisResult.swift).
struct TextSegment {
    Uuid id;
    std::string text;
    Rect boundingBox;   // normalized 0..1, top-left origin
    float confidence = 1.0f;
    bool isUserEdited = false;

    bool isHighConfidence() const { return confidence > 0.7f; }
    Rect pixelBoundingBox(const Size& imageSize) const {
        return Rect{boundingBox.minX() * imageSize.width, boundingBox.minY() * imageSize.height,
                    boundingBox.width * imageSize.width,
                    boundingBox.height * imageSize.height};
    }

    friend bool operator==(const TextSegment&, const TextSegment&) = default;
};

/// VLM screen-analysis result (macOS ScreenAnalysisResult.swift).
struct ScreenAnalysisResult {
    std::vector<TextSegment> segments;
    std::vector<TextSegment> rawSegments;
    std::vector<TextSegment> filteredOutSegments;
    std::int64_t imageWidth = 0;
    std::int64_t imageHeight = 0;

    ScreenAnalysisResult() = default;
    ScreenAnalysisResult(std::vector<TextSegment> segments_, std::vector<TextSegment> raw_,
                         std::vector<TextSegment> filteredOut_, std::int64_t width,
                         std::int64_t height)
        : segments(std::move(segments_)),
          rawSegments(raw_.empty() ? segments : std::move(raw_)),
          filteredOutSegments(std::move(filteredOut_)),
          imageWidth(width),
          imageHeight(height) {}

    std::size_t count() const { return segments.size(); }
    bool hasResults() const { return !segments.empty(); }
    std::string fullText() const;
    ScreenAnalysisResult filtered(float minimumConfidence) const;
    ScreenAnalysisResult recoveringFilteredSegments() const;
    /// Replaces one segment's text and marks it user-edited.
    ScreenAnalysisResult updatingSegment(const Uuid& segmentId, const std::string& newText) const;
    Size imageSize() const { return Size{static_cast<double>(imageWidth),
                                         static_cast<double>(imageHeight)}; }
    static ScreenAnalysisResult empty(std::int64_t width, std::int64_t height) {
        return {{}, {}, {}, width, height};
    }
    static ScreenAnalysisResult fromOcr(const OCRResult& ocr);

    friend bool operator==(const ScreenAnalysisResult&, const ScreenAnalysisResult&) = default;
};

/// Captured screenshot (macOS Screenshot.swift). Not Codable on macOS (the
/// CGImage cannot encode); persisted state lives in CaptureDocument.
struct Screenshot {
    Uuid id;
    ImageRef image = 0;                 // NativeShell handle; 0 = none
    TimePoint captureDate{};
    DisplayInfo sourceDisplay;
    std::vector<Annotation> annotations;
    std::optional<std::string> filePath;  // until saved
    ExportFormat format = ExportFormat::Png;

    Size dimensions() const;  // asks NativeShell via ImageRef; 0x0 when unset
    std::int64_t estimatedFileSize() const;
    /// "1.2 MB" style string (Swift formattedFileSize).
    std::string formattedFileSize() const;
    double aspectRatio() const;

    friend bool operator==(const Screenshot&, const Screenshot&) = default;
};

/// Global screen rect wrapper (macOS QuartzGlobalRect) — the pin-back rect in
/// global coordinates for 1:1 in-place pinning.
struct QuartzGlobalRect {
    Rect rawValue;

    QuartzGlobalRect() = default;
    explicit QuartzGlobalRect(Rect r) : rawValue(r) {}
    friend bool operator==(const QuartzGlobalRect&, const QuartzGlobalRect&) = default;
};

/// Unified capture lifecycle document (macOS CaptureDocument.swift).
struct CaptureDocument {
    Uuid id;
    ImageRef baseImage = 0;             // immutable base pixels
    QuartzGlobalRect sourceScreenRect;  // for 1:1 pinning
    std::optional<DisplayInfo> sourceDisplay;
    std::optional<Rect> cropTransform;  // base-image pixel coords; nullopt = full
    std::vector<Annotation> annotations;
    int editRevision = 1;
    int analysisInputRevision = 1;
    int lastSavedRevision = 0;
    std::optional<ScreenAnalysisResult> cachedAnalysisResult;
    TimePoint createdAt{};

    friend bool operator==(const CaptureDocument&, const CaptureDocument&) = default;
};

// ------------------------------------------------------------ JSON codecs ---
// Only the persistable pieces serialize: DisplayInfo, TextSegment,
// ScreenAnalysisResult, QuartzGlobalRect, and CaptureDocument metadata
// (image handles are runtime-only and skipped).

void to_json(Json& j, const DisplayInfo& d);
void from_json(const Json& j, DisplayInfo& d);
void to_json(Json& j, const OCRText& t);
void from_json(const Json& j, OCRText& t);
void to_json(Json& j, const TextSegment& s);
void from_json(const Json& j, TextSegment& s);
void to_json(Json& j, const ScreenAnalysisResult& r);
void from_json(const Json& j, ScreenAnalysisResult& r);
void to_json(Json& j, const QuartzGlobalRect& r);
void from_json(const Json& j, QuartzGlobalRect& r);

}  // namespace tf::core
