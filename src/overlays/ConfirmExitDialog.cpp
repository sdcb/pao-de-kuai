#include "overlays/ConfirmExitDialog.h"

#include "app/App.h"
#include "audio/SoundIds.h"

namespace pdk::overlays {
namespace {

constexpr Rect Panel{400.0f, 226.0f, 480.0f, 256.0f};

} // namespace

ConfirmExitDialog::ConfirmExitDialog(app::App& app) : app_(app) {
    buttons_ = {
        {{Panel.x + 60.0f, Panel.y + 176.0f, 170.0f, 46.0f}, "退出游戏", ui::ButtonStyle::Danger},
        {{Panel.x + 250.0f, Panel.y + 176.0f, 170.0f, 46.0f}, "再玩一会", ui::ButtonStyle::Secondary}
    };
}

void ConfirmExitDialog::Update(float dt) {
    elapsed_ += dt;
    ui::ButtonGroup::UpdateAll(buttons_, dt);
}

void ConfirmExitDialog::Render(graphics::RenderContext& context) {
    ui::BeginModal(context, Panel, elapsed_);
    ui::DrawDialogBody(context, Panel, ui::Icon::Exit, ui::theme::CinnabarLight, "确认退出游戏？", "设置会自动保存，下次再来。");
    ui::ButtonGroup::DrawAll(context, buttons_);
    ui::EndModal(context);
}

bool ConfirmExitDialog::OnMouseMove(float x, float y) {
    ui::ButtonGroup::UpdateHover(buttons_, x, y);
    return true;
}

bool ConfirmExitDialog::OnMouseDown(float x, float y) {
    const int hit = ui::ButtonGroup::Hit(buttons_, x, y);
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
