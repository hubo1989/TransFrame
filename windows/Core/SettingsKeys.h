#pragma once
// SettingsKeys.h — every UserDefaults key used by the app, one-to-one with
// the Swift `AppSettings.Keys` enum (TransFrame/Models/AppSettings.swift).
// Windows persists to the same key names (ApplicationData settings container
// or registry under the same "TransFrame." prefix) so a settings export from
// macOS restores verbatim on Windows.
//
// Keys holding credentials (vlmAPIKey, paddleOCRCloudAPIKey) are stored in
// Credential Manager on Windows (Keychain on macOS); they are listed here
// for completeness but never written to the plaintext store.

#include <string>

namespace tf::core {
namespace SettingsKeys {

inline constexpr const char* kPrefix = "TransFrame.";

// MARK: Capture / export
inline constexpr const char* saveLocation = "TransFrame.saveLocation";
inline constexpr const char* defaultFormat = "TransFrame.defaultFormat";
inline constexpr const char* jpegQuality = "TransFrame.jpegQuality";
inline constexpr const char* heicQuality = "TransFrame.heicQuality";

// MARK: Shortcuts (keyCode/modifiers dictionaries on macOS)
inline constexpr const char* fullScreenShortcut = "TransFrame.fullScreenShortcut";
inline constexpr const char* selectionShortcut = "TransFrame.selectionShortcut";
inline constexpr const char* translationModeShortcut = "TransFrame.translationModeShortcut";
inline constexpr const char* textSelectionTranslationShortcut =
    "TransFrame.textSelectionTranslationShortcut";
inline constexpr const char* translateAndInsertShortcut = "TransFrame.translateAndInsertShortcut";

// MARK: Annotation defaults
inline constexpr const char* strokeColor = "TransFrame.strokeColor";
inline constexpr const char* strokeWidth = "TransFrame.strokeWidth";
inline constexpr const char* textSize = "TransFrame.textSize";
inline constexpr const char* rectangleFilled = "TransFrame.rectangleFilled";
inline constexpr const char* ellipseFilled = "TransFrame.ellipseFilled";
inline constexpr const char* shapeFillColor = "TransFrame.shapeFillColor";
inline constexpr const char* mosaicBlockSize = "TransFrame.mosaicBlockSize";

// MARK: Translation languages
inline constexpr const char* translationTargetLanguage = "TransFrame.translationTargetLanguage";
inline constexpr const char* translationSourceLanguage = "TransFrame.translationSourceLanguage";
inline constexpr const char* translationAutoDetect = "TransFrame.translationAutoDetect";

// MARK: Engines / modes
inline constexpr const char* ocrEngine = "TransFrame.ocrEngine";
inline constexpr const char* translationEngine = "TransFrame.translationEngine";
inline constexpr const char* translationMode = "TransFrame.translationMode";

// MARK: Onboarding
inline constexpr const char* onboardingCompleted = "TransFrame.onboardingCompleted";
inline constexpr const char* userSkippedPermissions = "TransFrame.userSkippedPermissions";

// MARK: PaddleOCR (local server)
inline constexpr const char* paddleOCRServerAddress = "TransFrame.paddleOCRServerAddress";

// MARK: MTranServer
inline constexpr const char* mtranServerHost = "TransFrame.mtranServerHost";
inline constexpr const char* mtranServerPort = "TransFrame.mtranServerPort";

// MARK: VLM configuration
inline constexpr const char* vlmProvider = "TransFrame.vlmProvider";
inline constexpr const char* vlmAPIKey = "TransFrame.vlmAPIKey";  // credential store
inline constexpr const char* vlmBaseURL = "TransFrame.vlmBaseURL";
inline constexpr const char* vlmModelName = "TransFrame.vlmModelName";
inline constexpr const char* vlmOpenAIMode = "TransFrame.vlmOpenAIMode";
/// Per-provider suffix keys: "TransFrame.vlmBaseURL.<providerRaw>" /
/// "TransFrame.vlmModelName.<providerRaw>" (Swift Keys.vlmBaseURL(for:)).
inline std::string vlmBaseURLFor(const std::string& providerRaw) {
    return std::string(kPrefix) + "vlmBaseURL." + providerRaw;
}
inline std::string vlmModelNameFor(const std::string& providerRaw) {
    return std::string(kPrefix) + "vlmModelName." + providerRaw;
}

// MARK: Translation workflow
inline constexpr const char* preferredTranslationEngine = "TransFrame.preferredTranslationEngine";
inline constexpr const char* mtranServerURL = "TransFrame.mtranServerURL";
inline constexpr const char* translationFallbackEnabled = "TransFrame.translationFallbackEnabled";

// MARK: Translate-and-insert languages
inline constexpr const char* translateAndInsertSourceLanguage =
    "TransFrame.translateAndInsertSourceLanguage";
inline constexpr const char* translateAndInsertTargetLanguage =
    "TransFrame.translateAndInsertTargetLanguage";

// MARK: Multi-engine (legacy migration sources)
inline constexpr const char* engineSelectionMode = "TransFrame.engineSelectionMode";
inline constexpr const char* engineConfigs = "TransFrame.engineConfigs";
inline constexpr const char* promptConfig = "TransFrame.promptConfig";
inline constexpr const char* sceneBindings = "TransFrame.sceneBindings";
inline constexpr const char* parallelEngines = "TransFrame.parallelEngines";
inline constexpr const char* compatibleProviderConfigs = "TransFrame.compatibleProviderConfigs";

// MARK: Engine selection V2
inline constexpr const char* engineModeV2 = "TransFrame.engineModeV2";
inline constexpr const char* engineChainV2 = "TransFrame.engineChainV2";
inline constexpr const char* compareEnginesV2 = "TransFrame.compareEnginesV2";

// MARK: PaddleOCR mode / cloud
inline constexpr const char* paddleOCRMode = "TransFrame.paddleOCRMode";
inline constexpr const char* paddleOCRUseCloud = "TransFrame.paddleOCRUseCloud";
inline constexpr const char* paddleOCRCloudBaseURL = "TransFrame.paddleOCRCloudBaseURL";
inline constexpr const char* paddleOCRCloudAPIKey = "TransFrame.paddleOCRCloudAPIKey";  // credential store
inline constexpr const char* paddleOCRCloudModelId = "TransFrame.paddleOCRCloudModelId";
inline constexpr const char* paddleOCRLocalVLModelDir = "TransFrame.paddleOCRLocalVLModelDir";

// MARK: History (HistoryStore.defaultHistoryKey)
inline constexpr const char* translationHistory = "TransFrame.translationHistory";

}  // namespace SettingsKeys
}  // namespace tf::core
