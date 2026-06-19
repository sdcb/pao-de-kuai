#include "overlays/RoundResultOverlay.h"

#include "app/App.h"
#include "audio/SoundIds.h"
#include "core/StringUtil.h"

namespace pdk::overlays {

RoundResultOverlay::RoundResultOverlay(app::App& app, stats::RoundRecord record)
    : app_(app), record_(std::move(record)) {
    buttons_ = {
        {{470.0f, 500.0f, 160.0f, 48.0f}, "再来一局"},
        {{650.0f, 500.0f, 160.0f, 48.0f}, "主菜单"}
    };
}

void RoundResultOverlay::Update(float) {}

void RoundResultOverlay::Render(graphics::RenderContext& context) {
    context.FillRect({0.0f, 0.0f, 1280.0f, 720.0f}, scenes::Color(0.0f, 0.0f, 0.0f, 0.52f));
    scenes::DrawPanel(context, {350.0f, 150.0f, 580.0f, 440.0f}, scenes::Color(0.08f, 0.14f, 0.13f, 0.98f));
    const bool win = record_.winner == rules::PlayerId::Player;
    context.DrawTextUtf8(win ? "胜利" : "失败", {350.0f, 180.0f, 580.0f, 54.0f}, 38.0f, win ? scenes::Color(0.96f, 0.84f, 0.25f) : scenes::Color(0.78f, 0.86f, 0.92f), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    std::string text;
    text += "本局得分  玩家 ";
    core::AppendNumber(text, record_.scores[0]);
    text += "    AI1 ";
    core::AppendNumber(text, record_.scores[1]);
    text += "    AI2 ";
    core::AppendNumber(text, record_.scores[2]);
    text += "\n剩余牌数  玩家 ";
    core::AppendNumber(text, record_.remainingCards[0]);
    text += "    AI1 ";
    core::AppendNumber(text, record_.remainingCards[1]);
    text += "    AI2 ";
    core::AppendNumber(text, record_.remainingCards[2]);
    text += "\n炸弹次数  ";
    core::AppendNumber(text, record_.bombs.size());
    text += "    关圆鸡人数 ";
    core::AppendNumber(text, record_.spring.losers.size());
    text += "\n";
    if (!record_.bombs.empty()) {
        text += "炸弹固定分已计入，不参与春天翻倍\n";
    }
    if (record_.spring.enabled) {
        text += "触发关圆鸡 / 春天\n";
    }
    context.DrawTextUtf8(text, {420.0f, 260.0f, 440.0f, 180.0f}, 22.0f, scenes::Color(0.95f, 0.96f, 0.86f));
    scenes::ButtonGroup::DrawAll(context, buttons_);
}

bool RoundResultOverlay::OnMouseMove(float x, float y) {
    scenes::ButtonGroup::UpdateHover(buttons_, x, y);
    return true;
}

bool RoundResultOverlay::OnMouseDown(float x, float y) {
    const int hit = scenes::ButtonGroup::Hit(buttons_, x, y);
    if (hit == 0) {
        app_.Audio().Play(audio::SoundId::Confirm);
        app_.RestartCurrentGame();
    } else if (hit == 1) {
        app_.Audio().Play(audio::SoundId::Cancel);
        app_.ShowStart();
    }
    return true;
}

} // namespace pdk::overlays
