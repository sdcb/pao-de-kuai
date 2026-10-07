#pragma once

/*
 * Static card rendering: the face, the back, and the lift/rotation/glow treatment around them.
 *
 * Pure C.  The two public functions used a templated DrawCardCommon taking a lambda; that is a
 * function pointer plus a user pointer here, which keeps the shared treatment in one place
 * instead of duplicating it.
 *
 * CardLook needs the usual `#ifdef __cplusplus` default constructor: its members had brace
 * initialisers (`shadow{1.0f}`, `glowColor{theme::GoldLight}`), and a bare C struct would leave
 * `CardLook look;` indeterminate.  CardLook_Init is the single source of truth for those
 * defaults and the constructor calls it, so the two cannot drift.
 *
 * The C++ callers keep `ui::CardLook` and `ui::DrawCardFace(...)` through ui/CppCompat.h.
 */

#include "core/Geometry.h"
#include "graphics/D2DContext.h"
#include "graphics/SpriteAtlas.h"
#include "rules/Card.h"
#include "ui/Theme.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CardLook CardLook;

static inline void CardLook_Init(CardLook *look);

struct CardLook {
    float lift;
    float rotation;
    float hover;
    float glow;
    D2D1_COLOR_F glowColor;
    bool selected;
    float shadow;
    float opacity;

#ifdef __cplusplus
    /* ---- TEMPORARY C++ shim: delete with src/ui/CppCompat.h ---- */
    CardLook() { CardLook_Init(this); }
#endif
};

static inline void CardLook_Init(CardLook *look)
{
    look->lift = 0.0f;
    look->rotation = 0.0f;
    look->hover = 0.0f;
    look->glow = 0.0f;
    look->glowColor = THEME_GOLD_LIGHT;
    look->selected = false;
    look->shadow = 1.0f;
    look->opacity = 1.0f;
}

static inline CardLook CardLook_Default(void)
{
    CardLook look;

    CardLook_Init(&look);
    return look;
}

void CardView_DrawFace(RenderContext *context, SpriteAtlas *atlas, Card card, const Rect *rect,
                       const CardLook *look);
void CardView_DrawBack(RenderContext *context, SpriteAtlas *atlas, const Rect *rect,
                       const CardLook *look);

#ifdef __cplusplus
}
#endif
