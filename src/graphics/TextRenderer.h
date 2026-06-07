#pragma once

#include "graphics/D2DContext.h"

namespace pdk::graphics {

class TextRenderer {
public:
    static void Draw(RenderContext& context, const std::string& text, const core::Rect& rect, float size, D2D1_COLOR_F color) {
        context.DrawTextUtf8(text, rect, size, color);
    }
};

} // namespace pdk::graphics
