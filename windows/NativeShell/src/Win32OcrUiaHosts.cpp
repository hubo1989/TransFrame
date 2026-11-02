// Win32OcrUiaHosts.cpp — WinRT OCR + UIA + clipboard + SendInput seams.
// NOT RUN on this macOS host (requires Windows SDK); compile under MSVC
// (C++/WinRT + WindowsApp.lib). Structure mirrors the protocol contracts in
// Core/include/tf/OcrEngines.hpp and TextSelectionInsertion.hpp.
#include "tf/Win32OcrUiaHosts.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <optional>

#pragma comment(lib, "windowsapp")

namespace tf {

// ===========================================================================
// WinRT OCR glue
// ===========================================================================
WindowsOcrEngine::LanguageProvider MakeWinRtLanguageProvider() {
    return []() -> std::vector<WindowsOcrEngine::Language> {
        std::vector<WindowsOcrEngine::Language> out;
        try {
            const auto langs = winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages();
            for (const auto& lang : langs) {
                WindowsOcrEngine::Language item;
                item.tag = winrt::to_string(lang.LanguageTag());
                item.displayName = winrt::to_string(lang.DisplayName());
                out.push_back(std::move(item));
            }
        } catch (const winrt::hresult_error&) {
            // UIA/WinRT can fail in severed sessions; empty list → engine
            // reports unavailable (gray).
        }
        return out;
    };
}

WindowsOcrEngine::Recognizer MakeWinRtRecognizer() {
    return [](const OcrImage& image, const std::vector<std::string>& languageTags,
              std::vector<WindowsOcrEngine::RawLine>& outLines) -> std::optional<ProviderError> {
        try {
            using namespace winrt::Windows::Globalization;
            using namespace winrt::Windows::Graphics::Imaging;
            using namespace winrt::Windows::Media::Ocr;
            using namespace winrt::Windows::Storage::Streams;

            Language lang = nullptr;
            for (const auto& tag : languageTags) {
                // Language has no static factory; the ctor parses the BCP-47
                // tag (throws on malformed input -> caught below).
                lang = Language(winrt::to_hstring(tag));
                if (OcrEngine::IsLanguageSupported(lang)) break;
                lang = nullptr;
            }
            if (lang == nullptr) {
                return ProviderError::configuration("No OCR language pack installed");
            }
            OcrEngine engine = OcrEngine::TryCreateFromLanguage(lang);
            if (engine == nullptr) {
                return ProviderError::configuration("OCR engine creation failed");
            }

            // BGRA → SoftwareBitmap. The input is raw header-less pixels, so
            // a BitmapDecoder cannot parse it — build the bitmap directly
            // from the buffer (Bgra8, Premultiplied is OCR-supported alpha).
            winrt::Windows::Storage::Streams::Buffer buffer(
                static_cast<uint32_t>(image.bgra.size()));
            std::memcpy(buffer.data(), image.bgra.data(), image.bgra.size());
            buffer.Length(static_cast<uint32_t>(image.bgra.size()));
            SoftwareBitmap bitmap = SoftwareBitmap::CreateCopyFromBuffer(
                buffer, BitmapPixelFormat::Bgra8, static_cast<int>(image.width),
                static_cast<int>(image.height));
            if (bitmap == nullptr) {
                return ProviderError::content("Bitmap conversion failed");
            }

            // Qualify: an unqualified OcrResult resolves to tf::OcrResult
            // (the Core model), which is not the WinRT projection type.
            winrt::Windows::Media::Ocr::OcrResult result = engine.RecognizeAsync(bitmap).get();
            for (const auto& line : result.Lines()) {
                WindowsOcrEngine::RawLine raw;
                raw.text = winrt::to_string(line.Text());
                // OcrLine has no BoundingRect; union the word rects (DIPs,
                // top-left origin).
                std::optional<winrt::Windows::Foundation::Rect> bounds;
                for (const auto& word : line.Words()) {
                    const auto r = word.BoundingRect();
                    if (!bounds) {
                        bounds = r;
                    } else {
                        const double left = std::min<double>(bounds->X, r.X);
                        const double top = std::min<double>(bounds->Y, r.Y);
                        const double right =
                            std::max<double>(bounds->X + bounds->Width, r.X + r.Width);
                        const double bottom =
                            std::max<double>(bounds->Y + bounds->Height, r.Y + r.Height);
                        bounds = winrt::Windows::Foundation::Rect(
                            static_cast<float>(left), static_cast<float>(top),
                            static_cast<float>(right - left),
                            static_cast<float>(bottom - top));
                    }
                }
                if (bounds) {
                    const double scale = 1.0;  // SoftwareBitmap works in pixels
                    raw.x = static_cast<std::int64_t>(bounds->X * scale);
                    raw.y = static_cast<std::int64_t>(bounds->Y * scale);
                    raw.w = static_cast<std::int64_t>(bounds->Width * scale);
                    raw.h = static_cast<std::int64_t>(bounds->Height * scale);
                } else {
                    raw.x = raw.y = raw.w = raw.h = 0;
                }
                raw.confidence = 0.0f;  // no score → mapped to 1.0 upstream
                if (!raw.text.empty()) outLines.push_back(std::move(raw));
            }
            return std::nullopt;
        } catch (const winrt::hresult_error& e) {
            return ProviderError::content("OCR failed: " + winrt::to_string(e.message()));
        }
    };
}

// ===========================================================================
// PaddleOCR process runner
// ===========================================================================
PaddleOcrEngine::ProcessRunner MakeWin32ProcessRunner() {
    return [](const std::string& executable, const std::vector<std::string>& args,
              int timeoutSeconds, std::string& outStdout,
              std::string& outStderr) -> std::optional<ProviderError> {
        // Command line: quote executable + args (argv-style; no shell — Swift
        // security rule rejects shell metacharacters in the path).
        std::string cmdLine = "\"" + executable + "\"";
        for (const auto& a : args) {
            cmdLine += " \"";
            for (char c : a) {
                if (c == '"' || c == '\\') cmdLine += '\\';
                cmdLine += c;
            }
            cmdLine += "\"";
        }

        SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE outRead = nullptr, outWrite = nullptr;
        HANDLE errRead = nullptr, errWrite = nullptr;
        CreatePipe(&outRead, &outWrite, &sa, 0);
        CreatePipe(&errRead, &errWrite, &sa, 0);
        SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = outWrite;
        si.hStdError = errWrite;
        PROCESS_INFORMATION pi{};

        std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
        mutableCmd.push_back('\0');
        if (!CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &si, &pi)) {
            CloseHandle(outRead); CloseHandle(outWrite);
            CloseHandle(errRead); CloseHandle(errWrite);
            return ProviderError::network("Failed to launch PaddleOCR process");
        }
        CloseHandle(outWrite);
        CloseHandle(errWrite);

        // Read until exit or timeout.
        const DWORD deadline = static_cast<DWORD>(timeoutSeconds) * 1000;
        const DWORD start = GetTickCount();
        std::atomic<bool> exited{false};
        DWORD exitCode = 0;
        char buf[4096];
        while (!exited) {
            DWORD avail = 0;
            if (PeekNamedPipe(outRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                DWORD read = 0;
                if (ReadFile(outRead, buf, sizeof(buf), &read, nullptr) && read > 0) {
                    outStdout.append(buf, read);
                }
            }
            if (PeekNamedPipe(errRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                DWORD read = 0;
                if (ReadFile(errRead, buf, sizeof(buf), &read, nullptr) && read > 0) {
                    outStderr.append(buf, read);
                }
            }
            if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
                exited = true;
                GetExitCodeProcess(pi.hProcess, &exitCode);
            } else if (GetTickCount() - start > deadline) {
                TerminateProcess(pi.hProcess, 1);
                CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                CloseHandle(outRead); CloseHandle(errRead);
                return ProviderError::network("PaddleOCR timed out");
            }
        }
        // Drain remainder.
        DWORD avail = 0;
        while (PeekNamedPipe(outRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD read = 0;
            if (!ReadFile(outRead, buf, sizeof(buf), &read, nullptr) || read == 0) break;
            outStdout.append(buf, read);
        }
        while (PeekNamedPipe(errRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD read = 0;
            if (!ReadFile(errRead, buf, sizeof(buf), &read, nullptr) || read == 0) break;
            outStderr.append(buf, read);
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(outRead);
        CloseHandle(errRead);
        if (exitCode != 0) {
            return ProviderError::content("PaddleOCR exited with code " +
                                          std::to_string(exitCode));
        }
        return std::nullopt;
    };
}

PaddleOcrEngine::AvailabilityChecker MakeWin32AvailabilityChecker() {
    return [](const std::string& executablePath) -> bool {
        const DWORD attr = GetFileAttributesA(executablePath.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    };
}

// ===========================================================================
// Win32TextExtractorHost (UIA + clipboard + SendInput)
// ===========================================================================
struct Win32TextExtractorHost::Impl {
    // IUIAutomation instance is created lazily; the header avoids pulling
    // UIAutomation.h into Core. NativeShell owns the raw pointer.
    void* automation = nullptr;
};

namespace {

std::wstring ToWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), len);
    out.resize(out.size() - 1);
    return out;
}

std::string ToUtf8(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    const int len =
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), len, nullptr, nullptr);
    out.resize(out.size() - 1);
    return out;
}

bool SendCtrlShortcut(WORD key) {
    INPUT inputs[4]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = key;
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = key;
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(4, inputs, sizeof(INPUT)) == 4;
}

}  // namespace

