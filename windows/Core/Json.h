#pragma once
// Json.h — nlohmann/json adapters that reproduce the exact wire shapes of the
// macOS Swift Codable implementation. Every shape below was observed against
// JSONEncoder/JSONDecoder output (probe: 2026-11-02) — see report-A.md.
//
// Shapes:
//   CGRect      -> [[x, y], [width, height]]
//   UUID        -> "E621E1F8-C36C-495A-93FC-0C247A3E6E5F" (uppercase)
//   Date        -> seconds since 2001-01-01T00:00:00Z (double)
//   Data        -> base64 (explicit helpers below)
//   enum w/ payload (EngineChainItem.Kind) -> {"builtin":{"_0":"mtran"}}
//   enum-keyed dictionary -> alternating key/value array

#include "Geometry.h"
#include "Uuid.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace tf::core {
using Json = nlohmann::json;

// ---------------------------------------------------------------- CGPoint ---
inline void to_json(Json& j, const Point& p) { j = Json::array({p.x, p.y}); }
inline void from_json(const Json& j, Point& p) {
    p = Point{j.at(0).get<double>(), j.at(1).get<double>()};
}

// ----------------------------------------------------------------- CGSize ---
inline void to_json(Json& j, const Size& s) { j = Json::array({s.width, s.height}); }
inline void from_json(const Json& j, Size& s) {
    s = Size{j.at(0).get<double>(), j.at(1).get<double>()};
}

// ----------------------------------------------------------------- CGRect ---
// Swift encodes CGRect (a CGRectMake of (origin, size)) as nested arrays.
inline void to_json(Json& j, const Rect& r) {
    j = Json::array({Json::array({r.x, r.y}), Json::array({r.width, r.height})});
}
inline void from_json(const Json& j, Rect& r) {
    const auto& origin = j.at(0);
    const auto& size = j.at(1);
    r = Rect{origin.at(0).get<double>(), origin.at(1).get<double>(),
             size.at(0).get<double>(), size.at(1).get<double>()};
}

// ------------------------------------------------------------------- UUID ---
inline void to_json(Json& j, const Uuid& u) { j = u.toString(); }
inline void from_json(const Json& j, Uuid& u) {
    Uuid parsed;
    if (!Uuid::parse(j.get<std::string>(), parsed)) {
        throw std::invalid_argument("invalid UUID string");
    }
    u = parsed;
}

// ------------------------------------------------------------------- Date ---
// Swift Codable reference date: 2001-01-01T00:00:00Z = 978307200 unix seconds.
inline constexpr double kSwiftReferenceDateUnix = 978307200.0;

inline void to_json(Json& j, const std::chrono::system_clock::time_point& t) {
    using namespace std::chrono;
    const double unixSeconds =
        static_cast<double>(duration_cast<duration<double>>(t.time_since_epoch()).count());
    j = unixSeconds - kSwiftReferenceDateUnix;
}
inline void from_json(const Json& j, std::chrono::system_clock::time_point& t) {
    using namespace std::chrono;
    const double swiftSeconds = j.get<double>();
    t = system_clock::time_point{
        duration_cast<system_clock::duration>(duration<double>(swiftSeconds + kSwiftReferenceDateUnix))};
}

/// Convenience alias used by history models.
using TimePoint = std::chrono::system_clock::time_point;

/// ADL cannot find tf::core::to_json for std::chrono types (their namespace is
/// std::chrono, not tf::core), so hook the serializer explicitly. This makes
/// Json{{"timestamp", someTimePoint}} work inside initializer lists.
}  // namespace tf::core

namespace nlohmann {
template <>
struct adl_serializer<std::chrono::system_clock::time_point> {
    static void to_json(tf::core::Json& j, const std::chrono::system_clock::time_point& t) {
        ::tf::core::to_json(j, t);
    }
    static void from_json(const tf::core::Json& j, std::chrono::system_clock::time_point& t) {
        ::tf::core::from_json(j, t);
    }
};
}  // namespace nlohmann

namespace tf::core {

/// Current wall-clock time (thin wrapper so tests can inject fixed clocks later).
inline TimePoint now() { return std::chrono::system_clock::now(); }

// ----------------------------------------------------- optional members ----
// Swift `decodeIfPresent` semantics: missing key -> nullopt; JSON null -> nullopt.
template <typename T>
std::optional<T> getOptional(const Json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) { return std::nullopt; }
    return it->get<T>();
}

/// Swift's `decodeIfPresent(...) ?? defaultValue`.
template <typename T>
T getOptionalOr(const Json& j, const char* key, T fallback) {
    auto value = getOptional<T>(j, key);
    return value ? std::move(*value) : std::move(fallback);
}

/// Explicit Base64 helpers for Swift Data <-> std::vector<std::uint8_t>.
std::string encodeBase64(const std::vector<std::uint8_t>& data);
std::vector<std::uint8_t> decodeBase64(const std::string& text);

}  // namespace tf::core
