#include "overlays/TalkBubbleOverlay.h"

#include "scenes/SceneCommon.h"

namespace pdk::overlays {
namespace {

constexpr core::Rect Ai1Area{95.0f, 24.0f, 400.0f, 132.0f};
constexpr core::Rect Ai2Area{785.0f, 24.0f, 400.0f, 132.0f};
constexpr float BubbleWidth = 280.0f;
constexpr float BubbleHeight = 58.0f;
constexpr float BubbleGap = 8.0f;

core::Rect BubbleRectFor(rules::PlayerId player) {
    const core::Rect area = player == rules::PlayerId::Ai1 ? Ai1Area : Ai2Area;
    const float x = player == rules::PlayerId::Ai1
        ? area.x
        : area.x + area.width - BubbleWidth;
    return {x, area.y + area.height + BubbleGap, BubbleWidth, BubbleHeight};
}

} // namespace

TalkBubbleOverlay::TalkBubbleOverlay(rules::PlayerId player, std::string text)
    : player_(player), text_(std::move(text)) {}

void TalkBubbleOverlay::Update(float dt) {
    elapsed_ += dt;
}

void TalkBubbleOverlay::Render(graphics::RenderContext& context) {
    const core::Rect rect = BubbleRectFor(player_);
    scenes::DrawPanel(context, rect, scenes::Color(0.97f, 0.94f, 0.74f, 0.96f));
    context.DrawTextUtf8(text_, {rect.x + 12.0f, rect.y + 8.0f, rect.width - 24.0f, rect.height - 16.0f}, 17.0f, scenes::Color(0.10f, 0.14f, 0.12f));
}

} // namespace pdk::overlays
