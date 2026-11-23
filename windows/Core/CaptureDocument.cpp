// CaptureDocument.cpp — method bodies + JSON codecs for capture models.
#include "CaptureDocument.h"

#include <algorithm>
#include <sstream>

namespace tf::core {

// ------------------------------------------------------------- DisplayInfo --
std::string DisplayInfo::resolution() const {
    std::ostringstream out;
    out << static_cast<long long>(frame.width * scaleFactor) << " x "
        << static_cast<long long>(frame.height * scaleFactor);
    return out.str();
}

void to_json(Json& j, const DisplayInfo& d) {
    j = Json{{"id", d.id},     {"name", d.name}, {"frame", d.frame},
             {"scaleFactor", d.scaleFactor}, {"isPrimary", d.isPrimary}};
}
void from_json(const Json& j, DisplayInfo& d) {
    d.id = static_cast<std::uint32_t>(j.at("id").get<std::uint64_t>());
    d.name = j.at("name").get<std::string>();
    d.frame = j.at("frame").get<Rect>();
    d.scaleFactor = j.at("scaleFactor").get<double>();
    d.isPrimary = getOptionalOr<bool>(j, "isPrimary", false);
}

// ----------------------------------------------------------------- OCRText --
void to_json(Json& j, const OCRText& t) {
    j = Json{{"id", t.id}, {"text", t.text}, {"boundingBox", t.boundingBox},
             {"confidence", t.confidence}};
}
void from_json(const Json& j, OCRText& t) {
    t.id = j.at("id").get<Uuid>();
    t.text = j.at("text").get<std::string>();
    t.boundingBox = j.at("boundingBox").get<Rect>();
    t.confidence = static_cast<float>(getOptionalOr<double>(j, "confidence", 1.0));
}

// ---------------------------------------------------------------- OCRResult --
std::string OCRResult::fullText() const {
    std::vector<const OCRText*> sorted;
    sorted.reserve(observations.size());
    for (const auto& o : observations) { sorted.push_back(&o); }
    std::sort(sorted.begin(), sorted.end(),
              [](const OCRText* a, const OCRText* b) {
                  return a->boundingBox.minY() < b->boundingBox.minY();
              });
    std::string out;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (i > 0) { out += '\n'; }
        out += sorted[i]->text;
    }
    return out;
}

OCRResult OCRResult::filteredBy(float minimumConfidence) const {
    OCRResult out;
    out.imageWidth = imageWidth;
    out.imageHeight = imageHeight;
    out.timestamp = timestamp;
    out.observations.reserve(observations.size());
    for (const auto& o : observations) {
        if (o.confidence >= minimumConfidence) { out.observations.push_back(o); }
    }
    return out;
}

std::vector<OCRText> OCRResult::observationsIn(const Rect& rect) const {
    std::vector<OCRText> out;
    for (const auto& o : observations) {
        if (o.boundingBox.intersects(rect)) { out.push_back(o); }
    }
    return out;
}

// -------------------------------------------------------------- TextSegment --
void to_json(Json& j, const TextSegment& s) {
    j = Json{{"id", s.id}, {"text", s.text}, {"boundingBox", s.boundingBox},
             {"confidence", s.confidence}, {"isUserEdited", s.isUserEdited}};
}
void from_json(const Json& j, TextSegment& s) {
    s.id = j.at("id").get<Uuid>();
    s.text = j.at("text").get<std::string>();
    s.boundingBox = j.at("boundingBox").get<Rect>();
    s.confidence = static_cast<float>(getOptionalOr<double>(j, "confidence", 1.0));
    s.isUserEdited = getOptionalOr<bool>(j, "isUserEdited", false);
}

// ------------------------------------------------------ ScreenAnalysisResult --
std::string ScreenAnalysisResult::fullText() const {
    std::vector<const TextSegment*> sorted;
    sorted.reserve(segments.size());
    for (const auto& s : segments) { sorted.push_back(&s); }
    std::sort(sorted.begin(), sorted.end(),
              [](const TextSegment* a, const TextSegment* b) {
                  return a->boundingBox.minY() < b->boundingBox.minY();
              });
    std::string out;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (i > 0) { out += '\n'; }
        out += sorted[i]->text;
    }
    return out;
}

