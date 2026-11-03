// Annotation.cpp — JSON codecs for the 11 annotation payloads.
#include "Annotation.h"

#include <stdexcept>

namespace tf::core {

namespace {

/// decodes an optional member; missing/null -> nullopt (Swift decodeIfPresent).
template <typename T>
std::optional<T> opt(const Json& j, const char* key) {
    return getOptional<T>(j, key);
}

}  // namespace

// ---------------------------------------------------------------- rectangle --
void to_json(Json& j, const RectangleAnnotation& a) {
    j = Json{{"id", a.id},   {"rect", a.rect},         {"style", a.style},
             {"isFilled", a.isFilled}};
    j["fillColor"] = a.fillColor ? Json(*a.fillColor) : Json();
}
void from_json(const Json& j, RectangleAnnotation& a) {
    a.id = j.at("id").get<Uuid>();
    a.rect = j.at("rect").get<Rect>();
    a.style = j.at("style").get<StrokeStyle>();
    a.isFilled = getOptionalOr<bool>(j, "isFilled", false);
    a.fillColor = opt<CodableColor>(j, "fillColor");
}

// ---------------------------------------------------------------- freehand --
void to_json(Json& j, const FreehandAnnotation& a) {
    j = Json{{"id", a.id}, {"points", a.points}, {"style", a.style}};
}
void from_json(const Json& j, FreehandAnnotation& a) {
    a.id = j.at("id").get<Uuid>();
    a.points = j.at("points").get<std::vector<Point>>();
    a.style = j.at("style").get<StrokeStyle>();
}

// ------------------------------------------------------------------- arrow --
void to_json(Json& j, const ArrowAnnotation& a) {
    j = Json{{"id", a.id}, {"startPoint", a.startPoint}, {"endPoint", a.endPoint},
             {"style", a.style}};
    j["controlPoint"] = a.controlPoint ? Json(*a.controlPoint) : Json();
}
void from_json(const Json& j, ArrowAnnotation& a) {
    a.id = j.at("id").get<Uuid>();
    a.startPoint = j.at("startPoint").get<Point>();
    a.endPoint = j.at("endPoint").get<Point>();
    a.style = j.at("style").get<StrokeStyle>();
    a.controlPoint = opt<Point>(j, "controlPoint");
}

// ------------------------------------------------------------------- text --
void to_json(Json& j, const TextAnnotation& a) {
    j = Json{{"id", a.id}, {"position", a.position}, {"content", a.content}, {"style", a.style}};
    j["wrapWidth"] = a.wrapWidth ? Json(*a.wrapWidth) : Json();
}
void from_json(const Json& j, TextAnnotation& a) {
    a.id = j.at("id").get<Uuid>();
    a.position = j.at("position").get<Point>();
    a.content = j.at("content").get<std::string>();
    a.style = j.at("style").get<TextStyle>();
    a.wrapWidth = opt<double>(j, "wrapWidth");
}

// ---------------------------------------------------------------- ellipse --
void to_json(Json& j, const EllipseAnnotation& a) {
    j = Json{{"id", a.id},   {"rect", a.rect},         {"style", a.style},
             {"isFilled", a.isFilled}};
    j["fillColor"] = a.fillColor ? Json(*a.fillColor) : Json();
}
void from_json(const Json& j, EllipseAnnotation& a) {
    a.id = j.at("id").get<Uuid>();
    a.rect = j.at("rect").get<Rect>();
    a.style = j.at("style").get<StrokeStyle>();
    a.isFilled = getOptionalOr<bool>(j, "isFilled", false);
    a.fillColor = opt<CodableColor>(j, "fillColor");
}

// ------------------------------------------------------------------- line --
void to_json(Json& j, const LineAnnotation& a) {
    j = Json{{"id", a.id}, {"startPoint", a.startPoint}, {"endPoint", a.endPoint},
             {"style", a.style}};
}
void from_json(const Json& j, LineAnnotation& a) {
    a.id = j.at("id").get<Uuid>();
    a.startPoint = j.at("startPoint").get<Point>();
    a.endPoint = j.at("endPoint").get<Point>();
    a.style = j.at("style").get<StrokeStyle>();
}

// ----------------------------------------------------------------- mosaic --
void to_json(Json& j, const MosaicAnnotation& a) {
    j = Json{{"id", a.id}, {"rect", a.rect}, {"blockSize", a.blockSize}};
}
void from_json(const Json& j, MosaicAnnotation& a) {
    a.id = j.at("id").get<Uuid>();
    a.rect = j.at("rect").get<Rect>();
    a.blockSize = MosaicAnnotation::clampBlockSize(j.at("blockSize").get<int>());
}

// -------------------------------------------------------------- highlight --
void to_json(Json& j, const HighlightAnnotation& a) {
    j = Json{{"id", a.id}, {"rect", a.rect}, {"color", a.color}, {"opacity", a.opacity}};
}
void from_json(const Json& j, HighlightAnnotation& a) {
    a.id = j.at("id").get<Uuid>();
    a.rect = j.at("rect").get<Rect>();
    a.color = j.at("color").get<CodableColor>();
    a.opacity = std::clamp(j.at("opacity").get<double>(), 0.1, 0.8);
}

// ------------------------------------------------------------ numberLabel --
void to_json(Json& j, const NumberLabelAnnotation& a) {
    j = Json{{"id", a.id}, {"position", a.position}, {"number", a.number}, {"size", a.size},
             {"color", a.color}};
    j["leadPoint"] = a.leadPoint ? Json(*a.leadPoint) : Json();
}
void from_json(const Json& j, NumberLabelAnnotation& a) {
    a.id = j.at("id").get<Uuid>();
    a.position = j.at("position").get<Point>();
    a.number = std::max(1, j.at("number").get<int>());
    a.size = NumberLabelAnnotation::clampedSize(j.at("size").get<double>());
    a.color = j.at("color").get<CodableColor>();
    a.leadPoint = opt<Point>(j, "leadPoint");
}

// -------------------------------------------------------- privacyRedaction --
void to_json(Json& j, const PrivacyRedactionAnnotation& a) {
    j = Json{{"id", a.id}, {"rect", a.rect}, {"fillColor", a.fillColor}};
}
void from_json(const Json& j, PrivacyRedactionAnnotation& a) {
    a.id = j.at("id").get<Uuid>();
    a.rect = j.at("rect").get<Rect>();
    a.fillColor = j.at("fillColor").get<CodableColor>();
}

// ----------------------------------------------------------- calloutBubble --
void to_json(Json& j, const CalloutBubbleAnnotation& a) {
    j = Json{{"id", a.id}, {"rect", a.rect}, {"targetPoint", a.targetPoint},
             {"text", a.text}, {"style", a.style}, {"bubbleColor", a.bubbleColor}};
}
void from_json(const Json& j, CalloutBubbleAnnotation& a) {
    a.id = j.at("id").get<Uuid>();
    a.rect = j.at("rect").get<Rect>();
    a.targetPoint = j.at("targetPoint").get<Point>();
    a.text = j.at("text").get<std::string>();
    a.style = j.at("style").get<TextStyle>();
    a.bubbleColor = j.at("bubbleColor").get<CodableColor>();
}

// ---------------------------------------------------------- tagged wrapper --
std::string annotationTypeName(const Annotation& a) {
    // std::variant::index matches declaration order of AnnotationPayload.
    static const char* kNames[] = {"rectangle", "freehand",   "arrow",     "text",
                                   "ellipse",   "line",       "mosaic",    "highlight",
                                   "numberLabel", "privacyRedaction", "calloutBubble"};
    const auto index = a.payload.index();
    return index < std::size(kNames) ? kNames[index] : "rectangle";
}

void to_json(Json& j, const Annotation& a) {
    j = Json{{"type", annotationTypeName(a)}};
    std::visit([&j](const auto& payload) { j["value"] = payload; }, a.payload);
}

void from_json(const Json& j, Annotation& a) {
    const std::string type = j.at("type").get<std::string>();
    const Json& value = j.at("value");
    if (type == "rectangle") { a.payload = value.get<RectangleAnnotation>(); }
    else if (type == "freehand") { a.payload = value.get<FreehandAnnotation>(); }
    else if (type == "arrow") { a.payload = value.get<ArrowAnnotation>(); }
    else if (type == "text") { a.payload = value.get<TextAnnotation>(); }
    else if (type == "ellipse") { a.payload = value.get<EllipseAnnotation>(); }
    else if (type == "line") { a.payload = value.get<LineAnnotation>(); }
    else if (type == "mosaic") { a.payload = value.get<MosaicAnnotation>(); }
    else if (type == "highlight") { a.payload = value.get<HighlightAnnotation>(); }
    else if (type == "numberLabel") { a.payload = value.get<NumberLabelAnnotation>(); }
    else if (type == "privacyRedaction") { a.payload = value.get<PrivacyRedactionAnnotation>(); }
    else if (type == "calloutBubble") { a.payload = value.get<CalloutBubbleAnnotation>(); }
    else { throw std::invalid_argument("unknown annotation type: " + type); }
}

}  // namespace tf::core
