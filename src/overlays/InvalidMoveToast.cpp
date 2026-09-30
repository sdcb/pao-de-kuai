#include "overlays/InvalidMoveToast.h"

#include "scenes/SceneCommon.h"

namespace pdk::overlays {

using namespace ui;

InvalidMoveToast::InvalidMoveToast(std::string text) : text_(std::move(text)) {}

void InvalidMoveToast::Update(float dt) {
    elapsed_ += dt;
}

void InvalidMoveToast::Render(graphics::RenderContext& context) {
    const float in = EaseOutBack(Progress(elapsed_, 0.0f, 0.3f), 1.4f);
    const float alpha = Clamp01(elapsed_ * 8.0f) * Clamp01((2.0f - elapsed_) / 0.4f);
    context.PushOpacity(alpha);
    context.PushTranslation(0.0f, (1.0f - in) * -18.0f);
    ChipStyle chip;
    chip.fontSize = 16.0f;
    chip.height = 42.0f;
    chip.padX = 24.0f;
    chip.fill = WithAlpha(theme::CinnabarDeep, 0.94f);
    chip.stroke = WithAlpha(theme::CinnabarLight, 0.75f);
    core::Rect rect = ChipRect(context, {640.0f, 118.0f}, Anchor::Center, text_, chip);
    rect.x -= 14.0f;
    rect.width += 28.0f;
    context.DrawShadow({rect.x + 4.0f, rect.y + 6.0f, rect.width - 8.0f, rect.height}, 10.0f, {0.0f, 0.0f, 0.0f, 0.55f});
    DrawChip(context, rect, "", chip);
    DrawIcon(context, Icon::Alert, {rect.x + 18.0f, rect.y + 11.0f, 20.0f, 20.0f}, theme::Ivory);
    graphics::TextStyle style = Centered(Text(chip.fontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD));
    style.wrap = false;
    context.DrawTextUtf8(text_, {rect.x + 28.0f, rect.y, rect.width - 28.0f, rect.height}, style, theme::Ivory);
    context.PopTransform();
    context.PopOpacity();
}

} // namespace pdk::overlays
