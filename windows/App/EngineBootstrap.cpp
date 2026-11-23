// EngineBootstrap.cpp — engines.json parsing + SchedulerDependencies wiring.
// Platform-clean (no Win32/WinRT): compiled into the Catch2 test runner on
// any OS; AppMain (Win32 TU) does the %APPDATA% file IO and hands the JSON
// text over. See EngineBootstrap.hpp for the schema.
#include "EngineBootstrap.hpp"

#include "tf/CompatibleChatProvider.hpp"
#include "tf/MTranServerProvider.hpp"

#include <stdexcept>
#include <utility>

namespace tf::app {

namespace {

/// One chain entry: {"kind":"builtin","engine":"mtran"} or
/// {"kind":"compatible","id":"<uuid>"}; the macOS V2 encoding
/// {"id":"...","kind":{"builtin":{"_0":"mtran"}}} is accepted too so a
/// settings export stays readable.
EngineChainItem parseChainItem(const Json& j) {
    if (!j.is_object()) throw std::runtime_error("chain entry must be an object");
    const auto kind = j.find("kind");
    if (kind == j.end()) throw std::runtime_error("chain entry missing kind");

    if (kind->is_string()) {
        const std::string k = kind->get<std::string>();
        if (k == "builtin") {
            const auto engine = j.find("engine");
            if (engine == j.end() || !engine->is_string()) {
                throw std::runtime_error("builtin chain entry missing engine");
            }
            const auto t = translationEngineFromRaw(engine->get<std::string>());
            if (!t) throw std::runtime_error("unknown engine id: " + engine->get<std::string>());
            return EngineChainItem::makeBuiltin(*t);
        }
        if (k == "compatible") {
            const auto id = j.find("id");
            if (id == j.end() || !id->is_string() ||
                !Uuid::parse(id->get<std::string>()).has_value()) {
                throw std::runtime_error("compatible chain entry needs a valid UUID id");
            }
            return EngineChainItem::makeCompatible(id->get<std::string>());
        }
        throw std::runtime_error("chain kind must be builtin|compatible");
    }

    if (kind->is_object()) {  // V2 encoding
        if (const auto b = kind->find("builtin"); b != kind->end() && b->is_object()) {
            const auto v = b->find("_0");
            if (v != b->end() && v->is_string()) {
                if (const auto t = translationEngineFromRaw(v->get<std::string>())) {
                    return EngineChainItem::makeBuiltin(*t);
                }
                throw std::runtime_error("unknown V2 builtin engine: " + v->get<std::string>());
            }
        }
        if (const auto c = kind->find("compatible"); c != kind->end() && c->is_object()) {
            const auto v = c->find("_0");
            if (v != c->end() && v->is_string() &&
                Uuid::parse(v->get<std::string>()).has_value()) {
                return EngineChainItem::makeCompatible(v->get<std::string>());
            }
        }
    }
    throw std::runtime_error("unsupported chain entry encoding");
}

std::vector<EngineChainItem> parseChainArray(const Json& root, const char* key) {
    const auto it = root.find(key);
    if (it == root.end() || it->is_null()) return {};
    if (!it->is_array()) throw std::runtime_error(std::string(key) + " must be an array");
    std::vector<EngineChainItem> out;
    out.reserve(it->size());
    for (const auto& e : *it) out.push_back(parseChainItem(e));
    return out;
}

const Json& requireString(const Json& obj, const char* key, const std::string& what) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) {
        throw std::runtime_error(what + " needs a string field '" + key + "'");
    }
    return *it;
}

}  // namespace

