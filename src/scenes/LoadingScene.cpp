#include "scenes/LoadingScene.h"

#include "app/App.h"
#include "audio/SoundIds.h"
#include "scenes/GameScene.h"
#include "scenes/SceneCommon.h"
#include "scenes/StatsScene.h"

namespace pdk::scenes {

LoadingScene::LoadingScene(app::App& app, LoadingTarget target) : app_(app), target_(target) {}

void LoadingScene::OnEnter() {
    item_ = target_ == LoadingTarget::Game ? "加载牌图和 21 个 mp3 音效" : "加载统计数据";
    progress_ = 0.08f;
}

void LoadingScene::Update(float dt) {
    elapsed_ += dt;
    if (!loaded_ && elapsed_ > 0.12f) {
        progress_ = 0.80f;
        if (target_ == LoadingTarget::Game) {
            app_.LoadGameResources();
            app_.Audio().Play(audio::SoundId::RoundStart);
        }
        loaded_ = true;
        progress_ = 1.0f;
    }
    if (loaded_ && elapsed_ > 0.45f) {
        if (target_ == LoadingTarget::Game) {
            app_.ChangeScene(std::make_unique<GameScene>(app_));
        } else {
            app_.ChangeScene(std::make_unique<StatsScene>(app_));
        }
    }
}

void LoadingScene::Render(graphics::RenderContext& context) {
    context.Clear(Color(0.03f, 0.20f, 0.14f));
    context.DrawTextUtf8("加载中", {0.0f, 260.0f, 1280.0f, 50.0f}, 36.0f, Color(0.96f, 0.88f, 0.44f), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    context.DrawTextUtf8(item_, {0.0f, 318.0f, 1280.0f, 32.0f}, 20.0f, Color(0.86f, 0.93f, 0.82f), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    context.FillRect({390.0f, 380.0f, 500.0f, 18.0f}, Color(0.10f, 0.15f, 0.13f));
    context.FillRect({390.0f, 380.0f, 500.0f * progress_, 18.0f}, Color(0.86f, 0.72f, 0.28f));
}

} // namespace pdk::scenes
