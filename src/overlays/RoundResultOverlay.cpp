#include "overlays/RoundResultOverlay.h"

#include "app/App.h"
#include "audio/SoundIds.h"
#include "core/CppCompat.h"

#include <array>
#include <cmath>

namespace pdk::overlays {
namespace {

using namespace ui;

constexpr Rect Panel{330.0f, 128.0f, 620.0f, 470.0f};
constexpr float TableLeft = 384.0f;
constexpr float TableRight = 896.0f;
constexpr float HeaderY = 322.0f;
constexpr float RowTop = 346.0f;
constexpr float RowHeight = 46.0f;

std::string Signed(int value) {
    std::string text = value > 0 ? "+" : "";
    core::AppendNumber(text, value);
    return text;
}

} // namespace

RoundResultOverlay::RoundResultOverlay(app::App& app, stats::RoundRecord record)
    : app_(app), record_(std::move(record)) {
    buttons_ = {
        {{466.0f, 524.0f, 168.0f, 48.0f}, "再来一局", ButtonStyle::Primary},
        {{646.0f, 524.0f, 168.0f, 48.0f}, "主菜单", ButtonStyle::Secondary}
    };
}

void RoundResultOverlay::Update(float dt) {
    elapsed_ += dt;
    ButtonGroup::UpdateAll(buttons_, dt);
}

void RoundResultOverlay::Render(graphics::RenderContext& context) {
    const bool win = record_.winner == PLAYER_HUMAN;
    BeginModal(context, Panel, elapsed_);
    DrawPanel(context, Panel);
    if (win) {
        DrawRadialGlow(context, {640.0f, 150.0f}, 190.0f, WithAlpha(theme::Gold, 0.16f));
    }

    const float stamp = EaseOutBackWith(Progress(elapsed_, 0.12f, 0.35f), 1.8f);
    context.PushOpacity(Clamp01(Progress(elapsed_, 0.12f, 0.12f)));
    context.PushScale(Lerp(1.9f, 1.0f, stamp), {640.0f, 150.0f});
    DrawSeal(context, {640.0f, 150.0f}, 88.0f, win ? "胜" : "负", win ? theme::Cinnabar : Rgb(0x4A5A54), win ? -6.0f : 5.0f, 58.0f);
    context.PopTransform();
    context.PopOpacity();

    graphics::TextStyle title = Centered(Kai(32.0f));
    context.DrawTextUtf8(win ? "本局大胜" : "惜败一局", {Panel.x, 206.0f, Panel.width, 42.0f}, title, win ? theme::GoldLight : theme::Ivory);
    const int myScore = record_.scores[0];
    context.DrawTextUtf8("本局得分  " + Signed(myScore), {Panel.x, 248.0f, Panel.width, 26.0f},
        Centered(Text(17.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD)), ScoreColor(myScore));

    // Special-event badges.
    std::vector<std::string> badges;
    int scoredBombs = 0;
    int beatenBombs = 0;
    for (const rules::BombScoreEvent& bomb : record_.bombs) {
        if (bomb.beaten) {
            ++beatenBombs;
        } else {
            ++scoredBombs;
        }
    }
    if (scoredBombs > 0) {
        std::string text = "炸弹 × ";
        core::AppendNumber(text, scoredBombs);
        text += "  (不参与翻倍)";
        badges.push_back(text);
    }
    if (beatenBombs > 0) {
        std::string text = "炸弹被压 × ";
        core::AppendNumber(text, beatenBombs);
        text += "  (不计分)";
        badges.push_back(text);
    }
    if (record_.spring.enabled) {
        std::string text = "关圆鸡 × ";
        core::AppendNumber(text, record_.spring.loserCount);
        badges.push_back(text);
    }
    if (!badges.empty()) {
        ChipStyle chip;
        chip.fontSize = 13.5f;
        chip.height = 26.0f;
        chip.fill = WithAlpha(theme::CinnabarDeep, 0.55f);
        chip.stroke = WithAlpha(theme::CinnabarLight, 0.7f);
        chip.text = theme::Ivory;
        float total = -10.0f;
        std::vector<Rect> rects;
        for (const std::string& badge : badges) {
            rects.push_back(ChipRect(context, {0.0f, 292.0f}, Anchor::Left, badge, chip));
            total += rects.back().width + 10.0f;
        }
        float x = 640.0f - total * 0.5f;
        for (std::size_t i = 0; i < badges.size(); ++i) {
            rects[i].x = x;
            DrawChip(context, rects[i], badges[i], chip);
            x += rects[i].width + 10.0f;
        }
    }

    graphics::TextStyle header = Text(13.0f);
    header.wrap = false;
    context.DrawTextUtf8("玩家", {TableLeft + 48.0f, HeaderY - 10.0f, 160.0f, 20.0f}, header, theme::Faint);
    graphics::TextStyle headerCenter = header;
    headerCenter.align = DWRITE_TEXT_ALIGNMENT_CENTER;
    context.DrawTextUtf8("剩余牌", {640.0f, HeaderY - 10.0f, 100.0f, 20.0f}, headerCenter, theme::Faint);
    graphics::TextStyle headerRight = header;
    headerRight.align = DWRITE_TEXT_ALIGNMENT_TRAILING;
    context.DrawTextUtf8("本局得分", {TableRight - 130.0f, HeaderY - 10.0f, 120.0f, 20.0f}, headerRight, theme::Faint);

    const std::array<std::string, 3> names{record_.playerName.empty() ? std::string("玩家") : record_.playerName, "AI1", "AI2"};
    const float countUp = EaseOutCubic(Progress(elapsed_, 0.35f, 0.9f));
    for (int i = 0; i < 3; ++i) {
        const float rowAppear = EaseOutCubic(Progress(elapsed_, 0.2f + static_cast<float>(i) * 0.08f, 0.35f));
        const float y = RowTop + static_cast<float>(i) * RowHeight;
        context.PushOpacity(rowAppear);
        context.PushTranslation(0.0f, (1.0f - rowAppear) * 8.0f);
        const bool winner = rules::PlayerIndex(record_.winner) == i;
        if (winner) {
            context.FillRoundedRect({TableLeft - 8.0f, y + 3.0f, TableRight - TableLeft + 16.0f, RowHeight - 6.0f}, 12.0f,
                context.Linear({TableLeft, y}, {TableRight, y},
                    {{0.0f, WithAlpha(theme::Gold, 0.20f)}, {1.0f, WithAlpha(theme::Gold, 0.02f)}}));
        }
        DrawAvatar(context, {TableLeft, y + 8.0f, 30.0f, 30.0f}, AvatarLabel(names[static_cast<std::size_t>(i)]), false, 0.0f);
        graphics::TextStyle name = Text(17.0f, winner ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
        name.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
        name.wrap = false;
        name.ellipsis = true;
        context.DrawTextUtf8(names[static_cast<std::size_t>(i)], {TableLeft + 48.0f, y, 170.0f, RowHeight}, name, winner ? theme::GoldLight : theme::Ivory);
        if (winner) {
            ChipStyle chip;
            chip.fontSize = 12.0f;
            chip.height = 20.0f;
            chip.padX = 8.0f;
            chip.fill = WithAlpha(theme::Gold, 0.9f);
            chip.stroke = {0.0f, 0.0f, 0.0f, 0.0f};
            chip.text = theme::GoldInk;
            chip.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            const float nameWidth = std::min(170.0f, context.MeasureText(names[static_cast<std::size_t>(i)], name).width);
            DrawChip(context, {TableLeft + 58.0f + nameWidth, y + RowHeight * 0.5f}, Anchor::Left, "赢家", chip);
        }
        std::string remaining;
        core::AppendNumber(remaining, record_.remainingCards[static_cast<std::size_t>(i)]);
        context.DrawTextUtf8(remaining, {640.0f, y, 100.0f, RowHeight}, Centered(Text(17.0f)), theme::Muted);

        const int score = record_.scores[static_cast<std::size_t>(i)];
        const int shown = RoundToInt(static_cast<float>(score) * countUp);
        graphics::TextStyle scoreStyle = Text(21.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        scoreStyle.align = DWRITE_TEXT_ALIGNMENT_TRAILING;
        scoreStyle.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
        context.DrawTextUtf8(Signed(shown), {TableRight - 130.0f, y, 120.0f, RowHeight}, scoreStyle, ScoreColor(score));
        if (i < 2) {
            DrawHairline(context, TableLeft, TableRight, y + RowHeight, 0.22f);
        }
        context.PopTransform();
        context.PopOpacity();
    }

    ButtonGroup::DrawAll(context, buttons_);
    EndModal(context);
}

bool RoundResultOverlay::OnMouseMove(float x, float y) {
    ButtonGroup::UpdateHover(buttons_, x, y);
    return true;
}

bool RoundResultOverlay::OnMouseDown(float x, float y) {
    const int hit = ButtonGroup::Hit(buttons_, x, y);
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
