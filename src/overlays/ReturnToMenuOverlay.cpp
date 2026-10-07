#include "overlays/ReturnToMenuOverlay.h"

#include "app/App.h"
#include "audio/SoundIds.h"

namespace pdk::overlays {
namespace {

constexpr Rect Panel{400.0f, 226.0f, 480.0f, 256.0f};

} // namespace

ReturnToMenuOverlay::ReturnToMenuOverlay(app::App& app) : app_(app) {
    buttons_ = {
        {{Panel.x + 60.0f, Panel.y + 176.0f, 170.0f, 46.0f}, "回主菜单", ui::ButtonStyle::Secondary},
        {{Panel.x + 250.0f, Panel.y + 176.0f, 170.0f, 46.0f}, "继续游戏", ui::ButtonStyle::Primary}
    };
}

void ReturnToMenuOverlay::Update(float dt) {
    elapsed_ += dt;
    ui::ButtonGroup::UpdateAll(buttons_, dt);
}

void ReturnToMenuOverlay::Render(graphics::RenderContext& context) {
    ui::BeginModal(context, Panel, elapsed_);
    ui::DrawDialogBody(context, Panel, ui::Icon::Back, ui::theme::Gold, "返回主菜单？", "当前这一局不会被记录。");
    ui::ButtonGroup::DrawAll(context, buttons_);
    ui::EndModal(context);
}

bool ReturnToMenuOverlay::OnMouseMove(float x, float y) {
    ui::ButtonGroup::UpdateHover(buttons_, x, y);
    return true;
}

bool ReturnToMenuOverlay::OnMouseDown(float x, float y) {
    const int hit = ui::ButtonGroup::Hit(buttons_, x, y);
    if (hit == 0) {
        app_.Audio().Play(SOUND_CANCEL);
        app_.ShowStart();
        return true;
    }
    if (hit == 1) {
        app_.Audio().Play(SOUND_RESUME);
        app_.CloseTopOverlay();
        return true;
    }
    return true;
}

} // namespace pdk::overlays
