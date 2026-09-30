#include "scenes/StatsScene.h"

#include "app/App.h"
#include "audio/SoundIds.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace pdk::scenes {
namespace {

using namespace ui;

constexpr float CardTop = 128.0f;
constexpr float CardWidth = 372.0f;
constexpr float CardHeight = 462.0f;
constexpr float CardGap = 22.0f;

std::string Signed(int value) {
    std::string text = value > 0 ? "+" : "";
    core::AppendNumber(text, value);
    return text;
}

std::string Number(int value) {
    std::string text;
    core::AppendNumber(text, value);
    return text;
}

} // namespace

StatsScene::StatsScene(app::App& app) : app_(app) {
    buttons_ = {MakeBackButton()};
}

void StatsScene::OnEnter() {
    stats::StatStore store;
    const std::string today = stats::TodayDateKey();
    today_ = store.SummarizeDay(today);
    month_ = store.SummarizeMonth(today.substr(0, 6));
    history_ = store.SummarizeHistory();
}

void StatsScene::Update(float dt) {
    elapsed_ += dt;
    ButtonGroup::UpdateAll(buttons_, dt);
}

void StatsScene::Render(graphics::RenderContext& context) {
    context.Clear(theme::Room);
    DrawRoomBackground(context, {640.0f, 300.0f});
    DrawPageHeader(context, "战绩", "每一局都单独记录，今日、本月与历史从记录实时汇总");

    const std::string playerName = app_.Settings().playerName.empty() ? std::string("玩家") : app_.Settings().playerName;
    const std::array<std::string, 3> names{playerName, "AI1", "AI2"};

    auto block = [&](int index, const char* title, const stats::StatSummary& summary) {
        const float appear = EaseOutCubic(Progress(elapsed_, 0.05f + static_cast<float>(index) * 0.08f, 0.5f));
        const float grow = EaseOutCubic(Progress(elapsed_, 0.3f + static_cast<float>(index) * 0.08f, 0.8f));
        const float totalWidth = CardWidth * 3.0f + CardGap * 2.0f;
        const core::Rect card{640.0f - totalWidth * 0.5f + static_cast<float>(index) * (CardWidth + CardGap), CardTop, CardWidth, CardHeight};
        context.PushOpacity(appear);
        context.PushTranslation(0.0f, (1.0f - appear) * 18.0f);
        DrawPanel(context, card);

        const float left = card.x + 30.0f;
        const float right = card.x + card.width - 30.0f;
        context.DrawTextUtf8(title, {left, card.y + 24.0f, 200.0f, 36.0f}, Kai(27.0f), theme::GoldLight);
        ChipStyle rounds;
        rounds.fontSize = 13.5f;
        rounds.height = 26.0f;
        DrawChip(context, {right, card.y + 43.0f}, Anchor::Right, Number(summary.rounds) + " 局", rounds);

        const int myScore = RoundToInt(static_cast<float>(summary.scores[0]) * grow);
        context.DrawTextUtf8(Signed(myScore), {left, card.y + 76.0f, 300.0f, 76.0f}, Text(60.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD), ScoreColor(summary.scores[0]));
        context.DrawTextUtf8("我的得分", {left + 2.0f, card.y + 152.0f, 200.0f, 20.0f}, Text(13.0f), theme::Faint);
        DrawHairline(context, card.x + 16.0f, card.x + card.width - 16.0f, card.y + 190.0f, 0.35f);

        context.DrawTextUtf8("三家对比", {left, card.y + 204.0f, 200.0f, 20.0f}, Text(13.0f), theme::Faint);
        int maxAbs = 1;
        for (int score : summary.scores) {
            maxAbs = std::max(maxAbs, std::abs(score));
        }
        const float barLeft = left + 74.0f;
        const float barRight = right - 58.0f;
        const float zero = (barLeft + barRight) * 0.5f;
        const float half = (barRight - barLeft) * 0.5f;
        for (int i = 0; i < 3; ++i) {
            const float rowY = card.y + 236.0f + static_cast<float>(i) * 38.0f;
            graphics::TextStyle nameStyle = Text(14.5f);
            nameStyle.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
            nameStyle.wrap = false;
            nameStyle.ellipsis = true;
            context.DrawTextUtf8(names[static_cast<std::size_t>(i)], {left, rowY, 68.0f, 26.0f}, nameStyle, i == 0 ? theme::Ivory : theme::Muted);
            context.FillRoundedRect({barLeft, rowY + 9.0f, barRight - barLeft, 8.0f}, 4.0f, WithAlpha(theme::Ink, 0.9f));
            context.DrawLine({zero, rowY + 5.0f}, {zero, rowY + 21.0f}, WithAlpha(theme::Gold, 0.35f), 1.0f);
            const int score = summary.scores[static_cast<std::size_t>(i)];
            const float length = half * static_cast<float>(std::abs(score)) / static_cast<float>(maxAbs) * grow;
            if (length > 0.5f) {
                const D2D1_COLOR_F color = ScoreColor(score);
                const core::Rect bar = score >= 0
                    ? core::Rect{zero, rowY + 9.0f, length, 8.0f}
                    : core::Rect{zero - length, rowY + 9.0f, length, 8.0f};
                context.FillRoundedRect(bar, 4.0f, context.Linear({bar.x, bar.y}, {bar.x + bar.width, bar.y},
                    score >= 0 ? std::initializer_list<graphics::GradientStop>{{0.0f, WithAlpha(color, 0.45f)}, {1.0f, color}}
                               : std::initializer_list<graphics::GradientStop>{{0.0f, color}, {1.0f, WithAlpha(color, 0.45f)}}));
            }
            graphics::TextStyle valueStyle = Text(14.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            valueStyle.align = DWRITE_TEXT_ALIGNMENT_TRAILING;
            valueStyle.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
            context.DrawTextUtf8(Signed(score), {right - 56.0f, rowY, 56.0f, 26.0f}, valueStyle, ScoreColor(score));
        }
        DrawHairline(context, card.x + 16.0f, card.x + card.width - 16.0f, card.y + 358.0f, 0.35f);

        const std::array<std::pair<const char*, int>, 3> minis{{
            {"炸弹", summary.bombs}, {"关圆鸡", summary.springLosers}, {"最高单局", summary.bestSingleRoundPlayerScore}}};
        const float cellWidth = (right - left) / 3.0f;
        for (std::size_t i = 0; i < minis.size(); ++i) {
            const float cx = left + cellWidth * static_cast<float>(i);
            context.DrawTextUtf8(Number(minis[i].second), {cx, card.y + 376.0f, cellWidth, 34.0f},
                Centered(Text(26.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD)), theme::Ivory);
            context.DrawTextUtf8(minis[i].first, {cx, card.y + 412.0f, cellWidth, 20.0f}, Centered(Text(13.0f)), theme::Faint);
            if (i > 0) {
                context.DrawLine({cx, card.y + 382.0f}, {cx, card.y + 428.0f}, WithAlpha(theme::Gold, 0.15f), 1.0f);
            }
        }
        context.PopTransform();
        context.PopOpacity();
    };
    block(0, "今日", today_);
    block(1, "本月", month_);
    block(2, "历史", history_);

    context.DrawTextUtf8("记录保存在程序运行目录的 stat 文件夹", {0.0f, 618.0f, 1280.0f, 22.0f}, Centered(Text(13.0f)), WithAlpha(theme::Faint, 0.85f));
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
