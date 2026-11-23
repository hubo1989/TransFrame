#pragma once
// Annotation.h — the 11 annotation types (macOS Annotation.swift), one-to-one.
// rectangle / freehand / arrow / text / ellipse / line / mosaic / highlight /
// numberLabel / privacyRedaction / calloutBubble.
//
// Codable notes: Annotation itself is an enum with associated values; on
// macOS it is NOT Codable (persisted documents re-serialize through the
// CaptureDocument layer that stores geometric primitives). We still provide
// a tagged-JSON codec for each concrete payload so task B can persist
// documents; the shape mirrors Swift's synthesized CodingKeys per struct.
//
// All geometry is in image pixel coordinates, top-left origin.

#include "Geometry.h"
#include "Json.h"
#include "Styles.h"
#include "Uuid.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace tf::core {

// ---------------------------------------------------------------- payloads ---

struct RectangleAnnotation {
    Uuid id;
    Rect rect;
    StrokeStyle style;
    bool isFilled = false;
    /// Fill color when filled; nullopt falls back to stroke color (legacy docs).
    std::optional<CodableColor> fillColor;

    bool isValid() const { return rect.width >= 5 && rect.height >= 5; }
    CodableColor effectiveFillColor() const { return fillColor ? *fillColor : style.color; }
    Rect bounds() const { return rect; }

    friend bool operator==(const RectangleAnnotation&, const RectangleAnnotation&) = default;
};

struct FreehandAnnotation {
    Uuid id;
    std::vector<Point> points;  // minimum 2
    StrokeStyle style;

    bool isValid() const { return points.size() >= 2; }
    Rect bounds() const {
        if (points.empty()) { return Rect::zero(); }
        double minX = HUGE_VAL, minY = HUGE_VAL, maxX = -HUGE_VAL, maxY = -HUGE_VAL;
        for (const Point& p : points) {
            minX = std::min(minX, p.x); minY = std::min(minY, p.y);
            maxX = std::max(maxX, p.x); maxY = std::max(maxY, p.y);
        }
        return Rect{minX, minY, maxX - minX, maxY - minY};
    }

    friend bool operator==(const FreehandAnnotation&, const FreehandAnnotation&) = default;
};

struct ArrowAnnotation {
    Uuid id;
    Point startPoint;  // tail
    Point endPoint;    // head
    std::optional<Point> controlPoint;  // quadratic curve; nullopt = straight
    StrokeStyle style;

    Rect bounds() const {
        double minX = std::min(startPoint.x, endPoint.x);
        double minY = std::min(startPoint.y, endPoint.y);
        double maxX = std::max(startPoint.x, endPoint.x);
        double maxY = std::max(startPoint.y, endPoint.y);
        if (controlPoint) {
            minX = std::min(minX, controlPoint->x); minY = std::min(minY, controlPoint->y);
            maxX = std::max(maxX, controlPoint->x); maxY = std::max(maxY, controlPoint->y);
        }
        const double padding = style.lineWidth * 3;  // arrowhead allowance
        return Rect{minX - padding, minY - padding,
                    (maxX - minX) + padding * 2, (maxY - minY) + padding * 2};
    }

    friend bool operator==(const ArrowAnnotation&, const ArrowAnnotation&) = default;
};

struct TextAnnotation {
    Uuid id;
    Point position;  // top-leading anchor
    std::string content;
    TextStyle style;
    /// Max width for wrapping; nullopt = single line (Swift wrapWidth).
    std::optional<double> wrapWidth;

    bool isValid() const {
        // trim whitespace/newlines
        std::size_t b = 0, e = content.size();
        while (b < e && std::isspace(static_cast<unsigned char>(content[b]))) { ++b; }
        while (e > b && std::isspace(static_cast<unsigned char>(content[e - 1]))) { --e; }
        return b < e;
    }
    /// Text bounds require font metrics; the Core layer returns the anchor
    /// point only — the UI layer measures (Swift caches NSFont measurements).
    Rect bounds() const { return Rect{position.x, position.y, 0, 0}; }

    friend bool operator==(const TextAnnotation&, const TextAnnotation&) = default;
};

struct EllipseAnnotation {
    Uuid id;
    Rect rect;
    StrokeStyle style;
    bool isFilled = false;
    std::optional<CodableColor> fillColor;

    bool isValid() const { return rect.width >= 5 && rect.height >= 5; }
    CodableColor effectiveFillColor() const { return fillColor ? *fillColor : style.color; }
    Rect bounds() const { return rect; }

