#include "overlays/InvalidMoveToast.h"

#include "scenes/SceneCommon.h"

namespace pdk::overlays {

InvalidMoveToast::InvalidMoveToast(std::string text) : text_(std::move(text)) {}

void InvalidMoveToast::Update(float dt) {
    elapsed_ += dt;
}

void InvalidMoveToast::Render(graphics::RenderContext& context) {
    const float alpha = elapsed_ < 1.6f ? 0.90f : (2.0f - elapsed_) / 0.4f;
    context.FillRect({390.0f, 92.0f, 500.0f, 46.0f}, scenes::Color(0.24f, 0.08f, 0.06f, alpha));
    context.DrawTextUtf8(text_, {410.0f, 98.0f, 460.0f, 34.0f}, 18.0f, scenes::Color(1.0f, 0.92f, 0.82f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

} // namespace pdk::overlays
