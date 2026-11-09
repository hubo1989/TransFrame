// tf/Win32Locale.hpp — map the OS UI language onto the Core AppLanguage
// model (FR-37 "follow system"). Win32-only; Core stays portable.
#ifndef TF_WIN32_LOCALE_HPP
#define TF_WIN32_LOCALE_HPP

#include <string>

namespace tf {

/// BCP-47-ish locale name for the user's default UI language ("zh-CN",
/// "de-DE"), or empty when the mapping fails. Feeds
/// tf::core::appLanguageFromLocale.
std::string win32SystemUiLocaleName();

}  // namespace tf

#endif  // TF_WIN32_LOCALE_HPP
