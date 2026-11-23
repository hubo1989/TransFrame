#pragma once
// OCREngineType.h — OCR engine choices. On macOS `vision` is the local default;
// Windows maps that platform slot to `windows` (Windows.Media.Ocr, PRD §6.12).
// PaddleOCR stays `paddleocr` across both platforms.

#include <string>

namespace tf::core {

enum class OCREngineType : int32_t { Windows, PaddleOCR, Invalid = -1 };
inline std::string toRaw(OCREngineType e) {
    switch (e) {
        case OCREngineType::Windows: return "windows";
        case OCREngineType::PaddleOCR: return "paddleocr";
        default: return "";
    }
}
inline OCREngineType ocrEngineFromRaw(const std::string& raw) {
    if (raw == "windows") { return OCREngineType::Windows; }
    if (raw == "paddleocr") { return OCREngineType::PaddleOCR; }
    // Cross-platform migration: Vision translates to Windows.Media.Ocr.
    if (raw == "vision") { return OCREngineType::Windows; }
    return OCREngineType::Invalid;
}

}  // namespace tf::core