EngineBootstrapConfig parseEngineConfig(const std::string& jsonText) {
    Json root;
    try {
        root = Json::parse(jsonText);  // throws nlohmann parse_error on malformed JSON
    } catch (const Json::exception& e) {
        // Normalize to runtime_error: one exception type for every config
        // defect, matching the app-side catch(std::exception) fallback.
        throw std::runtime_error(std::string("engines.json parse error: ") + e.what());
    }
    if (!root.is_object()) throw std::runtime_error("engines.json root must be an object");

    EngineBootstrapConfig cfg;

    // ---- builtin engines (FR-18) -----------------------------------------
    if (const auto it = root.find("engines"); it != root.end() && !it->is_null()) {
        if (!it->is_array()) throw std::runtime_error("engines must be an array");
        std::map<TranslationEngineType, EngineEntry> byEngine;  // later rows win
        for (const auto& e : *it) {
            if (!e.is_object()) throw std::runtime_error("engine entry must be an object");
            const auto& rawId = requireString(e, "engine", "engine entry");
            const auto t = translationEngineFromRaw(rawId.get<std::string>());
            if (!t) throw std::runtime_error("unknown engine id: " + rawId.get<std::string>());

            EngineEntry entry;
            entry.engine = *t;
            entry.config = EngineConfig::make(*t);
            entry.enabled = true;  // listing a row means "use it" unless disabled
            entry.config.isEnabled = true;
            if (const auto v = e.find("enabled"); v != e.end() && !v->is_null()) {
                if (!v->is_boolean()) throw std::runtime_error("enabled must be a boolean");
                entry.enabled = v->get<bool>();
                entry.config.isEnabled = entry.enabled;
            }
            if (const auto v = e.find("baseURL"); v != e.end() && !v->is_null()) {
                if (!v->is_string()) throw std::runtime_error("baseURL must be a string");
                entry.config.baseURL = v->get<std::string>();
            }
            if (const auto v = e.find("modelName"); v != e.end() && !v->is_null()) {
                if (!v->is_string()) throw std::runtime_error("modelName must be a string");
                entry.config.modelName = v->get<std::string>();
            }
            if (const auto v = e.find("timeout"); v != e.end() && !v->is_null()) {
                if (!v->is_number()) throw std::runtime_error("timeout must be a number");
                entry.config.timeoutSeconds = v->get<double>();
            }
            if (const auto v = e.find("maxTokens"); v != e.end() && !v->is_null()) {
                if (!v->is_number_integer()) throw std::runtime_error("maxTokens must be an integer");
                entry.config.maxTokens = v->get<std::int64_t>();
            }
            if (const auto v = e.find("temperature"); v != e.end() && !v->is_null()) {
                if (!v->is_number()) throw std::runtime_error("temperature must be a number");
                entry.config.temperature = v->get<double>();
            }
            if (const auto v = e.find("customHeaders"); v != e.end() && !v->is_null()) {
                if (!v->is_object()) throw std::runtime_error("customHeaders must be an object");
                for (auto h = v->begin(); h != v->end(); ++h) {
                    if (!h.value().is_string()) {
                        throw std::runtime_error("customHeaders values must be strings");
                    }
                    entry.config.customHeaders[h.key()] = h.value().get<std::string>();
                }
            }
            byEngine[*t] = std::move(entry);
        }
        for (auto& [t, entry] : byEngine) {
            (void)t;
            cfg.engines.push_back(std::move(entry));
        }
    }

    // ---- compatible instances (FR-19) ------------------------------------
    if (const auto it = root.find("compatibleInstances"); it != root.end() && !it->is_null()) {
        if (!it->is_array()) throw std::runtime_error("compatibleInstances must be an array");
        std::map<std::string, CompatibleConfig> byId;  // duplicate ids: later wins
        for (const auto& e : *it) {
            if (!e.is_object()) throw std::runtime_error("compatibleInstance must be an object");
            const auto& id = requireString(e, "id", "compatibleInstance");
            if (!Uuid::parse(id.get<std::string>()).has_value()) {
                throw std::runtime_error("compatibleInstance id must be a UUID: " + id.get<std::string>());
            }
            CompatibleConfig c;
            c.id = id.get<std::string>();
            c.displayName = "Custom";
            if (const auto v = e.find("displayName"); v != e.end() && !v->is_null()) {
                if (!v->is_string()) throw std::runtime_error("displayName must be a string");
                c.displayName = v->get<std::string>();
            }
            c.baseURL = requireString(e, "baseURL", "compatibleInstance " + c.id).get<std::string>();
            if (const auto v = e.find("modelName"); v != e.end() && !v->is_null()) {
                if (!v->is_string()) throw std::runtime_error("modelName must be a string");
                c.modelName = v->get<std::string>();
            }
            c.hasAPIKey = true;  // keyless requires an explicit opt-out
            if (const auto v = e.find("hasAPIKey"); v != e.end() && !v->is_null()) {
                if (!v->is_boolean()) throw std::runtime_error("hasAPIKey must be a boolean");
                c.hasAPIKey = v->get<bool>();
            }
            byId[c.id] = std::move(c);
        }
        for (auto& [id, c] : byId) {
            (void)id;
            cfg.compatibleInstances.push_back(std::move(c));
        }
    }

    // ---- global selection (FR-20/21) -------------------------------------
    cfg.globalChain = parseChainArray(root, "globalChain");
    cfg.globalCompareSet = parseChainArray(root, "globalCompareSet");

    // ---- scene bindings (FR-23) ------------------------------------------
    if (const auto it = root.find("sceneBindings"); it != root.end() && !it->is_null()) {
        if (!it->is_object()) throw std::runtime_error("sceneBindings must be an object");
        for (auto sceneIt = it->begin(); sceneIt != it->end(); ++sceneIt) {
            const auto scene = translationSceneFromRaw(sceneIt.key());
            if (!scene) throw std::runtime_error("unknown scene key: " + sceneIt.key());
            const Json& b = sceneIt.value();
            if (!b.is_object()) throw std::runtime_error("scene binding must be an object");
            SceneRoute r;
            r.scene = *scene;
            std::string mode = "ordered";
            if (const auto m = b.find("mode"); m != b.end() && !m->is_null()) {
                if (!m->is_string()) throw std::runtime_error("mode must be a string");
                mode = m->get<std::string>();
            }
            if (mode != "ordered" && mode != "compare") {
                throw std::runtime_error("scene binding mode must be ordered|compare");
            }
            r.useCompare = (mode == "compare");
            r.items = parseChainArray(b, "engines");
            cfg.sceneRoutes.push_back(std::move(r));
        }
    }

    // ---- prompt overrides (FR-24) ----------------------------------------
    if (const auto it = root.find("prompts"); it != root.end() && !it->is_null()) {
        if (!it->is_object()) throw std::runtime_error("prompts must be an object");
        if (const auto p = it->find("enginePrompts"); p != it->end() && !p->is_null()) {
            if (!p->is_object()) throw std::runtime_error("prompts.enginePrompts must be an object");
            for (auto e = p->begin(); e != p->end(); ++e) {
                const auto t = translationEngineFromRaw(e.key());
                if (!t || !e.value().is_string()) {
                    throw std::runtime_error("bad prompts.enginePrompts key: " + e.key());
                }
                cfg.prompts.enginePrompts.emplace_back(*t, e.value().get<std::string>());
            }
        }
        if (const auto p = it->find("compatibleEnginePrompts"); p != it->end() && !p->is_null()) {
            if (!p->is_object()) {
                throw std::runtime_error("prompts.compatibleEnginePrompts must be an object");
            }
            for (auto e = p->begin(); e != p->end(); ++e) {
                if (!Uuid::parse(e.key()).has_value() || !e.value().is_string()) {
                    throw std::runtime_error("bad prompts.compatibleEnginePrompts key (need UUID)");
                }
                cfg.prompts.compatibleEnginePrompts.emplace_back(e.key(), e.value().get<std::string>());
            }
        }
        if (const auto p = it->find("scenePrompts"); p != it->end() && !p->is_null()) {
            if (!p->is_object()) throw std::runtime_error("prompts.scenePrompts must be an object");
            for (auto e = p->begin(); e != p->end(); ++e) {
                const auto s = translationSceneFromRaw(e.key());
                if (!s || !e.value().is_string()) {
                    throw std::runtime_error("bad prompts.scenePrompts key: " + e.key());
                }
                cfg.prompts.scenePrompts.emplace_back(*s, e.value().get<std::string>());
            }
        }
    }

    return cfg;
}

