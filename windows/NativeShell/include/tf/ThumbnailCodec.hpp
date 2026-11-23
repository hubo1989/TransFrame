// tf/ThumbnailCodec.hpp — WIC memory-codec helpers for history thumbnails
// (PRD FR-30) and recent-shot playback (FR-34). Win32-only.
//
//   encodeJpegThumbnail  BGRA pixels -> downscaled in-memory JPEG (the
//                       thumbnailData blob HistoryStore persists as base64)
//   decodeImageMemory   JPEG/PNG bytes -> BGRA PixelBuffer (preview window
//                       opens a stored screenshot through this)
//   decodeImageScaled   decode + downscale in one pass (48px list icons)
#ifndef TF_THUMBNAIL_CODEC_HPP
#define TF_THUMBNAIL_CODEC_HPP

#include "tf/CoreTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tf {

/// Encode `pixels` as JPEG in memory, downscaled so the longest edge is at
/// most `maxEdge` (aspect preserved; no upscale). Caller must have COM
/// initialized (CoInitializeEx). Empty vector on failure.
std::vector<std::uint8_t> encodeJpegThumbnail(const PixelBuffer& pixels, int maxEdge,
                                              int qualityPercent);

/// Decode an encoded image (JPEG/PNG/…, whatever WIC knows) into a tightly
/// strided BGRA PixelBuffer. Invalid buffer on failure.
PixelBuffer decodeImageMemory(const std::uint8_t* data, std::size_t size);

/// decodeImageMemory + downscale to `maxEdge` in one WIC pass. Invalid buffer
/// on failure.
PixelBuffer decodeImageScaled(const std::uint8_t* data, std::size_t size, int maxEdge);

}  // namespace tf

#endif  // TF_THUMBNAIL_CODEC_HPP
