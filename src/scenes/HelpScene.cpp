#include "scenes/HelpScene.h"

#include "app/App.h"
#include "audio/SoundIds.h"
#include "rules/CppCompat.h"

#include <array>
#include <string>
#include <vector>

namespace pdk::scenes {
namespace {

using namespace ui;
using rules::Rank;
using rules::Suit;

constexpr Rect RulesPanel{56.0f, 120.0f, 650.0f, 572.0f};
constexpr Rect SidePanel{724.0f, 120.0f, 500.0f, 572.0f};

struct PatternSample {
    const char* name;
    std::vector<rules::Card> cards;
};

std::vector<PatternSample> PatternSamples() {
    return {
        {"对子", {{RANK_NINE, SUIT_SPADES}, {RANK_NINE, SUIT_HEARTS}}},
        {"顺子", {{RANK_THREE, SUIT_CLUBS}, {RANK_FOUR, SUIT_HEARTS}, {RANK_FIVE, SUIT_SPADES}, {RANK_SIX, SUIT_DIAMONDS}, {RANK_SEVEN, SUIT_CLUBS}}},
        {"连对", {{RANK_THREE, SUIT_SPADES}, {RANK_THREE, SUIT_HEARTS}, {RANK_FOUR, SUIT_CLUBS}, {RANK_FOUR, SUIT_DIAMONDS}}},
        {"三带二", {{RANK_SEVEN, SUIT_SPADES}, {RANK_SEVEN, SUIT_HEARTS}, {RANK_SEVEN, SUIT_CLUBS}, {RANK_NINE, SUIT_DIAMONDS}, {RANK_JACK, SUIT_SPADES}}},
        {"飞机", {{RANK_EIGHT, SUIT_SPADES}, {RANK_EIGHT, SUIT_HEARTS}, {RANK_EIGHT, SUIT_CLUBS}, {RANK_NINE, SUIT_SPADES},
                  {RANK_NINE, SUIT_HEARTS}, {RANK_NINE, SUIT_DIAMONDS}, {RANK_FOUR, SUIT_CLUBS}, {RANK_SIX, SUIT_HEARTS}}},
        {"炸弹", {{RANK_KING, SUIT_SPADES}, {RANK_KING, SUIT_HEARTS}, {RANK_KING, SUIT_DIAMONDS}, {RANK_KING, SUIT_CLUBS}}},
    };
}

} // namespace

HelpScene::HelpScene(app::App& app) : app_(app) {
    buttons_ = {MakeBackButton()};
}

void HelpScene::OnEnter() {
    app_.LoadCardAtlas();
}

void HelpScene::Update(float dt) {
    elapsed_ += dt;
    ButtonGroup::UpdateAll(buttons_, dt);
}

float HelpScene::DrawBullets(graphics::RenderContext& context, std::string_view text, float x, float y, float width) {
    graphics::TextStyle style = Text(15.5f);
    style.lineHeight = 23.0f;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string line(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (!line.empty()) {
            const Size size = context.MeasureText(line, style, width - 20.0f);
            context.FillEllipse({x + 1.0f, y + 9.0f, 6.0f, 6.0f}, WithAlpha(theme::Gold, 0.85f));
            context.DrawTextUtf8(line, {x + 20.0f, y, width - 20.0f, size.height + 4.0f}, style, theme::Ivory);
            y += size.height + 9.0f;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return y;
}

void HelpScene::DrawPatternGallery(graphics::RenderContext& context, float x, float y, float width) {
    const std::vector<PatternSample> samples = PatternSamples();
    const float cellWidth = width * 0.5f;
    constexpr float cardW = 42.0f;
    constexpr float cardH = cardW * 1.4f;
    const float step = cardW / 3.0f;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const float cx = x + cellWidth * static_cast<float>(i % 2);
        const float cy = y + 78.0f * static_cast<float>(i / 2);
        context.DrawTextUtf8(samples[i].name, {cx, cy + 18.0f, 56.0f, 24.0f}, Text(14.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD), theme::Gold);
        for (std::size_t c = 0; c < samples[i].cards.size(); ++c) {
            CardLook look;
            look.shadow = 0.7f;
            DrawCardFace(context, app_.CardAtlas(), samples[i].cards[c], {cx + 58.0f + step * static_cast<float>(c), cy, cardW, cardH}, look);
        }
    }
}

void HelpScene::Render(graphics::RenderContext& context) {
    if (!app_.CardAtlas().Loaded()) {
        app_.LoadCardAtlas();
    }
    context.Clear(theme::Room);
    DrawRoomBackground(context, {640.0f, 300.0f});
    DrawPageHeader(context, "帮助", "三人场，四十八张牌；先出完手牌的一方获胜");

    const float appear = EaseOutCubic(Progress(elapsed_, 0.0f, 0.5f));
    context.PushOpacity(appear);
    context.PushTranslation(0.0f, (1.0f - appear) * 16.0f);

    DrawPanel(context, RulesPanel);
    context.DrawTextUtf8("游戏规则", {RulesPanel.x + 34.0f, RulesPanel.y + 24.0f, 300.0f, 34.0f}, Kai(25.0f), theme::GoldLight);
    DrawBullets(context, rules::SharedGameRulesText(), RulesPanel.x + 36.0f, RulesPanel.y + 76.0f, RulesPanel.width - 72.0f);

    DrawPanel(context, SidePanel);
    context.DrawTextUtf8("计分与托管", {SidePanel.x + 34.0f, SidePanel.y + 24.0f, 300.0f, 34.0f}, Kai(25.0f), theme::GoldLight);
    const float after = DrawBullets(context, rules::HumanHelpText(), SidePanel.x + 36.0f, SidePanel.y + 76.0f, SidePanel.width - 72.0f);
    DrawHairline(context, SidePanel.x + 20.0f, SidePanel.x + SidePanel.width - 20.0f, after + 10.0f, 0.4f);
    context.DrawTextUtf8("牌型速览", {SidePanel.x + 34.0f, after + 24.0f, 300.0f, 34.0f}, Kai(25.0f), theme::GoldLight);
    DrawPatternGallery(context, SidePanel.x + 36.0f, after + 72.0f, SidePanel.width - 72.0f);

    context.PopTransform();
    context.PopOpacity();
    ButtonGroup::DrawAll(context, buttons_);
}

bool HelpScene::OnMouseMove(float x, float y) {
    ButtonGroup::UpdateHover(buttons_, x, y);
    return true;
}

bool HelpScene::OnMouseDown(float x, float y) {
    if (ButtonGroup::Hit(buttons_, x, y) >= 0) {
        app_.Audio().Play(audio::SoundId::Cancel);
        app_.ShowStart();
        return true;
    }
    return false;
}

} // namespace pdk::scenes
