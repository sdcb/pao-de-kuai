#include "scenes/StartScene.h"

#include "app/App.h"
#include "audio/SoundIds.h"
#include "overlays/AboutOverlay.h"

#include <memory>

namespace pdk::scenes {
namespace {

constexpr D2D1_COLOR_F StartGreen = {0.04f, 0.30f, 0.18f, 1.0f};

} // namespace

StartScene::StartScene(app::App& app) : app_(app) {
    buttons_ = {
        {{520.0f, 230.0f, 240.0f, 50.0f}, "开始游戏"},
        {{520.0f, 290.0f, 240.0f, 50.0f}, "积分统计"},
        {{520.0f, 350.0f, 240.0f, 50.0f}, "设置"},
        {{520.0f, 410.0f, 240.0f, 50.0f}, "帮助"},
        {{520.0f, 470.0f, 240.0f, 50.0f}, "关于"},
        {{520.0f, 530.0f, 240.0f, 50.0f}, "退出"}
    };
}

void StartScene::Update(float) {}

void StartScene::Render(graphics::RenderContext& context) {
    context.Clear(StartGreen);
    context.FillRect({0.0f, 0.0f, 1280.0f, 720.0f}, StartGreen);
    context.DrawTextUtf8("极客版跑得快", {0.0f, 120.0f, 1280.0f, 72.0f}, 52.0f, Color(0.96f, 0.88f, 0.44f), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    context.DrawTextUtf8("单机三人场  48 张固定规则", {0.0f, 190.0f, 1280.0f, 34.0f}, 21.0f, Color(0.82f, 0.92f, 0.80f), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    for (const Button& button : buttons_) {
        DrawButton(context, button);
    }
}

bool StartScene::OnMouseMove(float x, float y) {
    UpdateButtonHover(buttons_, x, y);
    return true;
}

bool StartScene::OnMouseDown(float x, float y) {
    const int hit = HitButton(buttons_, x, y);
    if (hit < 0) {
        return false;
    }
    app_.Audio().Play(audio::SoundId::ButtonClick);
    if (hit == 0) {
        app_.StartGame();
    } else if (hit == 1) {
        app_.ShowStats();
    } else if (hit == 2) {
        app_.ShowSettings();
    } else if (hit == 3) {
        app_.ShowHelp();
    } else if (hit == 4) {
        app_.PushOverlay(std::make_unique<overlays::AboutOverlay>(app_));
    } else if (hit == 5) {
        app_.RequestClose();
    }
    return true;
}

} // namespace pdk::scenes