EngineBootstrapConfig defaultEngineConfig() {
    EngineBootstrapConfig cfg;
    EngineEntry mtran;
    mtran.engine = TranslationEngineType::MtranServer;
    mtran.enabled = true;
    mtran.config = EngineConfig::make(TranslationEngineType::MtranServer);
    mtran.config.isEnabled = true;
    cfg.engines.push_back(std::move(mtran));
    cfg.globalChain.push_back(EngineChainItem::makeBuiltin(TranslationEngineType::MtranServer));
    return cfg;
}

// ---------------------------------------------------------------------------
// EngineBootstrap
// ---------------------------------------------------------------------------
EngineBootstrap::EngineBootstrap(EngineBootstrapConfig config, CredentialStorePtr credentials,
                                 HttpTransportPtr transport)
    : config_(std::move(config)),
      credentials_(std::move(credentials)),
      transport_(std::move(transport)) {
    // Built eagerly (not lazily): populate()'s closures are read from worker
    // threads, so any lazy init here would race the settings page.
    auto probe = std::make_shared<DefaultEngineProbe>(
        credentials_, transport_, [this] { return compatibleConfigs(); });
    health_ = std::make_shared<EngineHealthChecker>(std::move(probe));
}

std::optional<EngineConfig> EngineBootstrap::engineConfig(TranslationEngineType engine) const {
    for (const auto& e : config_.engines) {
        if (e.engine != engine || !e.enabled) continue;
        EngineConfig cfg = e.config;
        if (engine == TranslationEngineType::MtranServer && !config_.mtranUrlOverride.empty()) {
            cfg.baseURL = config_.mtranUrlOverride;  // debug env wins (v1 behavior)
        }
        cfg.isEnabled = true;
        return cfg;
    }
    return std::nullopt;
}

