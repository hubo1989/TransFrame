// RecentShots.cpp — capture→history recording + tray strip (see header).
// The PNG goes to disk (full fidelity, pruned); the row carries a 480px JPEG
// thumbnail so the history window renders previews without touching files.
#include "pch.h"

#include "tf/RecentShots.hpp"

#include "tf/ImageExporter.hpp"
#include "tf/ThumbnailCodec.hpp"

#include <filesystem>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>

namespace tf::shots {

namespace {

std::string toUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0,
                                        nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), n, nullptr, nullptr);
    out.resize(out.size() - 1);
    return out;
}

std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), n);
    out.resize(out.size() - 1);
    return out;
}

std::wstring clockLabel(const core::TimePoint& t) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm local{};
    if (::localtime_s(&local, &tt) != 0) return {};
    wchar_t buf[16] = {};
    ::swprintf_s(buf, L"%02d:%02d", local.tm_hour, local.tm_min);
    return buf;
}

/// Keep only the newest `keep` .png files in `dir` (best-effort).
void pruneRecentFiles(const std::wstring& dir, int keep) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(fs::path(dir), ec)) return;
    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    for (const auto& entry : fs::directory_iterator(fs::path(dir), ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != L".png") continue;
        files.emplace_back(entry.last_write_time(ec), entry.path());
        if (ec) return;
    }
    if (files.size() <= static_cast<std::size_t>(keep)) return;
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    for (std::size_t i = static_cast<std::size_t>(keep); i < files.size(); ++i) {
        fs::remove(files[i].second, ec);
    }
}

std::optional<std::vector<std::uint8_t>> readFileBytes(const std::wstring& path) {
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
}

}  // namespace

bool recordScreenshot(core::HistoryStore& store, const PixelBuffer& pixels,
                      const std::wstring& recentDir) {
    if (!pixels.valid()) return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir(recentDir);
    fs::create_directories(dir, ec);  // exists_ok

    core::TranslationHistory entry;
    entry.id = core::Uuid::randomUuid();
    const std::wstring file = (dir / (toWide(entry.id.toString()) + L".png")).wstring();
    std::wstring writeError;
    if (!encodeToFile(pixels, file, ExportFormat::Png, 100, writeError)) return false;
    pruneRecentFiles(recentDir, kKeepPngFiles);

    // macOS model: sourceText=描述, translatedText=文件路径.
    entry.sourceText = "截图 " + std::to_string(pixels.width) + "x" +
                       std::to_string(pixels.height);
    entry.translatedText = toUtf8(file);
    entry.sourceLanguage = "Screenshot";
    entry.targetLanguage = entry.translatedText;
    entry.timestamp = core::now();
    entry.category = core::HistoryCategory::Screenshot;
    entry.thumbnailData = encodeJpegThumbnail(pixels, 480, 80);
    return store.insert(entry);
}

std::vector<RecentShot> trayRecentShots(const core::HistoryStore& store) {
    const auto rows = store.recentScreenshots(5);
    std::vector<RecentShot> out;
    out.reserve(rows.size());
    for (const auto& e : rows) {
        RecentShot shot;
        shot.screenshotId = static_cast<std::uint64_t>(
            std::hash<std::string>{}(e.id.toString()));
        shot.label = L"截图 " + clockLabel(e.timestamp);
        out.push_back(std::move(shot));
    }
    return out;
}

PixelBuffer loadRecentShotPixels(const core::TranslationHistory& entry) {
    if (!entry.translatedText.empty()) {
        if (const auto bytes = readFileBytes(toWide(entry.translatedText))) {
            PixelBuffer px = decodeImageMemory(bytes->data(), bytes->size());
            if (px.valid()) return px;
        }
    }
    if (entry.thumbnailData && !entry.thumbnailData->empty()) {
        return decodeImageMemory(entry.thumbnailData->data(), entry.thumbnailData->size());
    }
    return PixelBuffer{};
}

}  // namespace tf::shots