    friend bool operator==(const EllipseAnnotation&, const EllipseAnnotation&) = default;
};

struct LineAnnotation {
    Uuid id;
    Point startPoint;
    Point endPoint;
    StrokeStyle style;

    bool isValid() const {
        const double dx = endPoint.x - startPoint.x;
        const double dy = endPoint.y - startPoint.y;
        return std::sqrt(dx * dx + dy * dy) >= 5;
    }
    Rect bounds() const {
        const double minX = std::min(startPoint.x, endPoint.x);
        const double minY = std::min(startPoint.y, endPoint.y);
        const double maxX = std::max(startPoint.x, endPoint.x);
        const double maxY = std::max(startPoint.y, endPoint.y);
        const double padding = style.lineWidth / 2;
        return Rect{minX - padding, minY - padding,
                    (maxX - minX) + padding * 2, (maxY - minY) + padding * 2};
    }

    friend bool operator==(const LineAnnotation&, const LineAnnotation&) = default;
};

struct MosaicAnnotation {
    Uuid id;
    Rect rect;
    int blockSize = 10;  // clamped 4...128 (Swift init clamps; doc says 8-32 typical)

    MosaicAnnotation() = default;
    MosaicAnnotation(Uuid id_, Rect r, int blockSize_ = 10)
        : id(std::move(id_)), rect(r), blockSize(clampBlockSize(blockSize_)) {}

    static int clampBlockSize(int size) { return std::clamp(size, 4, 128); }
    bool isValid() const { return rect.width >= 10 && rect.height >= 10; }
    Rect bounds() const { return rect; }

    friend bool operator==(const MosaicAnnotation&, const MosaicAnnotation&) = default;
};

struct HighlightAnnotation {
    Uuid id;
    Rect rect;
    CodableColor color = CodableColor::yellowColor();
    double opacity = 0.4;  // clamped 0.1...0.8

    HighlightAnnotation() = default;
    HighlightAnnotation(Uuid id_, Rect r, CodableColor c = CodableColor::yellowColor(),
                        double op = 0.4)
        : id(std::move(id_)), rect(r), color(c), opacity(std::clamp(op, 0.1, 0.8)) {}

    bool isValid() const { return rect.width >= 5 && rect.height >= 5; }
    Rect bounds() const { return rect; }

    friend bool operator==(const HighlightAnnotation&, const HighlightAnnotation&) = default;
};

struct NumberLabelAnnotation {
    Uuid id;
    Point position;  // circle center
    int number = 1;  // >= 1
    double size = 24;     // circle diameter, clamped 12...256
    CodableColor color = CodableColor::redColor();
    std::optional<Point> leadPoint;

    NumberLabelAnnotation() = default;
    NumberLabelAnnotation(Uuid id_, Point pos, int number_ = 1, double size_ = 24,
                          CodableColor color_ = CodableColor::redColor(),
                          std::optional<Point> lead = std::nullopt)
        : id(std::move(id_)),
          position(pos),
          number(std::max(1, number_)),
          size(clampedSize(size_)),
          color(color_),
          leadPoint(lead) {}

    static double clampedSize(double size) { return std::clamp(size, 12.0, 256.0); }

    Rect circleRect() const {
        return Rect{position.x - size / 2, position.y - size / 2, size, size};
    }
    Rect bounds() const {
        Rect rect = circleRect();
        if (leadPoint) {
            rect = rect.unionWith(Rect{leadPoint->x - 4, leadPoint->y - 4, 8, 8});
        }
        return rect;
    }

    friend bool operator==(const NumberLabelAnnotation&, const NumberLabelAnnotation&) = default;
};

struct PrivacyRedactionAnnotation {
    Uuid id;
    Rect rect;
    CodableColor fillColor = CodableColor::blackColor();

    bool isValid() const { return rect.width >= 5 && rect.height >= 5; }
    Rect bounds() const { return rect; }

    friend bool operator==(const PrivacyRedactionAnnotation&, const PrivacyRedactionAnnotation&) = default;
};

struct CalloutBubbleAnnotation {
    Uuid id;
    Rect rect;              // bubble box
    Point targetPoint;      // pointer tip
    std::string text;
    TextStyle style;
    CodableColor bubbleColor = CodableColor::whiteColor();

    Rect bounds() const {
        const double minX = std::min(rect.minX(), targetPoint.x);
        const double minY = std::min(rect.minY(), targetPoint.y);
        const double maxX = std::max(rect.maxX(), targetPoint.x);
        const double maxY = std::max(rect.maxY(), targetPoint.y);
        return Rect{minX, minY, maxX - minX, maxY - minY};
    }

