#pragma once

#include "graphics/CppCompat.h"

#include <cstdint>

namespace pdk::ui {

constexpr D2D1_COLOR_F Rgb(std::uint32_t hex, float alpha = 1.0f) {
    return D2D1_COLOR_F{
        static_cast<float>((hex >> 16) & 0xFFu) / 255.0f,
        static_cast<float>((hex >> 8) & 0xFFu) / 255.0f,
        static_cast<float>(hex & 0xFFu) / 255.0f,
        alpha};
}

constexpr D2D1_COLOR_F WithAlpha(D2D1_COLOR_F color, float alpha) {
    return D2D1_COLOR_F{color.r, color.g, color.b, color.a * alpha};
}

namespace theme {

// Velvet table greens, from the lit centre of the felt out to the dark room.
constexpr D2D1_COLOR_F FeltLight = Rgb(0x1A5A42);
constexpr D2D1_COLOR_F Felt = Rgb(0x114231);
constexpr D2D1_COLOR_F FeltDeep = Rgb(0x0A2B20);
constexpr D2D1_COLOR_F Room = Rgb(0x061712);
constexpr D2D1_COLOR_F RoomDeep = Rgb(0x030C09);

// Ink panels.
constexpr D2D1_COLOR_F Ink = Rgb(0x0B1714);
constexpr D2D1_COLOR_F InkRaised = Rgb(0x13241F);
constexpr D2D1_COLOR_F InkHover = Rgb(0x1B332B);

// Champagne gold.
constexpr D2D1_COLOR_F Gold = Rgb(0xD8B878);
constexpr D2D1_COLOR_F GoldLight = Rgb(0xF1DDA6);
constexpr D2D1_COLOR_F GoldDeep = Rgb(0x9C7A3C);
constexpr D2D1_COLOR_F GoldInk = Rgb(0x2A1F0C);

// Cinnabar, reserved for seals, bombs and destructive actions.
constexpr D2D1_COLOR_F Cinnabar = Rgb(0xC23B2E);
constexpr D2D1_COLOR_F CinnabarLight = Rgb(0xE0604C);
constexpr D2D1_COLOR_F CinnabarDeep = Rgb(0x8E2A22);

constexpr D2D1_COLOR_F Ivory = Rgb(0xF3EBD8);
constexpr D2D1_COLOR_F Muted = Rgb(0xA9B8A8);
constexpr D2D1_COLOR_F Faint = Rgb(0x6F8479);

constexpr D2D1_COLOR_F Jade = Rgb(0x7FD1A0);
constexpr D2D1_COLOR_F Rust = Rgb(0xE08A7A);

constexpr float PanelRadius = 16.0f;
constexpr float CardRadiusRatio = 0.055f;

} // namespace theme

inline graphics::TextStyle Text(float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL) {
    graphics::TextStyle style;
    style.size = size;
    style.weight = weight;
    return style;
}

inline graphics::TextStyle Centered(graphics::TextStyle style) {
    style.align = DWRITE_TEXT_ALIGNMENT_CENTER;
    style.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    return style;
}

inline graphics::TextStyle Kai(float size) {
    graphics::TextStyle style;
    style.size = size;
    style.family = graphics::FontFamily::Kai;
    style.weight = DWRITE_FONT_WEIGHT_BOLD;
    return style;
}

inline D2D1_COLOR_F ScoreColor(int score) {
    if (score > 0) {
        return theme::Jade;
    }
    if (score < 0) {
        return theme::Rust;
    }
    return theme::Muted;
}

} // namespace pdk::ui
