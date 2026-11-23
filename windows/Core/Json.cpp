// Json.cpp — small codec helpers that need a translation unit.

#include "Json.h"

namespace tf::core {

static const char* kBase64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encodeBase64(const std::vector<std::uint8_t>& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 2 < data.size()) {
        const std::uint32_t n =
            (static_cast<std::uint32_t>(data[i]) << 16) |
            (static_cast<std::uint32_t>(data[i + 1]) << 8) |
            static_cast<std::uint32_t>(data[i + 2]);
        out += kBase64Chars[(n >> 18) & 63];
        out += kBase64Chars[(n >> 12) & 63];
        out += kBase64Chars[(n >> 6) & 63];
        out += kBase64Chars[n & 63];
        i += 3;
    }
    if (i + 1 == data.size()) {
        const std::uint32_t n = static_cast<std::uint32_t>(data[i]) << 16;
        out += kBase64Chars[(n >> 18) & 63];
        out += kBase64Chars[(n >> 12) & 63];
        out += "==";
    } else if (i + 2 == data.size()) {
        const std::uint32_t n =
            (static_cast<std::uint32_t>(data[i]) << 16) |
            (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out += kBase64Chars[(n >> 18) & 63];
        out += kBase64Chars[(n >> 12) & 63];
        out += kBase64Chars[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

static int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') { return c - 'A'; }
    if (c >= 'a' && c <= 'z') { return c - 'a' + 26; }
    if (c >= '0' && c <= '9') { return c - '0' + 52; }
    if (c == '+') { return 62; }
    if (c == '/') { return 63; }
    return -1;
}

std::vector<std::uint8_t> decodeBase64(const std::string& text) {
    std::vector<std::uint8_t> out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=' || c == '\n' || c == '\r') { continue; }
        const int v = base64Value(c);
        if (v < 0) { continue; }
        buffer = (buffer << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

}  // namespace tf::core
