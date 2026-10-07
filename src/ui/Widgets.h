#pragma once

/*
 * Buttons, panels, chips, avatars, seals and the room backdrop.
 *
 * Pure C.  What changed and why:
 *   - `std::string text` on Button and the text parameters became caller-owned C strings; the
 *     button's own label is a fixed buffer because a Button lives in a std::vector that is
 *     copied around.
 *   - `ButtonStyle`/`Anchor` are uint8_t plus UI_BUTTON_ and UI_ANCHOR_ constants, so C can
 *     switch on them; ui/CppCompat.h keeps the enum-class spelling for the C++ callers.
 *   - PanelStyle/ChipStyle/Button need the usual `#ifdef __cplusplus` default constructors: their
 *     members had brace initialisers, and `PanelStyle style;` or `{}` would otherwise leave an
 *     invisible panel.  Each *_Init function is the single source of truth for those defaults and
 *     the constructor calls it, so the two cannot drift.  Button deliberately has NO constructor:
 *     overlays aggregate-initialise it positionally (`{{rect}, "text", style}`), and a
 *     user-provided constructor would make it a non-aggregate.  Those sites use MakeButton in the
 *     facade instead, which applies the same defaults.
 *   - The free functions lost their default arguments (C has none); ui/CppCompat.h restores them.
 */

#include "core/Geometry.h"
#include "graphics/D2DContext.h"
#include "ui/Anim.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PDK_BUTTON_TEXT_CAP = 64 };

typedef uint8_t ButtonStyle;

enum {
    UI_BUTTON_PRIMARY = 0,
    UI_BUTTON_SECONDARY = 1,
    UI_BUTTON_GHOST = 2,
    UI_BUTTON_DANGER = 3,
    UI_BUTTON_ICON = 4
};

typedef uint8_t Anchor;

enum {
    UI_ANCHOR_LEFT = 0,
    UI_ANCHOR_CENTER = 1,
    UI_ANCHOR_RIGHT = 2
};

typedef struct Button Button;
typedef struct PanelStyle PanelStyle;
typedef struct ChipStyle ChipStyle;

static inline void Button_Init(Button *button);
static inline void PanelStyle_Init(PanelStyle *style);
static inline void ChipStyle_Init(ChipStyle *style);

struct Button {
    Rect rect;
    char text[PDK_BUTTON_TEXT_CAP];
    ButtonStyle style;
    Icon icon;
    float fontSize;
    bool visible;
    bool enabled;
    bool hover;
    float hoverT;
    float pressT;
    float visibleT;
};

struct PanelStyle {
    float radius;
    float shadow;
    float fillAlpha;
    bool ornament;
    bool innerLine;
    D2D1_COLOR_F top;
    D2D1_COLOR_F bottom;

#ifdef __cplusplus
    /* ---- TEMPORARY C++ shim: delete with src/ui/CppCompat.h ---- */
    PanelStyle() { PanelStyle_Init(this); }
#endif
};

struct ChipStyle {
    D2D1_COLOR_F fill;
    D2D1_COLOR_F stroke;
    D2D1_COLOR_F text;
    float fontSize;
    float height;
    float padX;
    int weight; /* DWRITE_FONT_WEIGHT */

#ifdef __cplusplus
    ChipStyle() { ChipStyle_Init(this); }
#endif
};

static inline void Button_Init(Button *button)
{
    button->rect.x = 0.0f;
    button->rect.y = 0.0f;
    button->rect.width = 0.0f;
    button->rect.height = 0.0f;
    button->text[0] = '\0';
    button->style = UI_BUTTON_SECONDARY;
    button->icon = UI_ICON_NONE;
    button->fontSize = 19.0f;
    button->visible = true;
    button->enabled = true;
    button->hover = false;
    button->hoverT = 0.0f;
    button->pressT = 0.0f;
    button->visibleT = 1.0f;
}

