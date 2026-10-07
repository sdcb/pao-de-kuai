#include "scenes/StartScene.h"

#include "app/App.h"
#include "audio/SoundIds.h"
#include "core/CppCompat.h"
#include "overlays/AboutOverlay.h"
#include "stats/CppCompat.h"

#include <array>
#include <cmath>
#include <memory>

namespace pdk::scenes {
namespace {

using namespace ui;

constexpr float MenuX = 842.0f;
constexpr float MenuTop = 184.0f;
constexpr float MenuWidth = 300.0f;
constexpr float MenuHeight = 54.0f;
constexpr float MenuGap = 14.0f;

// 10 J Q K A: the top straight, a quiet nod to the rules on the title screen.
constexpr std::array<rules::Card, 5> FanCards{{
    {RANK_TEN, SUIT_HEARTS},
    {RANK_JACK, SUIT_CLUBS},
    {RANK_QUEEN, SUIT_DIAMONDS},
    {RANK_KING, SUIT_SPADES},
    {RANK_ACE, SUIT_HEARTS}}};

} // namespace

StartScene::StartScene(app::App& app) : app_(app) {
    const std::array<const char*, 6> labels{"开始游戏", "积分统计", "设置", "帮助", "关于", "退出"};
    for (std::size_t i = 0; i < labels.size(); ++i) {
        Button button;
        button.rect = {MenuX, MenuTop + static_cast<float>(i) * (MenuHeight + MenuGap), MenuWidth, MenuHeight};
        button.text = labels[i];
        button.fontSize = 20.0f;
        button.style = i == 0 ? ButtonStyle::Primary : (i == labels.size() - 1 ? ButtonStyle::Ghost : ButtonStyle::Secondary);
        buttons_.push_back(button);
    }
    buttons_[0].fontSize = 22.0f;
}

void StartScene::OnEnter() {
    app_.LoadCardAtlas();
    const stats::StatSummary today = stats::StatStore().SummarizeDay(stats::TodayDateKey());
    welcome_ = app_.Settings().playerName.empty() ? "欢迎回来" : app_.Settings().playerName + "，欢迎回来";
    if (today.rounds > 0) {
        welcome_ += "  ·  今日 ";
        if (today.scores[0] > 0) {
            welcome_ += "+";
        }
        core::AppendNumber(welcome_, today.scores[0]);
        welcome_ += " 分";
    }
}

void StartScene::Update(float dt) {
    elapsed_ += dt;
    ButtonGroup::UpdateAll(buttons_, dt);
    const Point target{(mouse_.x - 640.0f) / 640.0f, (mouse_.y - 360.0f) / 360.0f};
    parallax_.x = Approach(parallax_.x, target.x, 3.0f, dt);
    parallax_.y = Approach(parallax_.y, target.y, 3.0f, dt);
}

void StartScene::Render(graphics::RenderContext& context) {
    if (!app_.CardAtlas().Loaded()) {
        app_.LoadCardAtlas();
    }
    context.Clear(theme::Room);
    DrawRoomBackground(context, {430.0f + parallax_.x * 20.0f, 330.0f + parallax_.y * 14.0f});

    // Moon-like halo behind the title block.
    const Point halo{400.0f + parallax_.x * 8.0f, 330.0f + parallax_.y * 6.0f};
    DrawRadialGlow(context, halo, 330.0f, WithAlpha(theme::Gold, 0.07f));
    context.StrokeEllipse({halo.x - 262.0f, halo.y - 262.0f, 524.0f, 524.0f}, WithAlpha(theme::Gold, 0.10f), 1.0f);
    context.StrokeEllipse({halo.x - 250.0f, halo.y - 250.0f, 500.0f, 500.0f}, WithAlpha(theme::Gold, 0.05f), 1.0f);

    DrawCardFan(context);
    DrawTitle(context);

    // Vertical divider between the title block and the menu.
    context.DrawLine({770.0f, 170.0f}, {770.0f, 590.0f}, context.Linear({770.0f, 170.0f}, {770.0f, 590.0f},
        {{0.0f, WithAlpha(theme::Gold, 0.0f)}, {0.5f, WithAlpha(theme::Gold, 0.35f)}, {1.0f, WithAlpha(theme::Gold, 0.0f)}}), 1.0f);

    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const float t = EaseOutCubic(Progress(elapsed_, 0.15f + static_cast<float>(i) * 0.06f, 0.5f));
        context.PushOpacity(t);
        context.PushTranslation((1.0f - t) * 40.0f, 0.0f);
        buttons_[i].Draw(context);
        context.PopTransform();
        context.PopOpacity();
    }

