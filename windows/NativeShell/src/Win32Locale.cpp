// Win32Locale.cpp — GetUserDefaultUILanguage -> locale name (FR-37).
#include "pch.h"

#include "tf/Win32Locale.hpp"

namespace tf {

std::string win32SystemUiLocaleName() {
    const LANGID langId = ::GetUserDefaultUILanguage();
    wchar_t wide[LOCALE_NAME_MAX_LENGTH]{};
    if (::LCIDToLocaleName(MAKELCID(langId, SORT_DEFAULT), wide, LOCALE_NAME_MAX_LENGTH,
                           0) > 0) {
        std::string out;
        out.reserve(wcslen(wide));
        for (const wchar_t* p = wide; *p; ++p) {
            out.push_back(static_cast<char>(*p));  // locale names are ASCII
        }
        return out;
    }
    return {};
}

}  // namespace tf
