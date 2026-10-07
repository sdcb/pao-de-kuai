#pragma once

#include "core/Geometry.h"
#include "graphics/CppCompat.h"

namespace pdk::ui {

enum class Icon {
    None,
    Back,
    Close,
    Star,
    Exit,
    Alert,
    Sparkle
};

// Vector icons drawn inside a square-ish rect; emoji do not render in colour on this target.
void DrawIcon(graphics::RenderContext& context, Icon icon, const Rect& rect, D2D1_COLOR_F color);

} // namespace pdk::ui
