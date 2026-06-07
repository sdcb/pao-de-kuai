#include "overlays/TalkBubbleOverlay.h"

#include "scenes/SceneCommon.h"

namespace pdk::overlays {

TalkBubbleOverlay::TalkBubbleOverlay(rules::PlayerId player, std::string text)
    : player_(player), text_(std::move(text)) {}

void TalkBubbleOverlay::Update(float dt) {
    elapsed_ += dt;
}

void TalkBubbleOverlay::Render(graphics::RenderContext& context) {
    const bool left = player_ == rules::PlayerId::Ai1;
    const core::Rect rect = left ? core::Rect{130.0f, 94.0f, 280.0f, 58.0f} : core::Rect{870.0f, 150.0f, 280.0f, 58.0f};
    scenes::DrawPanel(context, rect, scenes::Color(0.97f, 0.94f, 0.74f, 0.96f));
    context.DrawTextUtf8(text_, {rect.x + 12.0f, rect.y + 8.0f, rect.width - 24.0f, rect.height - 16.0f}, 17.0f, scenes::Color(0.10f, 0.14f, 0.12f));
}

} // namespace pdk::overlays
