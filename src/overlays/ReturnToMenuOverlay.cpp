#include "overlays/ReturnToMenuOverlay.h"

#include "app/App.h"
#include "audio/SoundIds.h"

namespace pdk::overlays {

ReturnToMenuOverlay::ReturnToMenuOverlay(app::App& app) : app_(app) {
    buttons_ = {
        {{470.0f, 420.0f, 150.0f, 48.0f}, "回主菜单"},
        {{660.0f, 420.0f, 150.0f, 48.0f}, "继续"}
    };
}

void ReturnToMenuOverlay::Update(float) {}

void ReturnToMenuOverlay::Render(graphics::RenderContext& context) {
    context.FillRect({0.0f, 0.0f, 1280.0f, 720.0f}, scenes::Color(0.0f, 0.0f, 0.0f, 0.42f));
    scenes::DrawPanel(context, {380.0f, 250.0f, 520.0f, 260.0f}, scenes::Color(0.07f, 0.13f, 0.12f, 0.96f));
    context.DrawTextUtf8("返回游戏菜单？", {380.0f, 300.0f, 520.0f, 60.0f}, 30.0f, scenes::Color(0.98f, 0.96f, 0.82f), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    for (const auto& button : buttons_) {
        scenes::DrawButton(context, button);
    }
}

bool ReturnToMenuOverlay::OnMouseMove(float x, float y) {
    scenes::UpdateButtonHover(buttons_, x, y);
    return true;
}

bool ReturnToMenuOverlay::OnMouseDown(float x, float y) {
    const int hit = scenes::HitButton(buttons_, x, y);
    if (hit == 0) {
        app_.Audio().Play(audio::SoundId::Cancel);
        app_.ShowStart();
        return true;
    }
    if (hit == 1) {
        app_.Audio().Play(audio::SoundId::Resume);
        app_.CloseTopOverlay();
        return true;
    }
    return true;
}

} // namespace pdk::overlays
