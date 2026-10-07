#pragma once

/*
 * TEMPORARY TRANSITION HEADER -- DELETE WHEN THE PORT IS DONE.
 *
 * `src/ui/Icons.*` and `src/ui/Theme.h` are pure C now (plan.md S7): the Icon enum class became
 * UI_ICON_* constants, the palette became THEME_* values initialised at compile time, and the
 * drawing goes through the C RenderContext.
 *
 * The C++ callers still write `ui::Icon::Back`, `ui::DrawIcon(...)`, `theme::Gold`,
 * `ui::Text(20.0f)` and so on, so this header reproduces exactly those.  Re-exporting the palette
 * as `inline const D2D1_COLOR_F` under the old names kept 230 call sites unchanged; that is the
 * whole point of doing it this way rather than renaming them.
 *
 * `Text`/`Centered`/`Kai` live here rather than in Theme.h because they return a TextStyle, and
 * in C++ that is the DWRITE-typed facade struct (the C one carries plain ints, since MSVC's C
 * mode cannot parse <dwrite.h>).
 *
 * It grows as the rest of src/ui is converted and disappears with the last C++ file under src/.
 */

#include "graphics/CppCompat.h"
#include "rules/CppCompat.h"
#include "ui/CardView.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace pdk::ui {

enum class Icon {
    None = UI_ICON_NONE,
    Back = UI_ICON_BACK,
    Close = UI_ICON_CLOSE,
    Star = UI_ICON_STAR,
    Exit = UI_ICON_EXIT,
    Alert = UI_ICON_ALERT,
    Sparkle = UI_ICON_SPARKLE
};

inline void DrawIcon(graphics::RenderContext& context, Icon icon, const Rect& rect,
                     D2D1_COLOR_F color)
{
    Icons_Draw(context.Native(), static_cast<::Icon>(icon), &rect, color);
}

using ::CardLook;

inline void DrawCardFace(graphics::RenderContext& context, graphics::SpriteAtlas& atlas,
                         const rules::Card& card, const Rect& rect, const CardLook& look)
{
    CardView_DrawFace(context.Native(), atlas.Native(), card, &rect, &look);
}

inline void DrawCardBack(graphics::RenderContext& context, graphics::SpriteAtlas& atlas,
                         const Rect& rect, const CardLook& look)
{
    CardView_DrawBack(context.Native(), atlas.Native(), &rect, &look);
}

/* C has no default arguments, so the runtime colour helper takes alpha explicitly there. */
inline D2D1_COLOR_F Rgb(std::uint32_t hex, float alpha = 1.0f)
{
    return ::Rgb(hex, alpha);
}

namespace theme {

inline const D2D1_COLOR_F FeltLight = THEME_FELT_LIGHT;
inline const D2D1_COLOR_F Felt = THEME_FELT;
inline const D2D1_COLOR_F FeltDeep = THEME_FELT_DEEP;
inline const D2D1_COLOR_F Room = THEME_ROOM;
inline const D2D1_COLOR_F RoomDeep = THEME_ROOM_DEEP;

inline const D2D1_COLOR_F Ink = THEME_INK;
inline const D2D1_COLOR_F InkRaised = THEME_INK_RAISED;
inline const D2D1_COLOR_F InkHover = THEME_INK_HOVER;

inline const D2D1_COLOR_F Gold = THEME_GOLD;
inline const D2D1_COLOR_F GoldLight = THEME_GOLD_LIGHT;
inline const D2D1_COLOR_F GoldDeep = THEME_GOLD_DEEP;
inline const D2D1_COLOR_F GoldInk = THEME_GOLD_INK;

inline const D2D1_COLOR_F Cinnabar = THEME_CINNABAR;
inline const D2D1_COLOR_F CinnabarLight = THEME_CINNABAR_LIGHT;
inline const D2D1_COLOR_F CinnabarDeep = THEME_CINNABAR_DEEP;

inline const D2D1_COLOR_F Ivory = THEME_IVORY;
inline const D2D1_COLOR_F Muted = THEME_MUTED;
inline const D2D1_COLOR_F Faint = THEME_FAINT;

inline const D2D1_COLOR_F Jade = THEME_JADE;
inline const D2D1_COLOR_F Rust = THEME_RUST;

inline constexpr float PanelRadius = THEME_PANEL_RADIUS;
inline constexpr float CardRadiusRatio = THEME_CARD_RADIUS_RATIO;

} // namespace theme

inline graphics::TextStyle Text(float size,
                                DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL)
{
    graphics::TextStyle style;

    style.size = size;
    style.weight = weight;
    return style;
}

inline graphics::TextStyle Centered(graphics::TextStyle style)
{
    style.align = DWRITE_TEXT_ALIGNMENT_CENTER;
    style.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    return style;
}

inline graphics::TextStyle Kai(float size)
{
    graphics::TextStyle style;

    style.size = size;
    style.family = graphics::FontFamily::Kai;
    style.weight = DWRITE_FONT_WEIGHT_BOLD;
    return style;
}

} // namespace pdk::ui
