#include "scenes/HelpScene.h"

#include "app/App.h"
#include "audio/SoundIds.h"
#include "rules/RuleText.h"

namespace pdk::scenes {

HelpScene::HelpScene(app::App& app) : app_(app) {
    buttons_ = {{{48.0f, 620.0f, 150.0f, 46.0f}, "返回"}};
}

void HelpScene::Update(float) {}

void HelpScene::Render(graphics::RenderContext& context) {
    context.Clear(Color(0.03f, 0.19f, 0.14f));
    context.DrawTextUtf8("帮助", {48.0f, 48.0f, 1180.0f, 50.0f}, 34.0f, Color(0.96f, 0.88f, 0.44f));
    DrawPanel(context, {95.0f, 130.0f, 1090.0f, 455.0f});
    const std::string text(rules::HelpRulesText());
    context.DrawTextUtf8(text, {135.0f, 165.0f, 1010.0f, 380.0f}, 22.0f, Color(0.90f, 0.95f, 0.86f));
    ButtonGroup::DrawAll(context, buttons_);
}

bool HelpScene::OnMouseMove(float x, float y) {
    ButtonGroup::UpdateHover(buttons_, x, y);
    return true;
}

bool HelpScene::OnMouseDown(float x, float y) {
    if (ButtonGroup::Hit(buttons_, x, y) >= 0) {
        app_.Audio().Play(audio::SoundId::Cancel);
        app_.ShowStart();
        return true;
    }
    return false;
}

} // namespace pdk::scenes
