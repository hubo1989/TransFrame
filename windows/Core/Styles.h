#pragma once
// Styles.h — CodableColor, StrokeStyle, TextStyle and overlay style models.
// The raw RGB defaults below were observed from NSColor(Color.*).deviceRGB
// on macOS. On Windows the UI layer may choose Fluent theme equivalents, but
// persisted values are platform-neutral doubles with the same field names.

#include "Json.h"

#include <algorithm>
#include <optional>
#include <string>

namespace tf::core {

struct CodableColor {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    double opacity = 1.0;

    static CodableColor redColor() { return {1.0, 0.25882354378700256, 0.2705882489681244, 1.0}; }
    static CodableColor blueColor() { return {0.0, 0.5686274766921997, 1.0, 1.0}; }
    static CodableColor greenColor() { return {0.1882352977991104, 0.8196078538894653, 0.34509801864624023, 1.0}; }
    static CodableColor yellowColor() { return {1.0, 0.8392157554626465, 0.0, 1.0}; }
    static CodableColor orangeColor() { return {1.0, 0.572549045085907, 0.1882352977991104, 1.0}; }
    static CodableColor whiteColor() { return {1.0, 1.0, 1.0, 1.0}; }
    static CodableColor blackColor() { return {0.0, 0.0, 0.0, 1.0}; }

    friend bool operator==(const CodableColor&, const CodableColor&) = default;

    void toJson(Json& j) const {
        j = Json{{"red", red}, {"green", green}, {"blue", blue}, {"opacity", opacity}};
    }
    static CodableColor fromJson(const Json& j) {
        return {j.at("red").get<double>(), j.at("green").get<double>(),
                j.at("blue").get<double>(), j.at("opacity").get<double>()};
    }
};
inline void to_json(Json& j, const CodableColor& c) { c.toJson(j); }
inline void from_json(const Json& j, CodableColor& c) { c = CodableColor::fromJson(j); }

struct StrokeStyle {
    CodableColor color = CodableColor::redColor();
    double lineWidth = 2.0;

    bool isValid() const { return lineWidth >= 1.0 && lineWidth <= 20.0; }
    StrokeStyle validated() const { return {color, std::clamp(lineWidth, 1.0, 20.0)}; }
    friend bool operator==(const StrokeStyle&, const StrokeStyle&) = default;

    void toJson(Json& j) const { j = Json{{"color", color}, {"lineWidth", lineWidth}}; }
    static StrokeStyle fromJson(const Json& j) {
        return {j.at("color").get<CodableColor>(), j.at("lineWidth").get<double>()};
    }
};
inline void to_json(Json& j, const StrokeStyle& s) { s.toJson(j); }
inline void from_json(const Json& j, StrokeStyle& s) { s = StrokeStyle::fromJson(j); }

struct TextStyle {
    CodableColor color = CodableColor::redColor();
    double fontSize = 14.0;
    std::string fontName = ".AppleSystemUIFont";

    bool isValid() const { return fontSize >= 8.0 && fontSize <= 72.0; }
    TextStyle validated() const { return {color, std::clamp(fontSize, 8.0, 72.0), fontName}; }
    friend bool operator==(const TextStyle&, const TextStyle&) = default;

    void toJson(Json& j) const { j = Json{{"color", color}, {"fontSize", fontSize}, {"fontName", fontName}}; }
    static TextStyle fromJson(const Json& j) {
        return {j.at("color").get<CodableColor>(), j.at("fontSize").get<double>(),
                j.at("fontName").get<std::string>()};
    }
};
inline void to_json(Json& j, const TextStyle& s) { s.toJson(j); }
inline void from_json(const Json& j, TextStyle& s) { s = TextStyle::fromJson(j); }

enum class FontWeight : int32_t { UltraLight, Thin, Light, Regular, Medium, Semibold, Bold, Heavy, Black };
inline std::string toRaw(FontWeight w) {
    switch (w) {
        case FontWeight::UltraLight: return "ultraLight";
        case FontWeight::Thin: return "thin";
        case FontWeight::Light: return "light";
        case FontWeight::Regular: return "regular";
        case FontWeight::Medium: return "medium";
        case FontWeight::Semibold: return "semibold";
        case FontWeight::Bold: return "bold";
        case FontWeight::Heavy: return "heavy";
        case FontWeight::Black: return "black";
        default: return "regular";
    }
}
inline FontWeight fontWeightFromRaw(const std::string& raw) {
    if (raw == "ultraLight") { return FontWeight::UltraLight; }
    if (raw == "thin") { return FontWeight::Thin; }
    if (raw == "light") { return FontWeight::Light; }
    if (raw == "medium") { return FontWeight::Medium; }
    if (raw == "semibold") { return FontWeight::Semibold; }
    if (raw == "bold") { return FontWeight::Bold; }
    if (raw == "heavy") { return FontWeight::Heavy; }
    if (raw == "black") { return FontWeight::Black; }
    return FontWeight::Regular;
}

struct TranslationFont {
    double size = 14.0;
    FontWeight weight = FontWeight::Medium;
    std::optional<std::string> fontName;

    friend bool operator==(const TranslationFont&, const TranslationFont&) = default;
    void toJson(Json& j) const {
        j = Json{{"size", size}, {"weight", toRaw(weight)}};
        j["fontName"] = fontName ? Json(*fontName) : Json();
    }
    static TranslationFont fromJson(const Json& j) {
        return {j.at("size").get<double>(), fontWeightFromRaw(j.at("weight").get<std::string>()),
                getOptional<std::string>(j, "fontName")};
    }
};
inline void to_json(Json& j, const TranslationFont& f) { f.toJson(j); }
inline void from_json(const Json& j, TranslationFont& f) { f = TranslationFont::fromJson(j); }

struct EdgePadding {
    double top = 4.0;
    double leading = 8.0;
    double bottom = 4.0;
    double trailing = 8.0;
    friend bool operator==(const EdgePadding&, const EdgePadding&) = default;
    void toJson(Json& j) const { j = Json{{"top", top}, {"leading", leading}, {"bottom", bottom}, {"trailing", trailing}}; }
    static EdgePadding fromJson(const Json& j) {
        return {j.at("top").get<double>(), j.at("leading").get<double>(),
                j.at("bottom").get<double>(), j.at("trailing").get<double>()};
    }
};
inline void to_json(Json& j, const EdgePadding& p) { p.toJson(j); }
inline void from_json(const Json& j, EdgePadding& p) { p = EdgePadding::fromJson(j); }

struct OverlayStyle {
    TranslationFont translationFont;
    CodableColor translationColor = CodableColor::whiteColor();
    CodableColor backgroundColor = {0.0, 0.0, 0.0, 0.75};
    EdgePadding padding;
    friend bool operator==(const OverlayStyle&, const OverlayStyle&) = default;
    void toJson(Json& j) const {
        j = Json{{"translationFont", translationFont}, {"translationColor", translationColor},
                 {"backgroundColor", backgroundColor}, {"padding", padding}};
    }
    static OverlayStyle fromJson(const Json& j) {
        return {j.at("translationFont").get<TranslationFont>(), j.at("translationColor").get<CodableColor>(),
                j.at("backgroundColor").get<CodableColor>(), j.at("padding").get<EdgePadding>()};
    }
};
inline void to_json(Json& j, const OverlayStyle& s) { s.toJson(j); }
inline void from_json(const Json& j, OverlayStyle& s) { s = OverlayStyle::fromJson(j); }

}  // namespace tf::core
