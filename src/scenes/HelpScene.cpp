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
    DrawPanel(context, {70.0f, 115.0f, 560.0f, 485.0f});
    DrawPanel(context, {650.0f, 115.0f, 560.0f, 485.0f});
    context.DrawTextUtf8("游戏规则", {105.0f, 138.0f, 500.0f, 34.0f}, 24.0f, Color(0.96f, 0.88f, 0.44f));
    context.DrawTextUtf8("界面与计分", {685.0f, 138.0f, 500.0f, 34.0f}, 24.0f, Color(0.96f, 0.88f, 0.44f));
    context.DrawTextUtf8(std::string(rules::SharedGameRulesText()), {105.0f, 178.0f, 500.0f, 385.0f}, 17.5f, Color(0.90f, 0.95f, 0.86f));
    context.DrawTextUtf8(std::string(rules::HumanHelpText()), {685.0f, 178.0f, 500.0f, 385.0f}, 20.0f, Color(0.90f, 0.95f, 0.86f));
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
