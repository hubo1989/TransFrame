// tf/ImageExporter.hpp — WIC encoding + clipboard copy for the preview
// window (PRD FR-12). Win32-only; the HEIC probe mirrors the macOS model:
// PNG/JPEG always available, HEIC only when the system ships a WIC HEIF
// encoder (Win11 needs the HEVC extensions — UI falls back with a notice).
//
// Compositing renders the base pixels + annotation list into a fresh BGRA
// buffer via a D2D WIC render target, so save and copy always include the
// annotations exactly as drawn on the canvas.
#ifndef TF_IMAGE_EXPORTER_HPP
#define TF_IMAGE_EXPORTER_HPP

#include "tf/CoreTypes.hpp"
#include "tf/OverlayRenderer.hpp"

#include "AnnotationDocument.h"

#include <d2d1.h>

#include <string>
#include <vector>

namespace tf {

/// Draw the annotation list onto `rt` in image pixel coordinates (1 unit =
/// 1 image pixel; callers apply zoom/pan via SetTransform first). Shared by
/// the preview canvas and the composited export path so what you see is
/// what you save. Uses the rt's current antialias mode.
void drawAnnotations(ID2D1RenderTarget& rt,
                     const std::vector<tf::core::Annotation>& annotations);

/// FR-26: composite translation overlay segments onto the base pixels via an
/// offscreen D2D (WIC-backed) render pass, producing a new BGRA buffer with
/// identical dimensions — the strategy decisions (background sampling,
/// adaptive colors, font fit, truncation) come from the portable, tested
/// planOverlayItem(). ReplaceOriginal paints inside each bbox; AppendBelow
/// adds a band under each line. Returns an invalid buffer on D2D/WIC failure
/// (callers keep the un-composited base).
PixelBuffer renderTranslationOverlay(const PixelBuffer& base,
                                     const std::vector<OverlaySegment>& segments,
                                     OverlayTextMode mode);

/// True when the system WIC catalog exposes a HEIF container encoder.
/// Probed per call (cheap COM scan); call sites may cache.
bool heicEncoderAvailable();

/// Encode `pixels` to `path` via WIC. `qualityPercent` (0..100) applies to
/// lossy formats only (JPEG/HEIC). On failure `error` carries a readable
/// reason (HEIC without the HEVC extension lands here).
bool encodeToFile(const PixelBuffer& pixels, const std::wstring& path,
                  ExportFormat format, int qualityPercent, std::wstring& error);

/// Render base pixels + annotations into a new BGRA PixelBuffer (1:1 image
/// pixel coordinates). Returns an invalid buffer on D2D/WIC failure.
PixelBuffer renderComposited(const PixelBuffer& base,
                             const tf::core::AnnotationDocument& document);

/// Put a BGRA PixelBuffer on the clipboard as CF_DIB (top-down height).
/// Opens/closes the clipboard around `owner`.
bool copyBitmapToClipboard(HWND owner, const PixelBuffer& pixels);

/// FR-31: in-memory PNG encode (image-host upload payload; same WIC pixel
/// path as encodeToFile so an upload matches a save of the same buffer).
/// Returns an empty vector on failure.
std::vector<std::uint8_t> encodePngToMemory(const PixelBuffer& pixels);

}  // namespace tf

#endif  // TF_IMAGE_EXPORTER_HPP