    friend bool operator==(const CalloutBubbleAnnotation&, const CalloutBubbleAnnotation&) = default;
};

// ------------------------------------------------------------- the variant ---

/// Annotation mirrors Swift's 11-case enum. std::variant keeps value
/// semantics and exhaustive visiting without heap allocation.
using AnnotationPayload = std::variant<RectangleAnnotation, FreehandAnnotation, ArrowAnnotation,
                                       TextAnnotation, EllipseAnnotation, LineAnnotation,
                                       MosaicAnnotation, HighlightAnnotation,
                                       NumberLabelAnnotation, PrivacyRedactionAnnotation,
                                       CalloutBubbleAnnotation>;

struct Annotation {
    AnnotationPayload payload;

    Annotation() : payload(RectangleAnnotation{}) {}

    // Tag-based constructors.
    static Annotation rectangle(RectangleAnnotation r) { return Annotation{std::move(r)}; }
    static Annotation freehand(FreehandAnnotation f) { return Annotation{std::move(f)}; }
    static Annotation arrow(ArrowAnnotation a) { return Annotation{std::move(a)}; }
    static Annotation text(TextAnnotation t) { return Annotation{std::move(t)}; }
    static Annotation ellipse(EllipseAnnotation e) { return Annotation{std::move(e)}; }
    static Annotation line(LineAnnotation l) { return Annotation{std::move(l)}; }
    static Annotation mosaic(MosaicAnnotation m) { return Annotation{std::move(m)}; }
    static Annotation highlight(HighlightAnnotation h) { return Annotation{std::move(h)}; }
    static Annotation numberLabel(NumberLabelAnnotation n) { return Annotation{std::move(n)}; }
    static Annotation privacyRedaction(PrivacyRedactionAnnotation p) {
        return Annotation{std::move(p)};
    }
    static Annotation calloutBubble(CalloutBubbleAnnotation c) { return Annotation{std::move(c)}; }

    Uuid id() const {
        return std::visit([](const auto& a) -> Uuid { return a.id; }, payload);
    }
    Rect bounds() const {
        return std::visit([](const auto& a) -> Rect { return a.bounds(); }, payload);
    }

    friend bool operator==(const Annotation&, const Annotation&) = default;

private:
    explicit Annotation(AnnotationPayload p) : payload(std::move(p)) {}
};

// ------------------------------------------------------------ JSON codecs ---
// Payload structs serialize with their Swift CodingKeys (synthesized order).
// The tagged Annotation wraps as {"type": "<case>", "value": {...}} — an
// explicit cross-platform shape since Swift's enum-with-payload Codable
// ({"rectangle":{"_0":{...}}}) is only used inside macOS-internal archives.

void to_json(Json& j, const RectangleAnnotation& a);
void from_json(const Json& j, RectangleAnnotation& a);
void to_json(Json& j, const FreehandAnnotation& a);
void from_json(const Json& j, FreehandAnnotation& a);
void to_json(Json& j, const ArrowAnnotation& a);
void from_json(const Json& j, ArrowAnnotation& a);
void to_json(Json& j, const TextAnnotation& a);
void from_json(const Json& j, TextAnnotation& a);
void to_json(Json& j, const EllipseAnnotation& a);
void from_json(const Json& j, EllipseAnnotation& a);
void to_json(Json& j, const LineAnnotation& a);
void from_json(const Json& j, LineAnnotation& a);
void to_json(Json& j, const MosaicAnnotation& a);
void from_json(const Json& j, MosaicAnnotation& a);
void to_json(Json& j, const HighlightAnnotation& a);
void from_json(const Json& j, HighlightAnnotation& a);
void to_json(Json& j, const NumberLabelAnnotation& a);
void from_json(const Json& j, NumberLabelAnnotation& a);
void to_json(Json& j, const PrivacyRedactionAnnotation& a);
void from_json(const Json& j, PrivacyRedactionAnnotation& a);
void to_json(Json& j, const CalloutBubbleAnnotation& a);
void from_json(const Json& j, CalloutBubbleAnnotation& a);
void to_json(Json& j, const Annotation& a);
void from_json(const Json& j, Annotation& a);

/// Non-empty lowercase case name ("rectangle" ... "calloutBubble").
std::string annotationTypeName(const Annotation& a);

}  // namespace tf::core