static inline void PanelStyle_Init(PanelStyle *style)
{
    style->radius = THEME_PANEL_RADIUS;
    style->shadow = 1.0f;
    style->fillAlpha = 0.94f;
    style->ornament = true;
    style->innerLine = true;
    style->top = THEME_INK_RAISED;
    style->bottom = THEME_INK;
}

static inline void ChipStyle_Init(ChipStyle *style)
{
    style->fill = WithAlpha(THEME_INK, 0.82f);
    style->stroke = WithAlpha(THEME_GOLD, 0.35f);
    style->text = THEME_IVORY;
    style->fontSize = 15.0f;
    style->height = 28.0f;
    style->padX = 13.0f;
    style->weight = 400; /* DWRITE_FONT_WEIGHT_NORMAL */
}

static inline Button Button_Default(void)
{
    Button button;

    Button_Init(&button);
    return button;
}

static inline PanelStyle PanelStyle_Default(void)
{
    PanelStyle style;

    PanelStyle_Init(&style);
    return style;
}

static inline ChipStyle ChipStyle_Default(void)
{
    ChipStyle style;

    ChipStyle_Init(&style);
    return style;
}

void Button_Update(Button *button, float dt);
void Button_Draw(const Button *button, RenderContext *context);
bool Button_HitTest(const Button *button, float x, float y);
void Button_UpdateHover(Button *button, float x, float y);

void ButtonGroup_DrawAll(RenderContext *context, const Button *buttons, int count);
void ButtonGroup_UpdateAll(Button *buttons, int count, float dt);
/* Returns the hit index and starts that button's press animation. */
int ButtonGroup_Hit(Button *buttons, int count, float x, float y);
void ButtonGroup_UpdateHover(Button *buttons, int count, float x, float y);

void Widgets_DrawPanel(RenderContext *context, const Rect *rect, const PanelStyle *style);
void Widgets_DrawOrnamentCorners(RenderContext *context, const Rect *rect, D2D1_COLOR_F color,
                                 float size, float inset);
/* Horizontal gold line that fades out at both ends. */
void Widgets_DrawHairline(RenderContext *context, float x0, float x1, float y, float alpha);

Rect Widgets_ChipRect(RenderContext *context, Point anchor, Anchor align, const char *text,
                      const ChipStyle *style);
void Widgets_DrawChipInRect(RenderContext *context, const Rect *rect, const char *text,
                            const ChipStyle *style);
Rect Widgets_DrawChip(RenderContext *context, Point anchor, Anchor align, const char *text,
                      const ChipStyle *style);

/* First visible character for avatars: one CJK glyph, or up to three ASCII characters. */
void Widgets_AvatarLabel(const char *name, char *out, int cap);
void Widgets_DrawAvatar(RenderContext *context, const Rect *rect, const char *label, bool active,
                        float time);
/* Soft circular glow; ring > 0 turns it into a halo that peaks at that fraction of the radius. */
void Widgets_DrawRadialGlow(RenderContext *context, Point center, float radius,
                            D2D1_COLOR_F color, float ring);

void Widgets_DrawSeal(RenderContext *context, Point center, float size, const char *text,
                      D2D1_COLOR_F color, float rotation, float fontSize);
void Widgets_DrawProgressBar(RenderContext *context, const Rect *rect, float value, float time);

/* Full-screen velvet backdrop used by the menu scenes. */
void Widgets_DrawRoomBackground(RenderContext *context, Point focus);
void Widgets_DrawVignette(RenderContext *context, float strength);
void Widgets_DrawBackdrop(RenderContext *context, float alpha);

/* Compact confirm dialog: panel, round icon badge, title and one line of explanation. */
void Widgets_DrawDialogBody(RenderContext *context, const Rect *panel, Icon icon,
                            D2D1_COLOR_F accent, const char *title, const char *subtitle);

/* Modal enter animation: dims the scene, then fades and springs the panel in. */
void Widgets_BeginModal(RenderContext *context, const Rect *panel, float elapsed);
void Widgets_EndModal(RenderContext *context);

#ifdef __cplusplus
}
#endif
