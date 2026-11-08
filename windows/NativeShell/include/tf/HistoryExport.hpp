// tf/HistoryExport.hpp — history export serialization (PRD FR-30):
// JSON / CSV / TXT over already-filtered HistoryStore rows. Pure C++ (no
// Win32) so the smoke tool and Catch2 can run it anywhere; the file dialog
// stays in HistoryWindow.cpp.
//
// Formats (all UTF-8):
//   JSON — array of objects, one per entry, id/category/texts/langs/
//          timestamp/favorite; thumbnail bytes deliberately omitted.
//   CSV  — header row + one row per entry; fields RFC-4180-quoted on
//          demand ("," "\"" CR LF); UTF-8 BOM prepended so Excel opens
//          CJK text correctly.
//   TXT  — one readable block per entry ("[类别] 时间 ★" + 原文/译文 lines).
#ifndef TF_HISTORY_EXPORT_HPP
#define TF_HISTORY_EXPORT_HPP

#include "tf/HistoryStore.hpp"

#include <string>
#include <vector>

namespace tf {

enum class HistoryExportFormat { Json, Csv, Txt };

/// Serialize `rows` (query output order, newest first) to UTF-8 text.
std::string serializeHistoryExport(const std::vector<core::TranslationHistory>& rows,
                                   HistoryExportFormat format);

/// "json" / "csv" / "txt" (ASCII case-insensitive); nullopt on anything else.
std::optional<HistoryExportFormat> historyExportFormatFromExtension(const std::string& ext);

}  // namespace tf

#endif  // TF_HISTORY_EXPORT_HPP
