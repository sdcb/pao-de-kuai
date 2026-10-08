#pragma once

/*
 * Vector icons drawn inside a square-ish rect; emoji do not render in colour on this target.
 *
 * Pure C.  The `Icon` enum class became a uint8_t plus ICON_* constants (the same treatment the
 * rules layer's enums got), the std::array vertex lists became plain arrays, and the
 * RenderContext was already converted so the drawing calls are the C API.
 *
 * ui/CppCompat.h keeps the `ui::Icon::Back` / `ui::DrawIcon` spelling the C++ callers use.
 */

#include "core/Geometry.h"
#include "graphics/D2DContext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t Icon;

enum {
    UI_ICON_NONE = 0,
    UI_ICON_BACK = 1,
    UI_ICON_CLOSE = 2,
    UI_ICON_STAR = 3,
    UI_ICON_EXIT = 4,
    UI_ICON_ALERT = 5,
    UI_ICON_SPARKLE = 6
};

void Icons_Draw(RenderContext *context, Icon icon, const Rect *rect, D2D1_COLOR_F color);

#ifdef __cplusplus
}
#endif
