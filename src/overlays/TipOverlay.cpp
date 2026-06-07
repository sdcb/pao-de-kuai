#include "overlays/TipOverlay.h"

#include "app/App.h"

namespace pdk::overlays {

TipOverlay::TipOverlay(app::App& app, std::string text) : app_(app), text_(std::move(text)) {}

void TipOverlay::Update(float) {}

void TipOverlay::Render(graphics::RenderContext& context) {
    scenes::DrawPanel(context, {410.0f, 78.0f, 460.0f, 76.0f}, scenes::Color(0.07f, 0.15f, 0.13f, 0.94f));
    context.DrawTextUtf8(text_, {432.0f, 92.0f, 416.0f, 48.0f}, 18.0f, scenes::Color(0.95f, 0.94f, 0.78f), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

bool TipOverlay::OnMouseDown(float, float) {
    app_.CloseTopOverlay();
    return false;
}

} // namespace pdk::overlays
