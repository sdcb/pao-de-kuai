#include "scenes/HelpScene.h"

#include "app/App.h"
#include "audio/SoundIds.h"

namespace pdk::scenes {

HelpScene::HelpScene(app::App& app) : app_(app) {
    buttons_ = {{{48.0f, 620.0f, 150.0f, 46.0f}, "返回"}};
}

void HelpScene::Update(float) {}

void HelpScene::Render(graphics::RenderContext& context) {
    context.Clear(Color(0.03f, 0.19f, 0.14f));
    context.DrawTextUtf8("帮助", {48.0f, 48.0f, 1180.0f, 50.0f}, 34.0f, Color(0.96f, 0.88f, 0.44f));
    DrawPanel(context, {95.0f, 130.0f, 1090.0f, 455.0f});
    const std::string text =
        "目标：先把手牌跑完。\n"
        "黑桃 3 玩家先出，但第一手不强制包含黑桃 3。\n"
        "牌型：单张、对子、至少两连对、三带一、三带二、飞机、至少五张顺子、炸弹。\n"
        "顺子不能使用 2；10JQKA 可以，JQKA2 和 A2345 不可以。\n"
        "飞机为至少两组连续三张，可带每组三张数量到两倍数量的零牌，只比三张主体。\n"
        "炸弹为四张同点数，4 个 3 最小，4 个 K 最大；炸弹可压非炸弹。\n"
        "三带二可以带两张零牌，不要求对子；炸弹只能按炸弹出，不能当四带三。\n"
        "最后一手牌不足带牌时，三张主体可以直接出完。\n"
        "要得起必须出，不能不要；提示如果打不起会直接不要。\n"
        "计分：赢家获得两家有效剩余牌数，剩一张不扣分；关圆鸡每人扣 32。\n"
        "炸弹立即 +20，另外两家各 -10，结算也会展示且不参与春天翻倍。\n"
        "托管会用基础 AI 代替玩家行动，可随时取消。";
    context.DrawTextUtf8(text, {135.0f, 165.0f, 1010.0f, 380.0f}, 22.0f, Color(0.90f, 0.95f, 0.86f));
    for (const Button& button : buttons_) {
        DrawButton(context, button);
    }
}

bool HelpScene::OnMouseMove(float x, float y) {
    UpdateButtonHover(buttons_, x, y);
    return true;
}

bool HelpScene::OnMouseDown(float x, float y) {
    if (HitButton(buttons_, x, y) >= 0) {
        app_.Audio().Play(audio::SoundId::Cancel);
        app_.ShowStart();
        return true;
    }
    return false;
}

} // namespace pdk::scenes
