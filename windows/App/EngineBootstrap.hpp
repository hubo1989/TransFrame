// EngineBootstrap.hpp — engine wiring for the Windows app (PRD FR-16..24/27).
// Replaces the v1 hardcoded MTranServer-only SchedulerDependencies with the
// full registry: builtin engines (9), OpenAI-compatible multi-instance
// (FR-19), ordered chain + compare set (FR-20/21), scene bindings (FR-23)
// and three-layer prompt overrides (FR-24), plus EngineHealthChecker wiring
// (FR-22) for the settings page.
//
// Platform-clean: AppMain (Win32 TU) reads %APPDATA%\TransFrame\engines.json
// and hands the JSON text to parseEngineConfig; unit tests drive this TU on
// any OS with a MockHttpTransport (brief: HTTP 层不依赖 UI 框架).
//
// engines.json schema (windows/docs/engines-config.md):
//   {
//     "version": 1,
//     "engines": [ {"engine":"mtran","enabled":true,"baseURL":"http://localhost:8989"}, ... ],
//     "compatibleInstances": [ {"id":"<uuid>","displayName":"...","baseURL":"...","modelName":"...","hasAPIKey":false} ],
//     "globalChain": [ {"kind":"builtin","engine":"mtran"}, {"kind":"compatible","id":"<uuid>"} ],
//     "globalCompareSet": [],
//     "sceneBindings": { "screenshot": {"mode":"ordered","engines":[...]}, ... },
//     "prompts": { "enginePrompts":{...}, "compatibleEnginePrompts":{...}, "scenePrompts":{...} }
//   }
#ifndef TF_APP_ENGINE_BOOTSTRAP_HPP
#define TF_APP_ENGINE_BOOTSTRAP_HPP

#include "tf/CoreModels.hpp"
#include "tf/EngineHealthChecker.hpp"
#include "tf/Json.hpp"
#include "tf/TranslationProvider.hpp"
#include "tf/TranslationScheduler.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tf::app {

/// One builtin-engine row (FR-18: 开关/端点/模型/超时). `engine` keys the
/// row; `config.isEnabled` mirrors `enabled`.
struct EngineEntry {
    TranslationEngineType engine = TranslationEngineType::MtranServer;
    bool enabled = false;
    EngineConfig config;  // config.engine == engine
};

/// Scene routing (FR-23): ordered chain or compare set; unbound scenes fall
/// back to the global chain (routeForScene).
struct SceneRoute {
    TranslationScene scene = TranslationScene::Screenshot;
    bool useCompare = false;
    std::vector<EngineChainItem> items;  // ordered chain or compare set
};

/// engines.json content + the debug overrides layered on top. Defaults are
/// produced by defaultEngineConfig(): MTranServer enabled (localhost:8989),
/// global ordered chain [mtran], no compatible instances, no scene/prompt
/// overrides — the v1 AppMain wiring, exactly.
struct EngineBootstrapConfig {
    std::vector<EngineEntry> engines;
    std::vector<CompatibleConfig> compatibleInstances;
    std::vector<EngineChainItem> globalChain;
    std::vector<EngineChainItem> globalCompareSet;
    std::vector<SceneRoute> sceneRoutes;
    TranslationPromptConfig prompts;  // FR-24: engine < compatible < scene

    /// TRANSFRAME_MTRAN_URL debug override: wins over engines.json for the
    /// mtran endpoint (empty = no override; matches the v1 env behavior).
    std::string mtranUrlOverride;
};

/// Parses engines.json text into the config model. Malformed documents,
/// unknown engine/scene keys and bad chain entries throw std::runtime_error
/// — the caller decides between falling back to defaults and surfacing the
/// error (AppMain logs via OutputDebugString and keeps defaults).
EngineBootstrapConfig parseEngineConfig(const std::string& jsonText);

/// Built-in defaults (engines.json missing): MTranServer-only chain, the v1
/// AppMain wiring.
EngineBootstrapConfig defaultEngineConfig();

/// Runtime bundle: parsed config + the provider seams (credential store,
/// transport) the SchedulerDependencies closures close over. Lifetime: the
/// app keeps this alive as long as the scheduler; the closures capture
/// `this`, so AppState declares engines before scheduler (reverse-order
/// destruction tears the scheduler down first).
class EngineBootstrap {
public:
    EngineBootstrap() = default;
    EngineBootstrap(EngineBootstrapConfig config, CredentialStorePtr credentials,
                    HttpTransportPtr transport);

    /// Fills every SchedulerDependencies slot (FR-16..19, 24):
    /// builtinProvider / compatibleProvider / compatibleConfigs / promptResolver.
    void populate(SchedulerDependencies& deps) const;

    /// FR-23: the bound route for a scene, or the global ordered chain.
    SceneRoute routeForScene(TranslationScene scene) const;

    /// FR-22 health queries for the settings page: 60s-cached four-state
    /// probe over the shared transport + credential store. The checker is
    /// created in the constructor — no lazy init races.
    std::shared_ptr<EngineHealthChecker> healthChecker() const { return health_; }
    EngineHealth healthOf(TranslationEngineType engine, const CancellationToken& token = {}) const;
    EngineHealth healthOfCompatible(const std::string& instanceUuid,
                                    const CancellationToken& token = {}) const;

    const EngineBootstrapConfig& config() const { return config_; }

    /// Effective EngineConfig for a builtin engine (enabled rows only);
    /// nullopt when the engine is absent or disabled. Applies
    /// mtranUrlOverride to the mtran row.
    std::optional<EngineConfig> engineConfig(TranslationEngineType engine) const;
    std::vector<CompatibleConfig> compatibleConfigs() const;
    std::optional<CompatibleConfig> compatibleById(const std::string& instanceUuid) const;

private:
    EngineBootstrapConfig config_;
    CredentialStorePtr credentials_;
    HttpTransportPtr transport_;
    std::shared_ptr<EngineHealthChecker> health_;
};

}  // namespace tf::app

#endif  // TF_APP_ENGINE_BOOTSTRAP_HPP
