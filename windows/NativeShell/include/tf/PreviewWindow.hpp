// tf/PreviewWindow.hpp — screenshot preview + annotation window (PRD FR-07).
// Win32 window with a D2D canvas: base pixels + annotation overlay, a button
// toolbar (矩形/文字/箭头/撤销/重做/保存/复制/关闭) and FR-07 shortcuts
// (Enter/Ctrl+S save, Ctrl+C copy, Esc close, R/1 T/4 A/3 tools, Ctrl+Z/Y
// undo/redo, Del delete). The 50-step undo stack and hit testing live in
// tf::core::AnnotationDocument; this controller only drives interaction.
//
// One process-wide preview window (singleton) so the ResultWindow "预览"
// button and tray actions reuse the same instance.
#ifndef TF_PREVIEW_WINDOW_HPP
#define TF_PREVIEW_WINDOW_HPP

#include "Annotation.h"  // FR-10 applySmartRedaction seam (tf::core::Annotation)
#include "tf/CoreTypes.hpp"

#include <functional>
#include <memory>
#include <string>

namespace tf {

struct PreviewWindowSpec {
    std::shared_ptr<PixelBuffer> image;  // captured pixels (BGRA8); nullptr = reject
    std::wstring suggestedFileName;      // e.g. "TransFrame_20261103_125012"
    std::wstring titleSuffix;            // optional " · 1920×1080"
};

class PreviewWindowController {
public:
    static PreviewWindowController& instance();

    /// Register the window class (once per process).
    static bool registerClass(HINSTANCE instance);

    /// Show (or refresh) the preview. Copies the spec; the shared pixel
    /// buffer is not duplicated. Async show on the caller's (main) thread.
    bool show(const PreviewWindowSpec& spec);

    /// FR-14 hook: fires when the toolbar 贴图 button is clicked. AppMain
    /// owns the PinnedWindowManager wiring; the preview stays decoupled.
    void setPinHandler(std::function<void()> handler);

    /// FR-31 hook: fires with the composited (annotated) pixels when the
    /// toolbar 上传 button is clicked. AppMain owns the upload provider.
    void setUploadHandler(std::function<void(std::shared_ptr<PixelBuffer>)> handler);

    /// FR-10 hook: fires with the preview's base pixels when the toolbar
    /// 智能打码 button is clicked. AppMain owns the VLM detection worker.
    void setSmartRedactHandler(std::function<void(std::shared_ptr<PixelBuffer>)> handler);

    /// FR-10: appends a mosaic batch as ONE undo transaction (Ctrl+Z clears
    /// the whole batch) and repaints the canvas.
    void applySmartRedaction(std::vector<core::Annotation> batch);

    void close();
    bool isVisible() const;

private:
    PreviewWindowController() = default;
    ~PreviewWindowController();
    struct Impl;
    /// Singleton keeps one Impl alive for the process lifetime (the window
    /// and its D2D device outlive any single show()).
    std::unique_ptr<Impl> impl_;
};

}  // namespace tf

#endif  // TF_PREVIEW_WINDOW_HPP
