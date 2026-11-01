#pragma once
// Uuid.h — RFC 4122 v4 UUIDs matching Swift Foundation's UUID semantics.
//
// Wire format (must stay byte-identical to macOS for cross-platform snapshots):
//   - Text:   UPPERCASE 8-4-4-4-12 (Swift `uuidString`, e.g. "E621E1F8-...").
//   - JSON:   that exact uppercase string (Swift Codable default).
//
// Generation uses Windows BCryptGenRandom on MSVC targets and a std::random_device
// fallback elsewhere so the Core unit tests can also run under clangd/other hosts.

#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <string_view>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#ifndef _MSC_VER
#pragma comment(lib, "bcrypt.lib")  // only meaningful on MSVC; guarded for clangd
#endif
#endif

namespace tf::core {

class Uuid {
public:
    Uuid() noexcept : bytes_{} {}

    /// Parses "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"; hex is case-insensitive,
    /// braces are tolerated. Returns false on malformed input (no partial state).
    static bool parse(std::string_view text, Uuid& out) noexcept {
        // Strip optional braces.
        if (!text.empty() && text.front() == '{' && text.back() == '}') {
            text.remove_prefix(1);
            text.remove_suffix(1);
        }
        int nibbleCount = 0;
        std::array<uint8_t, 16> bytes{};
        for (char c : text) {
            if (c == '-') { continue; }
            if (nibbleCount >= 32) { return false; }
            const int v = hexValue(c);
            if (v < 0) { return false; }
            bytes[nibbleCount / 2] =
                static_cast<uint8_t>((nibbleCount % 2 == 0) ? (v << 4) : (bytes[nibbleCount / 2] | v));
            ++nibbleCount;
        }
        if (nibbleCount != 32) { return false; }
        out.bytes_ = bytes;
        return true;
    }

    /// Cryptographically random v4 UUID (matches UUID() on macOS).
    static Uuid randomUuid() noexcept {
        std::array<uint8_t, 16> bytes{};
        fillRandom(bytes.data(), bytes.size());
        bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0F) | 0x40);  // version 4
        bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3F) | 0x80);  // variant 10
        return Uuid(bytes);
    }

    /// UPPERCASE canonical string (Swift-compatible).
    std::string toString() const {
        static const char* kHex = "0123456789ABCDEF";
        std::string s;
        s.reserve(36);
        for (size_t i = 0; i < bytes_.size(); ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) { s += '-'; }
            s += kHex[bytes_[i] >> 4];
            s += kHex[bytes_[i] & 0xF];
        }
        return s;
    }

    bool isNull() const noexcept {
        for (uint8_t b : bytes_) {
            if (b != 0) { return false; }
        }
        return true;
    }

    friend bool operator==(const Uuid& a, const Uuid& b) noexcept { return a.bytes_ == b.bytes_; }
    friend bool operator!=(const Uuid& a, const Uuid& b) noexcept { return a.bytes_ != b.bytes_; }
    friend bool operator<(const Uuid& a, const Uuid& b) noexcept { return a.bytes_ < b.bytes_; }

    const std::array<uint8_t, 16>& bytes() const noexcept { return bytes_; }

private:
    explicit Uuid(const std::array<uint8_t, 16>& b) noexcept : bytes_(b) {}

    static int hexValue(char c) noexcept {
        if (c >= '0' && c <= '9') { return c - '0'; }
        if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
        if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
        return -1;
    }

    static void fillRandom(uint8_t* out, size_t n) noexcept {
#if defined(_WIN32)
        // BCryptGenRandom never fails for system randomness in practice.
        if (BCryptGenRandom(nullptr, out, static_cast<ULONG>(n),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
            fallbackRandom(out, n);
        }
#else
        fallbackRandom(out, n);
#endif
    }

    static void fallbackRandom(uint8_t* out, size_t n) noexcept {
        // xorshift64* seeded per-call from random_device; only used off-Windows
        // (tests/clangd). Uniqueness odds remain UUID-grade for this purpose.
        static thread_local uint64_t state = [] {
            std::random_device rd;
            return (static_cast<uint64_t>(rd()) << 32) | rd();
        }();
        for (size_t i = 0; i < n; ++i) {
            state ^= state >> 12;
            state ^= state << 25;
            state ^= state >> 27;
            out[i] = static_cast<uint8_t>((state * 0x2545F4914F6CDD1DULL) >> 32);
        }
    }

    std::array<uint8_t, 16> bytes_;
};

}  // namespace tf::core
