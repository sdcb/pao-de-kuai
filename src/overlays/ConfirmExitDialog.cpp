#include "overlays/ConfirmExitDialog.h"

#include "app/App.h"
#include "audio/SoundIds.h"

namespace pdk::overlays {

ConfirmExitDialog::ConfirmExitDialog(app::App& app) : app_(app) {
    buttons_ = {
        {{470.0f, 420.0f, 150.0f, 48.0f}, "确认退出"},
        {{660.0f, 420.0f, 150.0f, 48.0f}, "取消"}
    };
}

void ConfirmExitDialog::Update(float) {}

void ConfirmExitDialog::Render(graphics::RenderContext& context) {
    context.FillRect({0.0f, 0.0f, 1280.0f, 720.0f}, scenes::Color(0.0f, 0.0f, 0.0f, 0.48f));
    scenes::DrawPanel(context, {380.0f, 250.0f, 520.0f, 260.0f}, scenes::Color(0.07f, 0.13f, 0.12f, 0.96f));
    context.DrawTextUtf8("确认关闭窗口？", {380.0f, 300.0f, 520.0f, 60.0f}, 30.0f, scenes::Color(0.98f, 0.96f, 0.82f), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    for (const auto& button : buttons_) {
        scenes::DrawButton(context, button);
    }
}

bool ConfirmExitDialog::OnMouseMove(float x, float y) {
    scenes::UpdateButtonHover(buttons_, x, y);
    return true;
}

bool ConfirmExitDialog::OnMouseDown(float x, float y) {
    const int hit = scenes::HitButton(buttons_, x, y);
    if (hit == 0) {
        app_.Audio().Play(audio::SoundId::Confirm);
        app_.ConfirmExit();
        return true;
    }
    if (hit == 1) {
        app_.Audio().Play(audio::SoundId::Cancel);
        app_.CloseTopOverlay();
        return true;
    }
    return true;
}

} // namespace pdk::overlays
