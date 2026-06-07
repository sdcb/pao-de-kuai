#include "scenes/StatsScene.h"

#include "app/App.h"
#include "audio/SoundIds.h"

#include <sstream>

namespace pdk::scenes {

StatsScene::StatsScene(app::App& app) : app_(app) {
    buttons_ = {{{48.0f, 620.0f, 150.0f, 46.0f}, "返回"}};
}

void StatsScene::OnEnter() {
    stats::StatStore store;
    const std::string today = stats::TodayDateKey();
    today_ = store.SummarizeDay(today);
    month_ = store.SummarizeMonth(today.substr(0, 6));
    history_ = store.SummarizeHistory();
}

void StatsScene::Update(float) {}

void StatsScene::Render(graphics::RenderContext& context) {
    context.Clear(Color(0.03f, 0.19f, 0.14f));
    context.DrawTextUtf8("积分统计", {48.0f, 48.0f, 1180.0f, 50.0f}, 34.0f, Color(0.96f, 0.88f, 0.44f));

    auto block = [&](const char* title, const stats::StatSummary& summary, float x) {
        DrawPanel(context, {x, 135.0f, 350.0f, 390.0f});
        context.DrawTextUtf8(title, {x + 24.0f, 158.0f, 302.0f, 34.0f}, 25.0f, Color(0.96f, 0.88f, 0.44f));
        std::ostringstream text;
        text << "局数: " << summary.rounds << "\n\n";
        text << "玩家得分: " << summary.scores[0] << "\n";
        text << "AI1 得分: " << summary.scores[1] << "\n";
        text << "AI2 得分: " << summary.scores[2] << "\n\n";
        text << "炸弹次数: " << summary.bombs << "\n";
        text << "关圆鸡人数: " << summary.springLosers << "\n";
        text << "历史最高单局: " << summary.bestSingleRoundPlayerScore << "\n";
        context.DrawTextUtf8(text.str(), {x + 26.0f, 215.0f, 298.0f, 280.0f}, 22.0f, Color(0.88f, 0.94f, 0.84f));
    };
    block("今日", today_, 80.0f);
    block("本月", month_, 465.0f);
    block("历史", history_, 850.0f);

    ButtonGroup::DrawAll(context, buttons_);
}

bool StatsScene::OnMouseMove(float x, float y) {
    ButtonGroup::UpdateHover(buttons_, x, y);
    return true;
}

bool StatsScene::OnMouseDown(float x, float y) {
    if (ButtonGroup::Hit(buttons_, x, y) >= 0) {
        app_.Audio().Play(audio::SoundId::Cancel);
        app_.ShowStart();
        return true;
    }
    return false;
}

} // namespace pdk::scenes
