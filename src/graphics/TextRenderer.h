#pragma once

#include "graphics/CppCompat.h"

namespace pdk::graphics {

class TextRenderer {
public:
    static void Draw(RenderContext& context, const std::string& text, const Rect& rect, float size, D2D1_COLOR_F color) {
        context.DrawTextUtf8(text, rect, size, color);
    }
};

} // namespace pdk::graphics
