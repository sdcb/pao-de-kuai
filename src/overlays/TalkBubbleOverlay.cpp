#include "overlays/TalkBubbleOverlay.h"

#include "scenes/GameLayout.h"
#include "scenes/SceneCommon.h"

#include <algorithm>
#include <array>

namespace pdk::overlays {
namespace {

using namespace ui;

constexpr float MaxTextWidth = 290.0f;
constexpr float PadX = 16.0f;
constexpr float PadY = 11.0f;
constexpr float TailHeight = 9.0f;

graphics::TextStyle BubbleText() {
    graphics::TextStyle style = Text(15.5f);
    style.lineHeight = 22.0f;
    return style;
}

} // namespace

TalkBubbleOverlay::TalkBubbleOverlay(rules::PlayerId player, std::string text)
    : player_(player), text_(std::move(text)) {}

void TalkBubbleOverlay::Update(float dt) {
    elapsed_ += dt;
}

void TalkBubbleOverlay::Render(graphics::RenderContext& context) {
    const graphics::TextStyle style = BubbleText();
    const core::Size textSize = context.MeasureText(text_, style, MaxTextWidth);
    const float width = std::min(MaxTextWidth, textSize.width) + PadX * 2.0f;
    const float height = textSize.height + PadY * 2.0f;
    const core::Rect plate = scenes::layout::PlateFor(player_);
    const core::Point avatar = scenes::layout::AvatarCenter(player_);
    const float top = plate.y + plate.height + 10.0f + TailHeight;
    const float x = player_ == rules::PlayerId::Ai1 ? plate.x + 8.0f : plate.x + plate.width - 8.0f - width;
    const core::Rect bubble{x, top, width, height};
    const float tailX = std::clamp(avatar.x, bubble.x + 22.0f, bubble.x + bubble.width - 22.0f);

    const float pop = EaseOutBack(Progress(elapsed_, 0.0f, 0.28f), 1.6f);
    const float alpha = Clamp01(elapsed_ * 8.0f) * Clamp01((3.0f - elapsed_) / 0.4f);
    context.PushOpacity(alpha);
    context.PushScale(Lerp(0.82f, 1.0f, pop), {tailX, top - TailHeight});

    context.DrawShadow({bubble.x + 4.0f, bubble.y + 6.0f, bubble.width - 8.0f, bubble.height}, 9.0f, {0.0f, 0.0f, 0.0f, 0.45f});
    const D2D1_COLOR_F paper = Rgb(0xF6EEDB);
    context.FillRoundedRect(bubble, 14.0f, context.Linear({0.0f, bubble.y}, {0.0f, bubble.y + bubble.height},
        {{0.0f, paper}, {1.0f, Rgb(0xE9DDC2)}}));
    const std::array<core::Point, 3> tail{
        core::Point{tailX - 9.0f, top + 1.0f}, core::Point{tailX, top - TailHeight}, core::Point{tailX + 9.0f, top + 1.0f}};
    context.FillPolygon(tail, paper);
    context.StrokeRoundedRect({bubble.x + 0.5f, bubble.y + 0.5f, bubble.width - 1.0f, bubble.height - 1.0f}, 14.0f, WithAlpha(theme::GoldDeep, 0.55f), 1.0f);
    context.DrawTextUtf8(text_, {bubble.x + PadX, bubble.y + PadY, MaxTextWidth, textSize.height + 2.0f}, style, Rgb(0x1F2A24));

    context.PopTransform();
    context.PopOpacity();
}

} // namespace pdk::overlays
