#pragma once

/*
 * TEMPORARY TRANSITION HEADER -- DELETE WHEN THE PORT IS DONE.
 *
 * `src/ui/Icons.*` is pure C now (plan.md S7): the Icon enum class became ICON_* constants and
 * the drawing goes through the C RenderContext.
 *
 * The C++ callers still write `ui::Icon::Back` and `ui::DrawIcon(context, ...)`, so this header
 * reproduces exactly that.  It grows as the rest of src/ui is converted and disappears with the
 * last C++ file under src/.
 */

#include "graphics/CppCompat.h"
#include "ui/Icons.h"

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

} // namespace pdk::ui