std::vector<CompatibleConfig> EngineBootstrap::compatibleConfigs() const {
    return config_.compatibleInstances;
}

std::optional<CompatibleConfig> EngineBootstrap::compatibleById(const std::string& uuid) const {
    for (const auto& c : config_.compatibleInstances) {
        if (c.id == uuid) return c;
    }
    return std::nullopt;
}

void EngineBootstrap::populate(SchedulerDependencies& deps) const {
    deps.builtinProvider = [this](TranslationEngineType t) -> TranslationProviderPtr {
        const auto cfg = engineConfig(t);
        if (!cfg) return nullptr;  // absent or disabled → chain falls through
        return makeBuiltinProvider(t, *cfg, credentials_, transport_);
    };
    deps.compatibleProvider = [this](const std::string& uuid) -> TranslationProviderPtr {
        const auto cfg = compatibleById(uuid);
        if (!cfg) return nullptr;  // deleted instance
        EngineConfig ec = EngineConfig::make(TranslationEngineType::Custom);
        ec.isEnabled = true;
        if (!cfg->baseURL.empty()) ec.baseURL = cfg->baseURL;
        if (!cfg->modelName.empty()) ec.modelName = cfg->modelName;
        return std::make_unique<CompatibleChatProvider>(*cfg, ec, credentials_, transport_);
    };
    deps.compatibleConfigs = [this] { return compatibleConfigs(); };
    deps.promptResolver = [this](TranslationEngineType t, TranslationScene s,
                                 const std::string& compatibleId) {
        return config_.prompts.resolve(t, s, compatibleId);
    };
}

SceneRoute EngineBootstrap::routeForScene(TranslationScene scene) const {
    for (const auto& r : config_.sceneRoutes) {
        // Empty bound item lists behave as unbound (an empty chain can never
        // produce a translation).
        if (r.scene == scene && !r.items.empty()) return r;
    }
    SceneRoute fallback;
    fallback.scene = scene;
    fallback.items = config_.globalChain;
    if (fallback.items.empty()) {
        // Engines.json absent/global chain cleared → the v1 MTranServer chain.
        fallback.items.push_back(EngineChainItem::makeBuiltin(TranslationEngineType::MtranServer));
    }
    return fallback;
}

EngineHealth EngineBootstrap::healthOf(TranslationEngineType engine,
                                       const CancellationToken& token) const {
    return health_->health(EngineHealthKey::forBuiltin(engine), token);
}

EngineHealth EngineBootstrap::healthOfCompatible(const std::string& instanceUuid,
                                                 const CancellationToken& token) const {
    return health_->health(EngineHealthKey::forCompatible(instanceUuid), token);
}

}  // namespace tf::app
