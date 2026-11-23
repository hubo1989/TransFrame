#pragma once
// tf/Onboarding.hpp — first-run onboarding pure logic (FR-39).
//
// The Win32 wizard window (NativeShell src/OnboardingWindow.cpp) owns the
// chrome; everything testable lives here:
//   step 1  hotkey intro   — resolveHotkeys(): settings.json overrides on top
//                            of the README defaults (Ctrl+Alt+F/A/T/D/I)
//   step 2  privacy        — parseEnabledEngines() + engineSendsToCloud():
//                            which enabled engines send text/images off-box
//                            (local hosts and mtran/ollama rows never do)
//   step 3  default chain  — OnboardingChainChoice -> mergeOnboardingChain():
//                            engines.json fragment (engines row + globalChain)
//   done    marker         — serializeOnboardingDoneFlag()/parseOnboardingDone()
//                            for settings.json ("onboardingDone": true)
//   flow    state machine  — OnboardingFlow (step advance/retreat/finish/skip)
// Platform-clean (std + nlohmann), mirrors the other Core stores.
#ifndef TF_ONBOARDING_HPP
#define TF_ONBOARDING_HPP

#include "tf/HotkeySettingsStore.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tf::core {

/// Step-3 card choice (PRD FR-39: MTranServer 本地 / Ollama / 云引擎).
enum class OnboardingChainChoice : std::uint8_t {
    MtranLocal = 0,  // builtin "mtran", http://localhost:8989
    OllamaLocal = 1, // builtin "ollama", http://localhost:11434
    CloudOpenAI = 2, // builtin "openai" (API key configured later in settings)
};

/// Static metadata for a choice card (engine id / endpoint / local flag).
struct OnboardingChoiceInfo {
    const char* engineId = "";
    const char* baseURL = "";   // empty = engine default endpoint
    bool local = true;          // privacy statement: 不出网
};
OnboardingChoiceInfo onboardingChoiceInfo(OnboardingChainChoice choice);

/// Five hotkey rows for step 1: the README defaults with settings.json
/// overrides applied (parseHotkeyBindings; missing rows keep defaults).
std::vector<HotkeyBinding> resolveOnboardingHotkeys(const std::string& settingsJsonUtf8);
/// Default bindings (README Ctrl+Alt+F/A/T/D/I — same as the shortcuts page).
std::vector<HotkeyBinding> defaultOnboardingHotkeys();

/// One enabled engine row from engines.json (step 2 input).
struct OnboardingEngineRow {
    std::string engineId;   // "mtran" / "openai" / ...
    std::string baseURL;    // may be empty (engine default endpoint)
    bool enabled = false;
};
/// Parse the `engines` array out of engines.json (tolerant: malformed
/// documents yield an empty list; unknown fields ignored).
std::vector<OnboardingEngineRow> parseOnboardingEngines(const std::string& enginesJsonUtf8);

/// Privacy classification: does using this engine send text/images to the
/// cloud? Local endpoints (localhost / 127.x / [::1]) and the local-first
/// builtins (mtran, ollama) never do; every other host does.
bool onboardingEngineSendsToCloud(const OnboardingEngineRow& row);

/// Host portion of a URL for display ("http://a.b/x" -> "a.b"); empty when
/// the URL has no parseable authority.
std::string onboardingUrlHost(const std::string& url);

/// Merge the step-3 choice into existing engines.json text:
///   - `engines` gains (or has updated) the chosen engine's row, enabled
///   - `globalChain` becomes [{"kind":"builtin","engine":<id>}]
///   - every other key survives verbatim (unknown keys belong to other pages)
std::string mergeOnboardingChain(const std::string& existingEnginesJsonUtf8,
                                 OnboardingChainChoice choice);

/// settings.json completion marker: merge {"onboardingDone": true} keeping
/// every other key.
std::string serializeOnboardingDoneFlag(const std::string& existingSettingsJsonUtf8);
/// True when settings.json carries "onboardingDone": true (absent/false/any
/// other type -> false; malformed JSON -> false, never throws).
bool parseOnboardingDone(const std::string& settingsJsonUtf8);

/// Wizard state machine (smoke + window share it).
struct OnboardingFlow {
    static constexpr int kStepCount = 3;  // hotkeys -> privacy -> chain

    int step = 0;
    std::optional<OnboardingChainChoice> choice;

    bool canBack() const { return step > 0; }
    /// Continue is allowed on step 2 only with a card selected.
    bool canContinue() const { return step < kStepCount - 1 || choice.has_value(); }
    void back() { if (canBack()) --step; }
    void next() { if (canContinue()) ++step; }
    /// Completion payload: the selected card, or nullopt when the user
    /// skipped (step 2 without a choice closes without writing engines.json —
    /// the completion marker is still written by the caller).
    std::optional<OnboardingChainChoice> finish() const {
        return step == kStepCount - 1 ? choice : std::nullopt;
    }
};

}  // namespace tf::core

#endif  // TF_ONBOARDING_HPP