Win32TextExtractorHost::Win32TextExtractorHost() : impl_(new Impl) {}

Win32TextExtractorHost::~Win32TextExtractorHost() { delete impl_; }

TextExtractorHost::ClipboardBackup Win32TextExtractorHost::backupClipboard() {
    // CF_UNICODETEXT snapshot (Swift PasteboardBackup keeps formats it can
    // restore; text is the format this flow ever writes).
    ClipboardBackup backup;
    if (!OpenClipboard(nullptr)) return backup;
    backup.sequenceNumber = GetClipboardSequenceNumber();
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(h));
        if (text) {
            backup.text = ToUtf8(text);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return backup;
}

void Win32TextExtractorHost::restoreClipboard(const ClipboardBackup& backup) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    if (!backup.text.empty()) {
        const std::wstring wide = ToWide(backup.text);
        const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (h) {
            void* dst = GlobalLock(h);
            if (dst) {
                memcpy(dst, wide.c_str(), bytes);
                GlobalUnlock(h);
                SetClipboardData(CF_UNICODETEXT, h);
            }
        }
    }
    CloseClipboard();
}

void Win32TextExtractorHost::clearClipboard() {
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        CloseClipboard();
    }
}

std::int64_t Win32TextExtractorHost::setClipboardText(const std::string& text) {
    const std::wstring wide = ToWide(text);
    if (!OpenClipboard(nullptr)) return -1;
    EmptyClipboard();
    const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) {
        void* dst = GlobalLock(h);
        if (dst) {
            memcpy(dst, wide.c_str(), bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        }
    }
    CloseClipboard();
    return GetClipboardSequenceNumber();
}

