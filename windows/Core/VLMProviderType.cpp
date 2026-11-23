// VLMProviderType.cpp — CompatibleConfig codec.
#include "VLMProviderType.h"

namespace tf::core {

void to_json(Json& j, const CompatibleConfig& c) {
    j = Json{{"id", c.id},
             {"displayName", c.displayName},
             {"baseURL", c.baseURL},
             {"modelName", c.modelName},
             {"hasAPIKey", c.hasAPIKey}};
}

void from_json(const Json& j, CompatibleConfig& c) {
    c.id = j.at("id").get<Uuid>();
    c.displayName = j.at("displayName").get<std::string>();
    c.baseURL = j.at("baseURL").get<std::string>();
    c.modelName = j.at("modelName").get<std::string>();
    c.hasAPIKey = getOptionalOr<bool>(j, "hasAPIKey", false);
}

}  // namespace tf::core
