// tf/OnboardingWindow.hpp — first-run wizard window (FR-39).
//
// Three steps (PRD 阶段5b): hotkey intro -> privacy/network statement ->
// default translation chain. Pure logic (state machine, privacy rows, the
// engines.json fragment) lives in Core tf/Onboarding.hpp; this window is
// Win32 chrome only. The host passes the current settings.json/engines.json
// texts in and decides what to persist from the outcome.
#ifndef TF_ONBOARDING_WINDOW_HPP
#define TF_ONBOARDING_WINDOW_HPP

#include "tf/Onboarding.hpp"

#include <optional>
#include <string>

namespace tf {

struct OnboardingOutcome {
    bool shown = false;       // false: window creation failed (host treats as skip)
    bool finished = false;    // reached via Finish, Skip or window close
    /// Selected chain card; nullopt when skipped/closed (marker only).
    std::optional<core::OnboardingChainChoice> choice;
};

/// Show the modal wizard. Blocks in its own message pump until the user
/// finishes, skips or closes the window.
OnboardingOutcome ShowOnboardingWizard(void* owner, const std::string& settingsJsonUtf8,
                                       const std::string& enginesJsonUtf8);

}  // namespace tf

#endif  // TF_ONBOARDING_WINDOW_HPP