std::int64_t Win32TextExtractorHost::clipboardSequenceNumber() {
    return GetClipboardSequenceNumber();
}

std::optional<std::string> Win32TextExtractorHost::clipboardText() {
    if (!OpenClipboard(nullptr)) return std::nullopt;
    std::optional<std::string> out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(h));
        if (text) {
            out = ToUtf8(text);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

bool Win32TextExtractorHost::sendCopyShortcut() { return SendCtrlShortcut('C'); }
bool Win32TextExtractorHost::sendPasteShortcut() { return SendCtrlShortcut('V'); }

std::string Win32TextExtractorHost::foregroundProcessName() {
    const HWND hwnd = GetForegroundWindow();
    if (!hwnd) return {};
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    wchar_t name[MAX_PATH]{};
    DWORD size = MAX_PATH;
    std::string out;
    if (QueryFullProcessImageNameW(h, 0, name, &size)) {
        out = ToUtf8(name);
        const std::size_t slash = out.find_last_of("\\/");
        if (slash != std::string::npos) out = out.substr(slash + 1);
    }
    CloseHandle(h);
    return out;
}

std::uint32_t Win32TextExtractorHost::foregroundProcessId() {
    const HWND hwnd = GetForegroundWindow();
    if (!hwnd) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid;
}

void Win32TextExtractorHost::sleepMilliseconds(int ms) { Sleep(static_cast<DWORD>(ms)); }

}  // namespace tf

#endif  // _WIN32
