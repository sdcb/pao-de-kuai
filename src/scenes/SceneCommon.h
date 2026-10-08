#pragma once

/*
 * The little shared bits of the menu scenes: the back button every one of them shows, and the
 * title row the stat and help pages share.
 *
 * Pure C.  The C++ version was two inline functions in `namespace pdk::scenes`, so a scene that
 * wanted them also dragged in ui::Button / ui::ButtonGroup / ui/CppCompat.h.  Both functions are
 * plain C now and take the C RenderContext, which is what a converted scene has.
 *
 * `ui::Kai(size)` set DWRITE_FONT_WEIGHT_BOLD, so these call sites set `weight` explicitly:
 * TextStyle_Kai only carries the font family (its own default is NORMAL), and dropping the weight
 * here would silently render the page titles lighter than the C++ screenshots.
 */

#include "core/Geometry.h"
#include "graphics/D2DContext.h"
#include "ui/Widgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The 112x44 "返回" button every menu page puts at the top left. */
static inline Button SceneCommon_BackButton(void)
{
    Button button = Button_Make(Rect_Make(28.0f, 28.0f, 112.0f, 44.0f), "返回",
                                UI_BUTTON_SECONDARY);

    button.icon = UI_ICON_BACK;
    button.fontSize = 18.0f;
    return button;
}

/* Title row shared by the menu pages: back button on the left, title and subtitle beside it. */
static inline void SceneCommon_DrawPageHeader(RenderContext *context, const char *title,
                                             const char *subtitle)
{
    TextStyle titleStyle = TextStyle_Kai(34.0f);
    TextStyle subtitleStyle = TextStyle_Label(14.5f, 400 /* NORMAL */);
    Rect titleRect = Rect_Make(164.0f, 22.0f, 600.0f, 40.0f);
    Rect subtitleRect = Rect_Make(166.0f, 62.0f, 800.0f, 22.0f);

    titleStyle.weight = 700; /* BOLD, matching ui::Kai */
    RenderContext_DrawTextUtf8(context, title, &titleRect, &titleStyle, THEME_GOLD_LIGHT);
    RenderContext_DrawTextUtf8(context, subtitle, &subtitleRect, &subtitleStyle, THEME_MUTED);
    Widgets_DrawHairline(context, 40.0f, 1240.0f, 98.0f, 0.4f);
}

#ifdef __cplusplus
}
#endif
