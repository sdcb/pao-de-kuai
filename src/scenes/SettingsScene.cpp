#include "scenes/SettingsScene.h"

#include "app/App.h"
#include "audio/SoundIds.h"

#include <algorithm>
#include <sstream>

namespace pdk::scenes {

SettingsScene::SettingsScene(app::App& app) : app_(app) {
    buttons_ = {
        {{48.0f, 620.0f, 150.0f, 46.0f}, "返回"},
        {{1080.0f, 620.0f, 150.0f, 46.0f}, "保存"}
    };
}

void SettingsScene::OnEnter() {
    draft_ = app_.Settings();
}

void SettingsScene::Update(float) {}

void SettingsScene::Render(graphics::RenderContext& context) {
    context.Clear(Color(0.03f, 0.19f, 0.14f));
    context.DrawTextUtf8("设置", {48.0f, 48.0f, 1180.0f, 50.0f}, 34.0f, Color(0.96f, 0.88f, 0.44f));
    DrawPanel(context, {270.0f, 140.0f, 740.0f, 370.0f});

    context.DrawTextUtf8("音量大小", {320.0f, 218.0f, 150.0f, 36.0f}, 22.0f, Color(0.88f, 0.94f, 0.84f));
    DrawSlider(context, {390.0f, 218.0f, 500.0f, 36.0f}, std::clamp(draft_.masterVolume, 0.0f, 1.0f));
    context.DrawTextUtf8("牌大小和动画速度使用固定手感，不再写入 appsettings.json", {320.0f, 330.0f, 640.0f, 32.0f}, 18.0f, Color(0.70f, 0.82f, 0.74f));
    context.DrawTextUtf8("玩家名从 appsettings.json 读取，默认 李姐", {320.0f, 375.0f, 640.0f, 32.0f}, 18.0f, Color(0.70f, 0.82f, 0.74f));

    for (const Button& button : buttons_) {
        DrawButton(context, button);
    }
}

bool SettingsScene::OnMouseMove(float x, float y) {
    UpdateButtonHover(buttons_, x, y);
    return true;
}

bool SettingsScene::OnMouseDown(float x, float y) {
    const int hit = HitButton(buttons_, x, y);
    if (hit == 0) {
        app_.Audio().Play(audio::SoundId::Cancel);
        app_.ShowStart();
        return true;
    }
    if (hit == 1) {
        app_.Settings() = draft_;
        app_.Audio().SetMasterVolume(draft_.masterVolume);
        app_.SaveSettings();
        app_.Audio().Play(audio::SoundId::Confirm);
        app_.ShowStart();
        return true;
    }
    const core::Rect volumeSlider{390.0f, 218.0f, 500.0f, 36.0f};
    if (volumeSlider.Contains(x, y)) {
        draft_.masterVolume = std::clamp((x - volumeSlider.x) / volumeSlider.width, 0.0f, 1.0f);
        app_.Audio().SetMasterVolume(draft_.masterVolume);
        app_.Audio().Play(audio::SoundId::ButtonClick);
    }
    return true;
}

} // namespace pdk::scenes