    graphics::TextStyle footer = Centered(Text(12.5f));
    context.DrawTextUtf8("使用 cJSON / doctest (MIT) 与 VC-LTL (EPL-2.0)", {0.0f, 680.0f, 1280.0f, 24.0f}, footer, WithAlpha(theme::Faint, 0.9f));
}

void StartScene::DrawTitle(graphics::RenderContext& context) {
    const float t = EaseOutCubic(Progress(elapsed_, 0.0f, 0.7f));
    context.PushOpacity(t);
    context.PushTranslation(0.0f, (1.0f - t) * 18.0f);

    const Rect titleRect{150.0f, 150.0f, 560.0f, 150.0f};
    graphics::TextStyle title = Kai(132.0f);
    title.wrap = false;
    context.DrawTextUtf8("跑得快", {titleRect.x + 3.0f, titleRect.y + 6.0f, titleRect.width, titleRect.height}, title, WithAlpha(theme::RoomDeep, 0.7f));
    context.DrawTextUtf8("跑得快", titleRect, title, context.Linear({0.0f, titleRect.y + 20.0f}, {0.0f, titleRect.y + 140.0f},
        {{0.0f, theme::GoldLight}, {0.55f, theme::Gold}, {1.0f, theme::GoldDeep}}));

    DrawSeal(context, {582.0f, 196.0f}, 62.0f, "极\n客", theme::Cinnabar, -6.0f, 25.0f);

    context.DrawTextUtf8("三人  ·  四十八张  ·  经典规则", {156.0f, 312.0f, 560.0f, 30.0f}, Text(19.0f), theme::Muted);
    DrawHairline(context, 150.0f, 620.0f, 356.0f, 0.5f);

    ChipStyle chip;
    chip.fontSize = 15.5f;
    chip.height = 34.0f;
    chip.padX = 18.0f;
    chip.stroke = WithAlpha(theme::Gold, 0.45f);
    DrawChip(context, {156.0f, 392.0f}, Anchor::Left, welcome_, chip);

    context.PopTransform();
    context.PopOpacity();
}

void StartScene::DrawCardFan(graphics::RenderContext& context) {
    const float appear = EaseOutCubic(Progress(elapsed_, 0.25f, 0.9f));
    const Point pivot{360.0f + parallax_.x * 14.0f, 900.0f + parallax_.y * 8.0f};
    const float cardW = 116.0f;
    const float cardH = cardW * 1.4f;
    for (std::size_t i = 0; i < FanCards.size(); ++i) {
        const float index = static_cast<float>(i) - 2.0f;
        const float drift = std::sin(elapsed_ * 0.9f + static_cast<float>(i) * 0.8f);
        const float angle = (index * 12.5f + drift * 0.8f) * appear;
        const float radians = angle * 3.14159265f / 180.0f;
        const float reach = 378.0f + drift * 3.0f;
        const Point center{pivot.x + std::sin(radians) * reach, pivot.y - std::cos(radians) * reach + (1.0f - appear) * 60.0f};
        CardLook look;
        look.rotation = angle;
        look.shadow = 1.2f;
        look.opacity = appear;
        DrawCardFace(context, app_.CardAtlas(), FanCards[i], {center.x - cardW * 0.5f, center.y - cardH * 0.5f, cardW, cardH}, look);
    }
}

bool StartScene::OnMouseMove(float x, float y) {
    mouse_ = {x, y};
    ButtonGroup::UpdateHover(buttons_, x, y);
    return true;
}

bool StartScene::OnMouseDown(float x, float y) {
    const int hit = ButtonGroup::Hit(buttons_, x, y);
    if (hit < 0) {
        return false;
    }
    app_.Audio().Play(SOUND_BUTTON_CLICK);
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
