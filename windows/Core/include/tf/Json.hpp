// tf/Json.hpp — thin helpers over vendored nlohmann/json.hpp so Core sources
// don't each hand-roll parse error handling.
#ifndef TF_JSON_HPP
#define TF_JSON_HPP

#include "CoreModels.hpp"
#include "nlohmann/json.hpp"

namespace tf {

using Json = nlohmann::json;

inline std::string jsonDump(const Json& j) { return j.dump(); }

}  // namespace tf

#endif  // TF_JSON_HPP
