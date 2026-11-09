// tests/ColorPickerHistoryTests.cpp — FR-32/FR-41 pure helpers.
//
// colorPickHex / colorPickParseHex / colorPickRgbText are header-inline and
// drive the HEX formatting + parsing contract (spec §3/§4.3);
// colorHistoryGridLayout pins the >=48px-swatch grid math across DPIs
// (spec §4.2).
#include "catch_amalgamated.hpp"

#include "tf/ColorHistoryWindow.hpp"
#include "tf/ColorPicker.hpp"

#include <array>
#include <cstdint>
#include <string>

using namespace tf;

// ---------------------------------------------------------------------------
// FR-32 HEX formatting / parsing
// ---------------------------------------------------------------------------

TEST_CASE("colorPickHex formats uppercase #RRGGBB", "[picker]") {
    CHECK(colorPickHex(255, 0, 0) == "#FF0000");
    CHECK(colorPickHex(26, 43, 60) == "#1A2B3C");
    CHECK(colorPickHex(18, 52, 86) == "#123456");
    CHECK(colorPickHex(0, 0, 0) == "#000000");
    CHECK(colorPickHex(255, 255, 255) == "#FFFFFF");
    CHECK(colorPickHex(0xAB, 0xCD, 0xEF) == "#ABCDEF");
}

TEST_CASE("colorPickParseHex accepts #, bare and mixed-case forms", "[picker]") {
    const auto rgb = colorPickParseHex("#1A2B3C");
    REQUIRE(rgb.has_value());
    CHECK((*rgb)[0] == 0x1A);
    CHECK((*rgb)[1] == 0x2B);
    CHECK((*rgb)[2] == 0x3C);

    const auto bare = colorPickParseHex("ff8000");
    REQUIRE(bare.has_value());
    CHECK((*bare)[0] == 255);
    CHECK((*bare)[1] == 128);
    CHECK((*bare)[2] == 0);

    const auto mixed = colorPickParseHex("AbCdEf");
    REQUIRE(mixed.has_value());
    CHECK((*mixed)[0] == 0xAB);
    CHECK((*mixed)[1] == 0xCD);
    CHECK((*mixed)[2] == 0xEF);
}

TEST_CASE("colorPickParseHex rejects malformed input", "[picker]") {
    CHECK_FALSE(colorPickParseHex("").has_value());
    CHECK_FALSE(colorPickParseHex("#12345").has_value());     // 5 digits
    CHECK_FALSE(colorPickParseHex("12345").has_value());
    CHECK_FALSE(colorPickParseHex("#1234567").has_value());   // 7 digits
    CHECK_FALSE(colorPickParseHex("1234567").has_value());
    CHECK_FALSE(colorPickParseHex("#12345G").has_value());    // non-hex char
    CHECK_FALSE(colorPickParseHex("GGGGGG").has_value());
    CHECK_FALSE(colorPickParseHex(" 12345").has_value());     // stray space
    CHECK_FALSE(colorPickParseHex("#1A2B3").has_value());
}

TEST_CASE("colorPickHex and colorPickParseHex round-trip", "[picker]") {
    const std::array<std::array<std::uint8_t, 3>, 4> samples{{
        {0x00, 0x00, 0x00},
        {0xFF, 0xFF, 0xFF},
        {0x1A, 0x2B, 0x3C},
        {0x0F, 0x9A, 0x33},
    }};
    for (const auto& s : samples) {
        const std::string hex = colorPickHex(s[0], s[1], s[2]);
        const auto back = colorPickParseHex(hex);
        REQUIRE(back.has_value());
        CHECK((*back)[0] == s[0]);
        CHECK((*back)[1] == s[1]);
        CHECK((*back)[2] == s[2]);
    }
}

TEST_CASE("colorPickRgbText formats the secondary copy string", "[picker]") {
    CHECK(colorPickRgbText(255, 0, 0) == "RGB(255, 0, 0)");
    CHECK(colorPickRgbText(26, 43, 60) == "RGB(26, 43, 60)");
    CHECK(colorPickRgbText(0, 128, 255) == "RGB(0, 128, 255)");
}

// ---------------------------------------------------------------------------
// FR-41 grid layout (spec: swatch >= 48 px, hex + time per cell)
// ---------------------------------------------------------------------------

TEST_CASE("colorHistoryGridLayout packs 96-DPI grid cells", "[colorhistory]") {
    const ColorHistoryLayout l = colorHistoryGridLayout(560, 420, 96);
    CHECK(l.swatch == 48);                       // spec minimum at 100%
    CHECK(l.cellW == 66);                        // 48 + 18 padding
    CHECK(l.cellH == 80);                        // 48 + 32 (HEX + time lines)
    CHECK(l.gridX == 10);
    CHECK(l.gridY == 10);
    CHECK(l.columns == 8);                       // (560 - 20) / 66
    CHECK(l.rows == 5);                          // (420 - 10) / 80
}

TEST_CASE("colorHistoryGridLayout scales swatch at high DPI", "[colorhistory]") {
    const ColorHistoryLayout l = colorHistoryGridLayout(560, 420, 144);  // 150%
    CHECK(l.swatch == 72);
    CHECK(l.cellW == 99);
    CHECK(l.cellH == 120);
    CHECK(l.columns == 5);                       // (560 - 30) / 99
    CHECK(l.rows == 3);                          // (420 - 15) / 120
}

TEST_CASE("colorHistoryGridLayout keeps 48px swatch floor below 100% DPI",
          "[colorhistory]") {
    const ColorHistoryLayout l = colorHistoryGridLayout(400, 300, 72);   // 75%
    CHECK(l.swatch >= 48);                       // max(48, 36) = 48 floor
    CHECK(l.columns >= 1);
}

TEST_CASE("colorHistoryGridLayout clamps tiny clients to >=1 column, 0 rows",
          "[colorhistory]") {
    const ColorHistoryLayout tiny = colorHistoryGridLayout(100, 100, 96);
    CHECK(tiny.columns == 1);                    // max(1, 80 / 66)
    CHECK(tiny.rows == 1);                       // 90 / 80

    const ColorHistoryLayout flat = colorHistoryGridLayout(100, 0, 96);
    CHECK(flat.columns == 1);
    CHECK(flat.rows == 0);                       // no vertical room
}
