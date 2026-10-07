#include "overlays/TipOverlay.h"

#include "app/App.h"

namespace pdk::overlays {

using namespace ui;

TipOverlay::TipOverlay(app::App& app, std::string text) : app_(app), text_(std::move(text)) {}

void TipOverlay::Update(float dt) {
    elapsed_ += dt;
}

void TipOverlay::Render(graphics::RenderContext& context) {
    const float t = EaseOutCubic(Progress(elapsed_, 0.0f, 0.35f));
    context.PushOpacity(t);
    context.PushTranslation(0.0f, (1.0f - t) * -12.0f);
    ChipStyle chip;
    chip.fontSize = 16.0f;
    chip.height = 44.0f;
    chip.padX = 26.0f;
    chip.fill = WithAlpha(theme::Ink, 0.94f);
    chip.stroke = WithAlpha(theme::Gold, 0.6f);
    chip.text = theme::Ivory;
    Rect rect = ChipRect(context, {640.0f, 196.0f}, Anchor::Center, text_, chip);
    rect.x -= 14.0f;
    rect.width += 28.0f;
    context.DrawShadow({rect.x + 4.0f, rect.y + 6.0f, rect.width - 8.0f, rect.height}, 10.0f, {0.0f, 0.0f, 0.0f, 0.5f});
    DrawChip(context, rect, "", chip);
    DrawIcon(context, Icon::Sparkle, {rect.x + 20.0f, rect.y + 13.0f, 18.0f, 18.0f}, theme::Gold);
    graphics::TextStyle style = Centered(Text(chip.fontSize));
    style.wrap = false;
    context.DrawTextUtf8(text_, {rect.x + 28.0f, rect.y, rect.width - 28.0f, rect.height}, style, theme::Ivory);
    context.PopTransform();
    context.PopOpacity();
}

bool TipOverlay::OnMouseDown(float, float) {
    app_.CloseTopOverlay();
    return false;
}

} // namespace pdk::overlays
