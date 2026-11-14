// SmartRedactionFlow.hpp — FR-10 智能打码 worker flow (App layer).
// Owns everything between the preview 智能打码 button and the main-thread
// mosaic batch: VLM credential/config snapshot, the OpenAI-compatible vision
// call, SmartRedactionEngine invocation and region → mosaic conversion.
// AppMain keeps only the wiring (one worker spawn + one WndProc case).
#ifndef TF_APP_SMART_REDACTION_FLOW_HPP
#define TF_APP_SMART_REDACTION_FLOW_HPP

#include "Annotation.h"  // tf::core::Annotation (mosaic batch)
#include "tf/CoreTypes.hpp"   // PixelBuffer
#include "tf/HttpTransport.hpp"
#include "tf/SmartRedaction.hpp"
#include "tf/EngineConfigStore.hpp"  // tf::core::VlmConfig

#include <windows.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tf::app {

/// Worker-thread inputs, snapshotted by the caller on the main thread so the
/// flow never touches app globals concurrently.
struct SmartRedactContext {
    std::shared_ptr<IHttpTransport> transport;
    CredentialStorePtr credentials;
    core::VlmConfig vlm;  // engines.json VLM section (provider/baseURL/model)
};

/// Worker -> main hop. `batch` carries the mosaics for ONE undo transaction
/// (AnnotationDocument.addAll — Ctrl+Z clears the whole batch). Failures keep
/// the classified ProviderError so the caller reuses its existing notice
/// styling; ErrorCategory::Configuration routes to the engine settings page
/// (FR-10 UX contract).
struct SmartRedactResult {
    bool ok = false;
    std::optional<ProviderError> error;  // !ok && !noImage -> notice source
    bool noImage = false;                // !ok && !error -> "no image" notice
    std::vector<core::Annotation> batch;
};

/// Detects sensitive regions and posts `doneMsg` to `notify` with a
/// `new SmartRedactResult*` as LPARAM (the receiving WndProc deletes it).
/// Blocks (HTTP + VLM); run on a worker thread. WinRT-apartment safe.
void runSmartRedaction(HWND notify, UINT doneMsg, SmartRedactContext context,
                       std::shared_ptr<PixelBuffer> pixels);

}  // namespace tf::app

#endif  // TF_APP_SMART_REDACTION_FLOW_HPP
