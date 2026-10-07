#include "scenes/LoadingScene.h"

#include "app/App.h"
#include "audio/SoundIds.h"
#include "scenes/GameScene.h"
#include "scenes/SceneCommon.h"
#include "scenes/StatsScene.h"

namespace pdk::scenes {

using namespace ui;

LoadingScene::LoadingScene(app::App& app, LoadingTarget target) : app_(app), target_(target) {}

void LoadingScene::OnEnter() {
    item_ = target_ == LoadingTarget::Game ? "正在铺开牌桌，准备牌图与音效" : "正在整理战绩";
    progress_ = 0.08f;
}

void LoadingScene::Update(float dt) {
    elapsed_ += dt;
    shownProgress_ = Approach(shownProgress_, progress_, 10.0f, dt);
    if (!loaded_ && elapsed_ > 0.12f) {
        progress_ = 0.80f;
        if (target_ == LoadingTarget::Game) {
            app_.LoadGameResources();
            app_.Audio().Play(SOUND_ROUND_START);
        }
        loaded_ = true;
        progress_ = 1.0f;
    }
    if (loaded_ && elapsed_ > 0.45f) {
        if (target_ == LoadingTarget::Game) {
            app_.ChangeScene(core::Transfer(new GameScene(app_)));
        } else {
            app_.ChangeScene(core::Transfer(new StatsScene(app_)));
        }
    }
}

void LoadingScene::Render(graphics::RenderContext& context) {
    context.Clear(theme::Room);
    DrawRoomBackground(context, {640.0f, 330.0f});
    DrawRadialGlow(context, {640.0f, 300.0f}, 260.0f, WithAlpha(theme::Gold, 0.06f));

    graphics::TextStyle title = Centered(Kai(76.0f));
    title.wrap = false;
    const Rect titleRect{0.0f, 230.0f, 1280.0f, 100.0f};
    context.DrawTextUtf8("跑得快", titleRect, title, context.Linear({0.0f, 250.0f}, {0.0f, 320.0f},
        {{0.0f, theme::GoldLight}, {0.6f, theme::Gold}, {1.0f, theme::GoldDeep}}));
    DrawSeal(context, {782.0f, 256.0f}, 40.0f, "极\n客", theme::Cinnabar, -6.0f, 16.0f);

    DrawProgressBar(context, {500.0f, 372.0f, 280.0f, 5.0f}, shownProgress_, elapsed_);
    context.DrawTextUtf8(item_, {0.0f, 392.0f, 1280.0f, 28.0f}, Centered(Text(15.0f)), theme::Muted);
}

} // namespace pdk::scenes
