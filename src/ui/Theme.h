#pragma once

/*
 * The palette, and the handful of colour helpers the UI shares.
 *
 * Pure C.  The C++ version used `constexpr D2D1_COLOR_F` in a namespace; here the values are
 * `static const` structs initialised by PDK_RGBA, which is still a compile-time constant
 * expression, and the names carry a THEME_ prefix so they are legal at file scope.
 *
 * ui/CppCompat.h re-exports them as `theme::Gold` and friends, so the 230 call sites that spell
 * them that way did not have to change.  `Text`/`Centered`/`Kai` moved there too: they return a
 * TextStyle, which in C++ is the DWRITE-typed facade struct rather than the C one.
 */

#include "graphics/D2DContext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fills a D2D1_COLOR_F from 0xRRGGBB at compile time. */
#define PDK_RGBA(hex, alpha)                                                             \
    {                                                                                    \
        (float)(((hex) >> 16) & 0xFFu) / 255.0f, (float)(((hex) >> 8) & 0xFFu) / 255.0f, \
            (float)((hex) & 0xFFu) / 255.0f, (alpha)                                     \
    }

/* Runtime form, for colours computed from data rather than written as literals.  C has no
 * default arguments, so callers wanting alpha = 1 pass it explicitly; ui/CppCompat.h restores
 * the one-argument form for the C++ callers. */
static inline D2D1_COLOR_F Rgb(uint32_t hex, float alpha)
{
    const D2D1_COLOR_F color = PDK_RGBA(hex, alpha);

    return color;
}

static inline D2D1_COLOR_F WithAlpha(D2D1_COLOR_F color, float alpha)
{
    D2D1_COLOR_F out;

    out.r = color.r;
    out.g = color.g;
    out.b = color.b;
    out.a = color.a * alpha;
    return out;
}

/* Velvet table greens, from the lit centre of the felt out to the dark room. */
static const D2D1_COLOR_F THEME_FELT_LIGHT = PDK_RGBA(0x1A5A42, 1.0f);
static const D2D1_COLOR_F THEME_FELT = PDK_RGBA(0x114231, 1.0f);
static const D2D1_COLOR_F THEME_FELT_DEEP = PDK_RGBA(0x0A2B20, 1.0f);
static const D2D1_COLOR_F THEME_ROOM = PDK_RGBA(0x061712, 1.0f);
static const D2D1_COLOR_F THEME_ROOM_DEEP = PDK_RGBA(0x030C09, 1.0f);

/* Ink panels. */
static const D2D1_COLOR_F THEME_INK = PDK_RGBA(0x0B1714, 1.0f);
static const D2D1_COLOR_F THEME_INK_RAISED = PDK_RGBA(0x13241F, 1.0f);
static const D2D1_COLOR_F THEME_INK_HOVER = PDK_RGBA(0x1B332B, 1.0f);

/* Champagne gold. */
static const D2D1_COLOR_F THEME_GOLD = PDK_RGBA(0xD8B878, 1.0f);
static const D2D1_COLOR_F THEME_GOLD_LIGHT = PDK_RGBA(0xF1DDA6, 1.0f);
static const D2D1_COLOR_F THEME_GOLD_DEEP = PDK_RGBA(0x9C7A3C, 1.0f);
static const D2D1_COLOR_F THEME_GOLD_INK = PDK_RGBA(0x2A1F0C, 1.0f);

/* Cinnabar, reserved for seals, bombs and destructive actions. */
static const D2D1_COLOR_F THEME_CINNABAR = PDK_RGBA(0xC23B2E, 1.0f);
static const D2D1_COLOR_F THEME_CINNABAR_LIGHT = PDK_RGBA(0xE0604C, 1.0f);
static const D2D1_COLOR_F THEME_CINNABAR_DEEP = PDK_RGBA(0x8E2A22, 1.0f);

static const D2D1_COLOR_F THEME_IVORY = PDK_RGBA(0xF3EBD8, 1.0f);
static const D2D1_COLOR_F THEME_MUTED = PDK_RGBA(0xA9B8A8, 1.0f);
static const D2D1_COLOR_F THEME_FAINT = PDK_RGBA(0x6F8479, 1.0f);

static const D2D1_COLOR_F THEME_JADE = PDK_RGBA(0x7FD1A0, 1.0f);
static const D2D1_COLOR_F THEME_RUST = PDK_RGBA(0xE08A7A, 1.0f);

#define THEME_PANEL_RADIUS 16.0f
#define THEME_CARD_RADIUS_RATIO 0.055f

static inline D2D1_COLOR_F ScoreColor(int score)
{
    if (score > 0) {
        return THEME_JADE;
    }
    if (score < 0) {
        return THEME_RUST;
    }
    return THEME_MUTED;
}

#ifdef __cplusplus
}
#endif
