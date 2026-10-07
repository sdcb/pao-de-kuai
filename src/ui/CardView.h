#pragma once

#include "core/Geometry.h"
#include "graphics/D2DContext.h"
#include "graphics/SpriteAtlas.h"
#include "rules/CppCompat.h"
#include "ui/Theme.h"

namespace pdk::ui {

struct CardLook {
    float lift{0.0f};
    float rotation{0.0f};
    float hover{0.0f};
    float glow{0.0f};
    D2D1_COLOR_F glowColor{theme::GoldLight};
    bool selected{false};
    float shadow{1.0f};
    float opacity{1.0f};
};

void DrawCardFace(graphics::RenderContext& context, graphics::SpriteAtlas& atlas, const rules::Card& card, const Rect& rect, const CardLook& look = {});
void DrawCardBack(graphics::RenderContext& context, graphics::SpriteAtlas& atlas, const Rect& rect, const CardLook& look = {});

} // namespace pdk::ui
