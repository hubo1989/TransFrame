#pragma once
// ExportFormat.h — PNG/JPEG/HEIC (macOS ExportFormat.swift).
// HEIC availability depends on WIC/installed HEVC support; this model only
// carries the format value and estimates. Feature-layer probing is task B.

#include <string>

namespace tf::core {

enum class ExportFormat : int32_t { Png, Jpeg, Heic, Invalid = -1 };
inline std::string toRaw(ExportFormat f) {
    switch (f) {
        case ExportFormat::Png: return "png";
        case ExportFormat::Jpeg: return "jpeg";
        case ExportFormat::Heic: return "heic";
        default: return "";
    }
}
inline ExportFormat exportFormatFromRaw(const std::string& raw) {
    if (raw == "png") { return ExportFormat::Png; }
    if (raw == "jpeg") { return ExportFormat::Jpeg; }
    if (raw == "heic") { return ExportFormat::Heic; }
    return ExportFormat::Invalid;
}
inline const char* mimeType(ExportFormat f) {
    switch (f) {
        case ExportFormat::Png: return "image/png";
        case ExportFormat::Jpeg: return "image/jpeg";
        case ExportFormat::Heic: return "image/heic";
        default: return "application/octet-stream";
    }
}
inline double estimatedBytesPerPixel(ExportFormat f) {
    switch (f) {
        case ExportFormat::Png: return 1.5;
        case ExportFormat::Jpeg: return 0.3;
        case ExportFormat::Heic: return 0.2;
        default: return 0.0;
    }
}
inline bool supportsQuality(ExportFormat f) { return f == ExportFormat::Jpeg || f == ExportFormat::Heic; }

}  // namespace tf::core
