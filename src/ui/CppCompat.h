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
#include "ui/Inputs.h"
#include "ui/Widgets.h"
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

/* ---- widgets --------------------------------------------------------- */

enum class ButtonStyle {
    Primary = UI_BUTTON_PRIMARY,
    Secondary = UI_BUTTON_SECONDARY,
    Ghost = UI_BUTTON_GHOST,
    Danger = UI_BUTTON_DANGER,
    Icon = UI_BUTTON_ICON
};

enum class Anchor {
    Left = UI_ANCHOR_LEFT,
    Center = UI_ANCHOR_CENTER,
    Right = UI_ANCHOR_RIGHT
};

using ::Button;
using ::ChipStyle;
using ::PanelStyle;

/*
 * Buttons are still aggregate-initialised positionally at the overlays
 * (`{{rect}, "退出游戏", ui::ButtonStyle::Danger}`), which a user-provided constructor would
 * break, so the C struct has none and the defaults come from here instead.
 */
inline Button MakeButton(const Rect& rect, const char* text, ButtonStyle style)
{
    Button button;

    Button_Init(&button);
    button.rect = rect;
    Str_CopyTo(button.text, PDK_BUTTON_TEXT_CAP, text);
    button.style = static_cast<::ButtonStyle>(style);
    return button;
}

struct ButtonGroup {
    static void DrawAll(graphics::RenderContext& context, const std::vector<Button>& buttons)
    {
        ButtonGroup_DrawAll(context.Native(), buttons.data(), static_cast<int>(buttons.size()));
    }
    static void UpdateAll(std::vector<Button>& buttons, float dt)
    {
        ButtonGroup_UpdateAll(buttons.data(), static_cast<int>(buttons.size()), dt);
    }
    // Returns the hit index and starts that button's press animation.
    static int Hit(std::vector<Button>& buttons, float x, float y)
    {
        return ButtonGroup_Hit(buttons.data(), static_cast<int>(buttons.size()), x, y);
    }
    static void UpdateHover(std::vector<Button>& buttons, float x, float y)
    {
        ButtonGroup_UpdateHover(buttons.data(), static_cast<int>(buttons.size()), x, y);
    }
};

/* The C API has no default arguments, so they live here. */
inline void DrawPanel(graphics::RenderContext& context, const Rect& rect,
                      const PanelStyle& style = PanelStyle())
{
    Widgets_DrawPanel(context.Native(), &rect, &style);
}

inline void DrawOrnamentCorners(graphics::RenderContext& context, const Rect& rect,
                                D2D1_COLOR_F color, float size = 16.0f, float inset = 9.0f)
{
    Widgets_DrawOrnamentCorners(context.Native(), &rect, color, size, inset);
}

// Horizontal gold line that fades out at both ends.
inline void DrawHairline(graphics::RenderContext& context, float x0, float x1, float y,
                         float alpha = 0.55f)
{
    Widgets_DrawHairline(context.Native(), x0, x1, y, alpha);
}

inline Rect ChipRect(graphics::RenderContext& context, Point anchor, Anchor align,
                     const std::string& text, const ChipStyle& style)
{
    return Widgets_ChipRect(context.Native(), anchor, static_cast<::Anchor>(align), text.c_str(),
                            &style);
}

inline void DrawChip(graphics::RenderContext& context, const Rect& rect, const std::string& text,
                     const ChipStyle& style)
{
    Widgets_DrawChipInRect(context.Native(), &rect, text.c_str(), &style);
}

inline Rect DrawChip(graphics::RenderContext& context, Point anchor, Anchor align,
                     const std::string& text, const ChipStyle& style = ChipStyle())
{
    return Widgets_DrawChip(context.Native(), anchor, static_cast<::Anchor>(align), text.c_str(),
                            &style);
}

// First visible character for avatars: one CJK glyph, or up to three ASCII characters.
inline std::string AvatarLabel(const std::string& name)
{
    char out[PDK_BUTTON_TEXT_CAP];

    Widgets_AvatarLabel(name.c_str(), out, PDK_BUTTON_TEXT_CAP);
    return std::string(out);
}

inline void DrawAvatar(graphics::RenderContext& context, const Rect& rect,
                       const std::string& label, bool active, float time)
{
    Widgets_DrawAvatar(context.Native(), &rect, label.c_str(), active, time);
}

// Soft circular glow; ring > 0 turns it into a halo that peaks at that fraction of the radius.
inline void DrawRadialGlow(graphics::RenderContext& context, Point center, float radius,
                           D2D1_COLOR_F color, float ring = 0.0f)
{
    Widgets_DrawRadialGlow(context.Native(), center, radius, color, ring);
}

inline void DrawSeal(graphics::RenderContext& context, Point center, float size,
                     const std::string& text, D2D1_COLOR_F color, float rotation, float fontSize)
{
    Widgets_DrawSeal(context.Native(), center, size, text.c_str(), color, rotation, fontSize);
}

inline void DrawProgressBar(graphics::RenderContext& context, const Rect& rect, float value,
                            float time)
{
    Widgets_DrawProgressBar(context.Native(), &rect, value, time);
}

// Full-screen velvet backdrop used by the menu scenes.
inline void DrawRoomBackground(graphics::RenderContext& context, Point focus)
{
    Widgets_DrawRoomBackground(context.Native(), focus);
}

inline void DrawVignette(graphics::RenderContext& context, float strength)
{
    Widgets_DrawVignette(context.Native(), strength);
}

inline void DrawBackdrop(graphics::RenderContext& context, float alpha)
{
    Widgets_DrawBackdrop(context.Native(), alpha);
}

// Compact confirm dialog: panel, round icon badge, title and one line of explanation.
inline void DrawDialogBody(graphics::RenderContext& context, const Rect& panel, Icon icon,
                           D2D1_COLOR_F accent, const std::string& title,
                           const std::string& subtitle)
{
    Widgets_DrawDialogBody(context.Native(), &panel, static_cast<::Icon>(icon), accent,
                           title.c_str(), subtitle.c_str());
}

// Modal enter animation: dims the scene, then fades and springs the panel in.
inline void BeginModal(graphics::RenderContext& context, const Rect& panel, float elapsed)
{
    Widgets_BeginModal(context.Native(), &panel, elapsed);
}

inline void EndModal(graphics::RenderContext& context)
{
    Widgets_EndModal(context.Native());
}

/* ---- form widgets ---------------------------------------------------- */

/*
 * src/ui/Inputs.* is pure C now (plan.md S7).  The four types keep their old spellings through
 * plain `using` declarations -- every C entry point takes the widget as its first pointer
 * argument, so the converted call sites read the same as the old method calls and there is
 * nothing to wrap.  The one shape that changed is `TextField::Utf8()`, which returned a
 * std::string and is now TextField_Utf8 (caller-owned Str) / TextField_Utf8To (fixed buffer);
 * SettingsOverlay picks the buffer form for its 64-byte player name.
 */
using ::Segmented;
using ::Slider;
using ::TextField;
using ::Toggle;

} // namespace pdk::ui
