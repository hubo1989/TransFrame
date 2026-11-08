// HistoryExport.cpp — portable serialization for FR-30 exports (see header).
// JSON goes through the vendored nlohmann/json (HistoryStore already uses it
// for meta_json); CSV/TXT are hand-rolled — the only tricky bit is the
// RFC-4180 quoting. Timestamps render in the machine's local time zone.
#include "tf/HistoryExport.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace tf {

namespace {

std::string localTimestamp(const core::TimePoint& t) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm local{};
#if defined(_WIN32)
    if (::localtime_s(&local, &tt) != 0) return {};
#else
    if (::localtime_r(&tt, &local) == nullptr) return {};
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

/// Multi-line text becomes "line ⏎ line" so TXT blocks stay scannable.
std::string singleLine(std::string s) {
    for (char& c : s) {
        if (c == '\n' || c == '\r') c = ' ';
    }
    return s;
}

bool csvNeedsQuotes(const std::string& field) {
    return field.find_first_of(",\"\n\r") != std::string::npos;
}

std::string csvField(const std::string& field) {
    if (!csvNeedsQuotes(field)) return field;
    std::string out = "\"";
    for (char c : field) {
        if (c == '"') out += "\"\"";
        out += c;
    }
    out += "\"";
    return out;
}

std::string categoryName(core::HistoryCategory c) {
    switch (c) {
        case core::HistoryCategory::Translation: return "翻译";
        case core::HistoryCategory::Screenshot: return "截图";
        case core::HistoryCategory::Color: return "取色";
        case core::HistoryCategory::Upload: return "上传";
        default: return "未知";
    }
}

}  // namespace

std::string serializeHistoryExport(const std::vector<core::TranslationHistory>& rows,
                                   HistoryExportFormat format) {
    switch (format) {
        case HistoryExportFormat::Json: {
            nlohmann::json array = nlohmann::json::array();
            for (const auto& e : rows) {
                nlohmann::json o = nlohmann::json::object();
                o["id"] = e.id.toString();
                o["category"] = core::toRaw(e.category);
                o["source"] = e.sourceText;
                o["translated"] = e.translatedText;
                o["sourceLang"] = e.sourceLanguage;
                o["targetLang"] = e.targetLanguage;
                o["timestamp"] = localTimestamp(e.timestamp);
                o["favorite"] = e.isFavorite;
                array.push_back(std::move(o));
            }
            nlohmann::json top = nlohmann::json::object();
            top["count"] = rows.size();
            top["entries"] = std::move(array);
            return top.dump(2);
        }
        case HistoryExportFormat::Csv: {
            // UTF-8 BOM: Excel otherwise decodes CJK as the ANSI code page.
            std::string out = "\xEF\xBB\xBF";
            out += "id,category,source,translated,source_lang,target_lang,timestamp,favorite\r\n";
            for (const auto& e : rows) {
                out += csvField(e.id.toString()) + "," +
                       csvField(core::toRaw(e.category)) + "," +
                       csvField(e.sourceText) + "," +
                       csvField(e.translatedText) + "," +
                       csvField(e.sourceLanguage) + "," +
                       csvField(e.targetLanguage) + "," +
                       csvField(localTimestamp(e.timestamp)) + "," +
                       (e.isFavorite ? "1" : "0") + "\r\n";
            }
            return out;
        }
        case HistoryExportFormat::Txt: {
            std::string out;
            for (const auto& e : rows) {
                out += "[" + categoryName(e.category) + "] " +
                       localTimestamp(e.timestamp);
                if (e.isFavorite) out += " ★";
                out += "\n";
                out += "原文: " + singleLine(e.sourceText) + "\n";
                out += "译文: " + singleLine(e.translatedText) + "\n";
                out += "---\n";
            }
            return out;
        }
    }
    return {};
}

std::optional<HistoryExportFormat> historyExportFormatFromExtension(const std::string& ext) {
    std::string lower;
    lower.reserve(ext.size());
    for (char c : ext) {
        if (c == '.' ) continue;
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower == "json") return HistoryExportFormat::Json;
    if (lower == "csv") return HistoryExportFormat::Csv;
    if (lower == "txt") return HistoryExportFormat::Txt;
    return std::nullopt;
}

}  // namespace tf
