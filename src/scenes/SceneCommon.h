#pragma once

#include "audio/SoundIds.h"
#include "core/Geometry.h"
#include "graphics/D2DContext.h"
#include "graphics/SpriteAtlas.h"
#include "rules/CppCompat.h"
#include "ui/Anim.h"
#include "ui/CardView.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <string>
#include <vector>

namespace pdk::scenes {

using ui::Button;
using ui::ButtonGroup;
using ui::ButtonStyle;

inline Button MakeBackButton() {
    Button button{{28.0f, 28.0f, 112.0f, 44.0f}, "返回", ButtonStyle::Secondary, ui::Icon::Back, 18.0f};
    return button;
}

// Title row shared by the menu pages: back button on the left, title and subtitle beside it.
inline void DrawPageHeader(graphics::RenderContext& context, const std::string& title, const std::string& subtitle) {
    context.DrawTextUtf8(title, {164.0f, 22.0f, 600.0f, 40.0f}, ui::Kai(34.0f), ui::theme::GoldLight);
    context.DrawTextUtf8(subtitle, {166.0f, 62.0f, 800.0f, 22.0f}, ui::Text(14.5f), ui::theme::Muted);
    ui::DrawHairline(context, 40.0f, 1240.0f, 98.0f, 0.4f);
}

} // namespace pdk::scenes