ScreenAnalysisResult ScreenAnalysisResult::filtered(float minimumConfidence) const {
    std::vector<TextSegment> passed, excluded;
    for (const auto& s : segments) {
        (s.confidence >= minimumConfidence ? passed : excluded).push_back(s);
    }
    std::vector<TextSegment> filtered = filteredOutSegments;
    filtered.insert(filtered.end(), excluded.begin(), excluded.end());
    return {std::move(passed), rawSegments, std::move(filtered), imageWidth, imageHeight};
}

ScreenAnalysisResult ScreenAnalysisResult::recoveringFilteredSegments() const {
    std::vector<TextSegment> all = segments;
    all.insert(all.end(), filteredOutSegments.begin(), filteredOutSegments.end());
    std::sort(all.begin(), all.end(), [](const TextSegment& a, const TextSegment& b) {
        const double ay = a.boundingBox.minY(), by = b.boundingBox.minY();
        if (std::abs(ay - by) > 1e-9) { return ay < by; }
        return a.boundingBox.minX() < b.boundingBox.minX();
    });
    return {std::move(all), rawSegments, {}, imageWidth, imageHeight};
}

ScreenAnalysisResult ScreenAnalysisResult::updatingSegment(const Uuid& segmentId,
                                                           const std::string& newText) const {
    ScreenAnalysisResult copy = *this;
    for (auto& s : copy.segments) {
        if (s.id == segmentId) {
            s.text = newText;
            s.isUserEdited = true;
        }
    }
    return copy;
}

ScreenAnalysisResult ScreenAnalysisResult::fromOcr(const OCRResult& ocr) {
    std::vector<TextSegment> segments;
    segments.reserve(ocr.observations.size());
    for (const auto& o : ocr.observations) {
        TextSegment s;
        s.id = o.id;
        s.text = o.text;
        s.boundingBox = o.boundingBox;
        s.confidence = o.confidence;
        segments.push_back(std::move(s));
    }
    return {std::move(segments), {}, {}, ocr.imageWidth, ocr.imageHeight};
}

void to_json(Json& j, const ScreenAnalysisResult& r) {
    j = Json{{"segments", r.segments},
             {"rawSegments", r.rawSegments},
             {"filteredOutSegments", r.filteredOutSegments},
             {"imageSize", Json::array({r.imageWidth, r.imageHeight})}};
}
void from_json(const Json& j, ScreenAnalysisResult& r) {
    r.segments = j.at("segments").get<std::vector<TextSegment>>();
    r.rawSegments = getOptionalOr<std::vector<TextSegment>>(j, "rawSegments", {});
    r.filteredOutSegments = getOptionalOr<std::vector<TextSegment>>(j, "filteredOutSegments", {});
    const auto size = j.at("imageSize").get<std::vector<std::int64_t>>();
    if (size.size() == 2) {
        r.imageWidth = size[0];
        r.imageHeight = size[1];
    }
}

// --------------------------------------------------------------- Screenshot --
Size Screenshot::dimensions() const {
    // ImageRef resolution is a NativeShell concern; Core reports 0x0 when the
    // handle is unset (task B wires the real lookup).
    if (image == 0) { return Size{0, 0}; }
    // Placeholder until NativeShell exposes image info; dims flow through
    // CaptureDocument::baseImage instead.
    return Size{0, 0};
}

std::int64_t Screenshot::estimatedFileSize() const {
    const Size size = dimensions();
    const double pixels = size.width * size.height;
    return static_cast<std::int64_t>(pixels * estimatedBytesPerPixel(format));
}

std::string Screenshot::formattedFileSize() const {
    const std::int64_t bytes = estimatedFileSize();
    std::ostringstream out;
    if (bytes < 1024) {
        out << bytes << " B";
    } else if (bytes < 1024 * 1024) {
        out.precision(1);
        out << std::fixed << static_cast<double>(bytes) / 1024.0 << " KB";
    } else {
        out.precision(1);
        out << std::fixed << static_cast<double>(bytes) / (1024.0 * 1024.0) << " MB";
    }
    return out.str();
}

double Screenshot::aspectRatio() const {
    const Size size = dimensions();
    return size.height != 0 ? size.width / size.height : 0.0;
}

// --------------------------------------------------------- QuartzGlobalRect --
void to_json(Json& j, const QuartzGlobalRect& r) { j = r.rawValue; }
void from_json(const Json& j, QuartzGlobalRect& r) { r.rawValue = j.get<Rect>(); }

}  // namespace tf::core
