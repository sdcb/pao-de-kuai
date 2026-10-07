#include "scenes/GameScene.h"

#include "app/App.h"
#include "audio/SoundIds.h"
#include "core/CppCompat.h"
#include "game/LocalAiController.h"
#include "overlays/ReturnToMenuOverlay.h"
#include "overlays/RoundResultOverlay.h"
#include "overlays/TalkBubbleOverlay.h"
#include "resources/CardAtlasData.h"
#include "scenes/GameLayout.h"
#include "stats/StatStore.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace pdk::scenes {
namespace {

using namespace ui;

constexpr float Pi = 3.14159265f;
constexpr float DealCardInterval = 1.0f / 6.0f;
constexpr int DealSoundCount = 16;
constexpr int FullHandCardCount = 16;
constexpr float SortAnimationSpeed = 2.4f;
constexpr float RoundResultDelaySeconds = 1.2f;
constexpr float PlayerCardWidth = 105.0f;
constexpr float PlayerCardHeight = 147.0f;
constexpr float PlayedCardWidth = 92.0f;
constexpr float PlayedCardTop = 220.0f;
constexpr float HoverLift = 7.0f;
constexpr float PassChipSeconds = 1.6f;
constexpr float ToastSeconds = 4.5f;
constexpr Point DealCenter{640.0f, 300.0f};
constexpr Point PlayerPlaySource{640.0f, 640.0f};

bool ContainsIndex(const std::vector<int>& values, int index) {
    return std::find(values.begin(), values.end(), index) != values.end();
}

float VisibleStepForWidth(float cardWidth) {
    const resources::CardAtlasInfo& info = resources::GetCardAtlasInfo();
    // mainX is the atlas-defined safe visible strip; stepping by this scaled
    // width prevents the next card from revealing the large right-side suit.
    return cardWidth * static_cast<float>(info.mainX) / static_cast<float>(info.cardWidth);
}

float CardRowWidth(int count, float cardWidth) {
    if (count <= 0) {
        return 0.0f;
    }
    return cardWidth + static_cast<float>(count - 1) * VisibleStepForWidth(cardWidth);
}

int FindCardIndex(const rules::Cards& cards, rules::Card card) {
    const auto it = std::find_if(cards.begin(), cards.end(), [card](rules::Card existing) {
        return existing.rank == card.rank && existing.suit == card.suit;
    });
    if (it == cards.end()) {
        return -1;
    }
    return static_cast<int>(std::distance(cards.begin(), it));
}

// Moves a dealt card from the centre pile along a shallow arc; returns flight progress.
float DealFlight(Rect& rect, float& rotation, int index, float elapsed) {
    const float t = Clamp01((elapsed - static_cast<float>(index) * DealCardInterval) / (DealCardInterval * 1.5f));
    const float e = EaseOutCubic(t);
    const float startRotation = static_cast<float>((index * 37) % 29 - 14);
    rect.x = Lerp(DealCenter.x - rect.width * 0.5f, rect.x, e);
    rect.y = Lerp(DealCenter.y - rect.height * 0.5f, rect.y, e) - std::sin(t * Pi) * 46.0f;
    rotation = Lerp(startRotation, 0.0f, e);
    return t;
}

std::string SignedScore(int score) {
    std::string text = score > 0 ? "+" : "";
    core::AppendNumber(text, score);
    return text;
}

} // namespace

GameScene::GameScene(app::App& app, bool mock, bool midgame) : app_(app), mock_(mock), midgameMock_(midgame) {
    backButton_ = {{20.0f, 20.0f, 46.0f, 46.0f}, "", ButtonStyle::Icon, Icon::Back};
    buttons_ = {
        {{0.0f, 0.0f, 110.0f, 44.0f}, "托管", ButtonStyle::Ghost},
        {{0.0f, 0.0f, 104.0f, 44.0f}, "不要", ButtonStyle::Secondary},
        {{0.0f, 0.0f, 104.0f, 44.0f}, "提示", ButtonStyle::Secondary},
        {{0.0f, 0.0f, 128.0f, 44.0f}, "出牌", ButtonStyle::Primary}
    };
    for (Button& button : buttons_) {
        button.fontSize = 18.0f;
    }
    LayoutActionButtons();
}

void GameScene::OnEnter() {
    if (!app_.GameResourcesReady()) {
        app_.LoadGameResources();
    }
    InitializeExternalAi();
    todayScores_ = stats::StatStore().SummarizeDay(stats::TodayDateKey()).scores;
    StartNextRound();
}

void GameScene::StartNextRound() {
    game_.SetRoundTraceEnabled(app_.Settings().roundTraceEnabled);
    game_.StartNewRound(app_.Settings().playerName, mock_ ? 20260606u : 0u);
    recordedRound_ = false;
    roundResultPending_ = false;
    for (int i = 0; i < 3; ++i) {
        handsBeforeSort_[i] = game_.Players()[i].hand;
    }
    handsSorted_ = false;
    dealElapsed_ = 0.0f;
    sortAnimation_ = 0.0f;
    playAnimation_ = 0.0f;
    bombAnimation_ = 0.0f;
    roundResultDelay_ = 0.0f;
    dealSoundCount_ = 0;
    dragSelecting_ = false;
    dragMoved_ = false;
    hoverCard_ = -1;
    backButton_.hover = false;
    dragStartCard_ = -1;
    dragPath_.clear();
    handLift_.clear();
    handHover_.clear();
    passTimers_ = {0.0f, 0.0f, 0.0f};
    lastAnimatedPlayer_ = PLAYER_HUMAN;
    if (midgameMock_) {
        game_.SortHands();
        handsSorted_ = true;
        dealSoundCount_ = DealSoundCount;
        dealElapsed_ = static_cast<float>(DealSoundCount) * DealCardInterval + 1.0f;
    }
    actionButtonsDirty_ = true;
    lastInteractionReady_ = InteractionReady();
    UpdateActionButtons();
}

void GameScene::InitializeExternalAi() {
    if (mock_ || app_.ViewerMode()) {
        game_.SetExternalAiControllers({});
        return;
    }

    auto controller = std::make_shared<game::LocalAiController>();
    auto kindFor = [](const std::string& selection) {
        return selection == "strong" ? game::LocalAiKind::Strong : game::LocalAiKind::Basic;
    };
    controller->SetStrategy(PLAYER_AI1, kindFor(app_.Settings().ai1));
    controller->SetStrategy(PLAYER_AI2, kindFor(app_.Settings().ai2));
    game_.SetExternalAiControllers({std::move(controller)});
}

void GameScene::Update(float dt) {
    time_ += dt;
    backButton_.Update(dt);
    ButtonGroup::UpdateAll(buttons_, dt);
    for (float& timer : passTimers_) {
        timer = std::max(0.0f, timer - dt);
    }
    toastAge_ += dt;
    if (game_.Toast() != toastText_) {
        toastText_ = game_.Toast();
        toastAge_ = 0.0f;
    }

    if (!handsSorted_) {
        dealElapsed_ += dt;
        while (dealSoundCount_ < DealSoundCount && dealElapsed_ >= static_cast<float>(dealSoundCount_ + 1) * DealCardInterval) {
            app_.Audio().Play(audio::SoundId::DealCard);
            dealSoundCount_++;
        }
        if (dealElapsed_ >= static_cast<float>(DealSoundCount) * DealCardInterval + 0.1f) {
            for (int i = 0; i < 3; ++i) {
                handsBeforeSort_[i] = game_.Players()[i].hand;
            }
            game_.SortHands();
            handsSorted_ = true;
            sortAnimation_ = 1.0f;
            app_.Audio().Play(audio::SoundId::Hint);
            actionButtonsDirty_ = true;
        }
    } else {
        sortAnimation_ = std::max(0.0f, sortAnimation_ - dt * SortAnimationSpeed);
    }
    playAnimation_ = std::max(0.0f, playAnimation_ - dt * 2.6f);
    bombAnimation_ = std::max(0.0f, bombAnimation_ - dt * 0.9f);    if (InteractionReady()) {
        game_.Update(dt);
    }
    if (midgameMock_) {
        UpdateMidgameMock();
    }
    const bool hadEvents = !game_.Events().empty();
    ConsumeEvents();
    UpdateRoundResultDelay(dt);
    UpdateHandAnimation(dt);

    // Button enablement can query rule search, so update it only after game or
    // interaction state changes instead of doing that work every frame/mouse move.
    const bool ready = InteractionReady();
    if (hadEvents || ready != lastInteractionReady_) {
        actionButtonsDirty_ = true;
    }
    if (actionButtonsDirty_) {
        UpdateActionButtons();
    }
}

void GameScene::UpdateMidgameMock() {
    if (!InteractionReady() || !game_.IsHumanTurn()) {
        return;
    }
    if (!mockPlayed_) {
        if (game_.ApplyHint() && !game_.SelectedIndices().empty()) {
            game_.PlaySelected();
        }
        mockPlayed_ = true;
    } else if (!mockHinted_) {
        game_.ApplyHint();
        mockHinted_ = true;
    }
    actionButtonsDirty_ = true;
}

void GameScene::UpdateHandAnimation(float dt) {
    const auto& hand = game_.Players()[0].hand;
    if (handLift_.size() != hand.size()) {
        handLift_.assign(hand.size(), 0.0f);
        handHover_.assign(hand.size(), 0.0f);
    }
    const bool ready = InteractionReady();
    for (std::size_t i = 0; i < hand.size(); ++i) {
        const int index = static_cast<int>(i);
        const bool selected = game_.SelectedIndices().contains(index);
        const float hoverTarget = ready && !dragSelecting_ && hoverCard_ == index ? 1.0f : 0.0f;
        handHover_[i] = Approach(handHover_[i], hoverTarget, 18.0f, dt);
        const float liftTarget = (selected ? layout::SelectedLift : 0.0f) + handHover_[i] * HoverLift;
        handLift_[i] = Approach(handLift_[i], liftTarget, 20.0f, dt);
    }
}

void GameScene::Render(graphics::RenderContext& context) {
    if (!app_.CardAtlas().Loaded()) {
        app_.LoadGameResources();
    }
    context.Clear(theme::Room);
    context.FillRect({0.0f, 0.0f, LogicalWidth, LogicalHeight}, context.Radial({640.0f, 330.0f}, 900.0f, 620.0f,
        {{0.0f, theme::FeltDeep}, {0.6f, theme::Room}, {1.0f, theme::RoomDeep}}));

    float shakeX = 0.0f;
    float shakeY = 0.0f;
    if (bombAnimation_ > 0.0f) {
        const float p = 1.0f - bombAnimation_;
        const float amplitude = 9.0f * bombAnimation_ * bombAnimation_ * bombAnimation_;
        shakeX = std::sin(p * 71.0f) * amplitude;
        shakeY = std::cos(p * 53.0f) * amplitude * 0.6f;
    }
    context.PushTranslation(shakeX, shakeY);
    DrawTable(context);
    DrawVignette(context, 0.6f);
    DrawAiSeat(context, PLAYER_AI1);
    DrawAiSeat(context, PLAYER_AI2);
    DrawTurnChip(context);
    DrawPlayedCards(context);
    DrawPassChips(context);
    DrawDealPile(context);
    DrawPlayerPlate(context);
    ButtonGroup::DrawAll(context, buttons_);
    DrawToast(context);
    DrawPlayerHand(context);
    context.PopTransform();

    DrawBombEffect(context);
    backButton_.Draw(context);
}

void GameScene::DrawTable(graphics::RenderContext& context) {
    const Rect& table = layout::Table;
    const Point center{table.x + table.width * 0.5f, table.y + table.height * 0.5f};
    const float rx = table.width * 0.5f;
    const float ry = table.height * 0.5f;
    constexpr D2D1_COLOR_F Black = {0.0f, 0.0f, 0.0f, 1.0f};

    // Drop shadow under the whole table.
    context.FillEllipse({center.x - rx - 40.0f, center.y - ry - 26.0f, (rx + 40.0f) * 2.0f, (ry + 40.0f) * 2.0f},
        context.Radial({center.x, center.y + 14.0f}, rx + 40.0f, ry + 40.0f,
            {{0.0f, WithAlpha(Black, 0.6f)}, {0.86f, WithAlpha(Black, 0.5f)}, {1.0f, WithAlpha(Black, 0.0f)}}));

    // Padded leather rail with a gold inlay.
    const Rect rail{table.x - 16.0f, table.y - 16.0f, table.width + 32.0f, table.height + 32.0f};
    context.FillEllipse(rail, context.Linear({0.0f, rail.y}, {0.0f, rail.y + rail.height},
        {{0.0f, Rgb(0x3A2A1C)}, {0.5f, Rgb(0x21170F)}, {1.0f, Rgb(0x120C08)}}));
    context.StrokeEllipse({rail.x + 2.0f, rail.y + 2.0f, rail.width - 4.0f, rail.height - 4.0f}, WithAlpha(theme::GoldLight, 0.10f), 2.0f);
    context.StrokeEllipse({table.x - 8.0f, table.y - 8.0f, table.width + 16.0f, table.height + 16.0f}, WithAlpha(theme::Gold, 0.28f), 1.0f);

    // Felt, lit from slightly above centre.
    context.FillEllipse(table, context.Radial({center.x, center.y - 30.0f}, rx * 1.02f, ry * 1.15f,
        {{0.0f, theme::FeltLight}, {0.55f, theme::Felt}, {1.0f, theme::FeltDeep}}));
    context.PushOpacity(0.32f);
    context.FillEllipse(table, context.FeltBrush());
    context.PopOpacity();
    context.StrokeEllipse({table.x + 9.0f, table.y + 9.0f, table.width - 18.0f, table.height - 18.0f}, WithAlpha(Black, 0.16f), 18.0f);
    context.StrokeEllipse({table.x + 3.0f, table.y + 3.0f, table.width - 6.0f, table.height - 6.0f}, WithAlpha(Black, 0.28f), 6.0f);
    context.StrokeEllipse({table.x - 0.5f, table.y - 0.5f, table.width + 1.0f, table.height + 1.0f}, WithAlpha(theme::Gold, 0.78f), 1.6f);
    // Faint play-zone line.
    context.StrokeEllipse({table.x + 70.0f, table.y + 58.0f, table.width - 140.0f, table.height - 116.0f}, WithAlpha(theme::Gold, 0.08f), 1.0f);
}

void GameScene::DrawTurnChip(graphics::RenderContext& context) {
    std::string text;
    bool highlight = false;
    if (game_.IsRoundOver()) {
        text = "本局结束";
    } else if (!handsSorted_) {
        text = "发牌中";
    } else if (game_.IsHumanTurn()) {
        text = "轮到你出牌";
        highlight = true;
    } else {
        text = game_.Players()[rules::PlayerIndex(game_.CurrentPlayer())].name + " 出牌中";
    }
    ChipStyle style;
    style.fontSize = 16.0f;
    style.height = 34.0f;
    style.padX = 22.0f;
    style.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    if (highlight) {
        const float pulse = 0.5f + 0.5f * std::sin(time_ * 4.0f);
        const Rect rect = ChipRect(context, {640.0f, 44.0f}, Anchor::Center, text, style);
        context.DrawShadow(rect, 9.0f, WithAlpha(theme::Gold, 0.25f + 0.2f * pulse));
        style.fill = WithAlpha(theme::GoldDeep, 0.35f);
        style.stroke = WithAlpha(theme::GoldLight, 0.85f);
        style.text = theme::GoldLight;
    } else {
        style.text = theme::Muted;
    }
    DrawChip(context, {640.0f, 44.0f}, Anchor::Center, text, style);
}

void GameScene::DrawAiSeat(graphics::RenderContext& context, rules::PlayerId player) {
    const game::PlayerState& state = game_.Players()[rules::PlayerIndex(player)];
    const Rect plate = layout::PlateFor(player);
    const bool active = handsSorted_ && !game_.IsRoundOver() && game_.CurrentPlayer() == player;

    PanelStyle panel;
    panel.radius = 20.0f;
    panel.ornament = false;
    panel.fillAlpha = 0.9f;
    DrawPanel(context, plate, panel);
    if (active) {
        const float pulse = 0.5f + 0.5f * std::sin(time_ * 4.2f);
        context.StrokeRoundedRect({plate.x + 0.5f, plate.y + 0.5f, plate.width - 1.0f, plate.height - 1.0f}, panel.radius,
            WithAlpha(theme::GoldLight, 0.35f + 0.3f * pulse), 1.6f);
    }

    const Rect avatar = layout::AvatarRect(player);
    DrawAvatar(context, avatar, AvatarLabel(state.name), active, time_);

    const int count = static_cast<int>(state.hand.size());
    ChipStyle countChip;
    countChip.fontSize = 13.0f;
    countChip.height = 22.0f;
    countChip.padX = 10.0f;
    std::string countText;
    core::AppendNumber(countText, count);
    countText += " 张";
    if (!game_.IsRoundOver() && handsSorted_ && count > 0 && count <= 2) {
        countText = count == 1 ? "报单" : "报双";
        countChip.fill = theme::Cinnabar;
        countChip.stroke = WithAlpha(theme::CinnabarLight, 0.8f);
        countChip.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        context.DrawShadow(ChipRect(context, {avatar.x + avatar.width * 0.5f, avatar.y + avatar.height + 16.0f}, Anchor::Center, countText, countChip),
            7.0f, WithAlpha(theme::Cinnabar, 0.45f + 0.25f * std::sin(time_ * 6.0f)));
    }
    DrawChip(context, {avatar.x + avatar.width * 0.5f, avatar.y + avatar.height + 16.0f}, Anchor::Center, countText, countChip);

    const Rect info = layout::AiInfoArea(player);
    graphics::TextStyle nameStyle = Text(17.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    nameStyle.wrap = false;
    nameStyle.ellipsis = true;
    context.DrawTextUtf8(state.name, {info.x, info.y, info.width - 96.0f, 26.0f}, nameStyle, theme::Ivory);
    if (active) {
        const float nameWidth = std::min(info.width - 96.0f, context.MeasureText(state.name, nameStyle).width);
        std::string thinking = "思考中";
        const int dots = static_cast<int>(time_ * 3.0f) % 4;
        thinking.append(static_cast<std::size_t>(dots), '.');
        context.DrawTextUtf8(thinking, {info.x + nameWidth + 10.0f, info.y + 3.0f, 120.0f, 22.0f}, Text(13.5f), theme::Gold);
    }
    graphics::TextStyle scoreStyle = Text(13.5f);
    scoreStyle.align = DWRITE_TEXT_ALIGNMENT_TRAILING;
    scoreStyle.wrap = false;
    const int today = todayScores_[rules::PlayerIndex(player)];
    context.DrawTextUtf8("今日 " + SignedScore(today), {info.x + info.width - 96.0f, info.y + 3.0f, 96.0f, 22.0f}, scoreStyle, ScoreColor(today));

    for (int i = 0; i < count; ++i) {
        Rect target = AiCardRectFor(player, i, count);
        CardLook look;
        look.shadow = 0.8f;
        if (!handsSorted_) {
            if (DealFlight(target, look.rotation, i, dealElapsed_) <= 0.0f) {
                continue;
            }
        } else if (sortAnimation_ > 0.0f) {
            const rules::Cards& oldHand = handsBeforeSort_[rules::PlayerIndex(player)];
            const int oldIndex = FindCardIndex(oldHand, state.hand[static_cast<std::size_t>(i)]);
            if (oldIndex >= 0) {
                // AI cards stay face-down, but their backs still move from the
                // dealt order to the sorted order so all hands feel consistent.
                const Rect from = AiCardRectFor(player, oldIndex, static_cast<int>(oldHand.size()));
                const float t = EaseInOutSine(1.0f - sortAnimation_);
                target.x = Lerp(from.x, target.x, t);
                target.y = Lerp(from.y, target.y, t);
            }
        }
        // After the round ends the result overlay is intentionally delayed, so
        // reveal the AI leftovers here to let the player inspect what was held.
        if (game_.IsRoundOver()) {
            DrawCardFace(context, app_.CardAtlas(), state.hand[static_cast<std::size_t>(i)], target, look);
        } else {
            DrawCardBack(context, app_.CardAtlas(), target, look);
        }
    }
}

void GameScene::DrawPlayerPlate(graphics::RenderContext& context) {
    const Rect plate = layout::PlayerPlate;
    const bool active = handsSorted_ && game_.IsHumanTurn();
    PanelStyle panel;
    panel.radius = 20.0f;
    panel.ornament = false;
    panel.fillAlpha = 0.88f;
    DrawPanel(context, plate, panel);
    DrawAvatar(context, layout::AvatarRect(PLAYER_HUMAN), AvatarLabel(app_.Settings().playerName), active, time_);

    graphics::TextStyle nameStyle = Text(17.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    nameStyle.wrap = false;
    nameStyle.ellipsis = true;
    context.DrawTextUtf8(app_.Settings().playerName, {plate.x + 80.0f, plate.y + 12.0f, 110.0f, 26.0f}, nameStyle, theme::Ivory);
    context.DrawTextUtf8("今日 " + SignedScore(todayScores_[0]), {plate.x + 80.0f, plate.y + 42.0f, 160.0f, 22.0f}, Text(14.5f), ScoreColor(todayScores_[0]));
    if (game_.Autoplay()) {
        ChipStyle chip;
        chip.fontSize = 12.5f;
        chip.height = 22.0f;
        chip.padX = 9.0f;
        chip.fill = WithAlpha(theme::GoldDeep, 0.5f);
        chip.stroke = WithAlpha(theme::Gold, 0.8f);
        chip.text = theme::GoldLight;
        DrawChip(context, {plate.x + plate.width - 14.0f, plate.y + 25.0f}, Anchor::Right, "托管中", chip);
    }
}

void GameScene::DrawPlayerHand(graphics::RenderContext& context) {
    const auto& hand = game_.Players()[0].hand;
    const float glowPulse = 0.72f + 0.28f * std::sin(time_ * 5.0f);
    for (int i = 0; i < static_cast<int>(hand.size()); ++i) {
        const rules::Card& card = hand[static_cast<std::size_t>(i)];
        Rect rect = CardRect(i);
        CardLook look;
        if (!handsSorted_) {
            if (DealFlight(rect, look.rotation, i, dealElapsed_) <= 0.0f) {
                continue;
            }
            DrawCardFace(context, app_.CardAtlas(), card, rect, look);
            continue;
        }
        if (sortAnimation_ > 0.0f) {
            const rules::Cards& oldHand = handsBeforeSort_[rules::PlayerIndex(PLAYER_HUMAN)];
            const int oldIndex = FindCardIndex(oldHand, card);
            if (oldIndex >= 0) {
                const Rect from = CardRectFor(oldIndex, static_cast<int>(oldHand.size()));
                const float t = EaseInOutSine(1.0f - sortAnimation_);
                rect.x = Lerp(from.x, rect.x, t);
                rect.y = Lerp(from.y, rect.y, t) - std::sin(t * Pi) * 16.0f;
            }
        }
        const std::size_t index = static_cast<std::size_t>(i);
        look.lift = index < handLift_.size() ? handLift_[index] : 0.0f;
        look.hover = index < handHover_.size() ? handHover_[index] : 0.0f;
        look.selected = game_.SelectedIndices().contains(i);
        if (dragSelecting_ && ContainsIndex(dragPath_, i)) {
            look.glow = 0.85f;
            look.glowColor = theme::Ivory;
        } else if (ContainsIndex(game_.HintIndices(), i)) {
            look.glow = glowPulse;
        }
        DrawCardFace(context, app_.CardAtlas(), card, rect, look);
    }
}

void GameScene::DrawPlayedCards(graphics::RenderContext& context) {
    const auto& cards = game_.LastCards();
    if (cards.empty()) {
        graphics::TextStyle mark = Centered(Kai(104.0f));
        mark.wrap = false;
        context.DrawTextUtf8("跑得快", {340.0f, 236.0f, 600.0f, 120.0f}, mark, WithAlpha(theme::GoldLight, 0.06f));
        if (handsSorted_ && !game_.IsRoundOver()) {
            context.DrawTextUtf8("等待出牌", {340.0f, 350.0f, 600.0f, 24.0f}, Centered(Text(14.5f)), WithAlpha(theme::Muted, 0.6f));
        }
        return;
    }
    const float cardW = PlayedCardWidth;
    const float cardH = cardW * 1.4f;
    const float step = VisibleStepForWidth(cardW);
    const float totalW = CardRowWidth(static_cast<int>(cards.size()), cardW);
    const float left = 640.0f - totalW * 0.5f;
    Point source = PlayerPlaySource;
    float sourceRotation = 0.0f;
    if (lastAnimatedPlayer_ != PLAYER_HUMAN) {
        source = layout::AvatarCenter(lastAnimatedPlayer_);
        sourceRotation = lastAnimatedPlayer_ == PLAYER_AI1 ? -18.0f : 18.0f;
    }
    const float progress = 1.0f - playAnimation_;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        const Rect finalRect{left + static_cast<float>(i) * step, PlayedCardTop, cardW, cardH};
        Rect rect = finalRect;
        CardLook look;
        if (playAnimation_ > 0.0f) {
            const float t = Clamp01((progress - static_cast<float>(i) * 0.012f) / 0.8f);
            const float e = EaseOutCubic(t);
            const float scale = Lerp(0.5f, 1.0f, EaseOutBackWith(t, 1.3f));
            const float cx = Lerp(source.x, finalRect.x + cardW * 0.5f, e);
            const float cy = Lerp(source.y, finalRect.y + cardH * 0.5f, e);
            rect.width = cardW * scale;
            rect.height = cardH * scale;
            rect.x = cx - rect.width * 0.5f;
            rect.y = cy - rect.height * 0.5f;
            look.rotation = Lerp(sourceRotation, 0.0f, e);
            look.lift = (1.0f - e) * 18.0f;
        }
        DrawCardFace(context, app_.CardAtlas(), cards[i], rect, look);
    }

    if (game_.LastPattern()) {
        const float appear = EaseOutCubic(Progress(progress, 0.5f, 0.5f));
        context.PushOpacity(appear);
        ChipStyle chip;
        chip.fontSize = 14.5f;
        chip.height = 30.0f;
        chip.padX = 16.0f;
        const std::string label = game_.Players()[rules::PlayerIndex(game_.LastMovePlayer())].name + "  ·  " +
            rules::PatternDescription(*game_.LastPattern());
        DrawChip(context, {640.0f, PlayedCardTop + cardH + 28.0f}, Anchor::Center, label, chip);
        context.PopOpacity();
    }
}

void GameScene::DrawPassChips(graphics::RenderContext& context) {
    constexpr Point anchors[3] = {{640.0f, 504.0f}, {300.0f, 232.0f}, {980.0f, 232.0f}};
    ChipStyle chip;
    chip.fontSize = 17.0f;
    chip.height = 38.0f;
    chip.padX = 24.0f;
    chip.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    chip.fill = WithAlpha(theme::Ink, 0.88f);
    chip.stroke = WithAlpha(theme::Ivory, 0.35f);
    for (int i = 0; i < 3; ++i) {
        const float timer = passTimers_[static_cast<std::size_t>(i)];
        if (timer <= 0.0f) {
            continue;
        }
        const float age = PassChipSeconds - timer;
        const float alpha = Clamp01(age * 6.0f) * Clamp01(timer * 3.0f);
        context.PushOpacity(alpha);
        const Point anchor{anchors[i].x, anchors[i].y - EaseOutCubic(Clamp01(age * 3.0f)) * 10.0f};
        context.DrawShadow(ChipRect(context, anchor, Anchor::Center, "不要", chip), 8.0f, {0.0f, 0.0f, 0.0f, 0.5f});
        DrawChip(context, anchor, Anchor::Center, "不要", chip);
        context.PopOpacity();
    }
}

void GameScene::DrawToast(graphics::RenderContext& context) {
    if (toastText_.empty() || toastAge_ >= ToastSeconds) {
        return;
    }
    const float alpha = Clamp01(toastAge_ * 5.0f) * Clamp01((ToastSeconds - toastAge_) * 2.0f);
    context.PushOpacity(alpha);
    ChipStyle chip;
    chip.fontSize = 15.0f;
    chip.height = 34.0f;
    chip.padX = 20.0f;
    chip.fill = WithAlpha(theme::Ink, 0.9f);
    chip.stroke = WithAlpha(theme::Gold, 0.4f);
    const Point anchor{640.0f, 446.0f + (1.0f - EaseOutCubic(Clamp01(toastAge_ * 4.0f))) * 8.0f};
    DrawChip(context, anchor, Anchor::Center, toastText_, chip);
    context.PopOpacity();
}

void GameScene::DrawDealPile(graphics::RenderContext& context) {
    if (handsSorted_) {
        return;
    }
    const int remaining = std::max(0, DealSoundCount - static_cast<int>(dealElapsed_ / DealCardInterval));
    const int layers = std::min(4, (remaining + 3) / 4);
    for (int i = 0; i < layers; ++i) {
        const float offset = static_cast<float>(layers - 1 - i) * 2.5f;
        CardLook look;
        look.rotation = static_cast<float>(i % 2 == 0 ? -2 : 3);
        DrawCardBack(context, app_.CardAtlas(),
            {DealCenter.x - PlayerCardWidth * 0.5f + offset, DealCenter.y - PlayerCardHeight * 0.5f + offset, PlayerCardWidth, PlayerCardHeight}, look);
    }
}

void GameScene::DrawBombEffect(graphics::RenderContext& context) {
    if (bombAnimation_ <= 0.0f) {
        return;
    }
    const float p = 1.0f - bombAnimation_;
    const float fade = bombAnimation_;
    const Point center{640.0f, 290.0f};
    context.FillRect({0.0f, 0.0f, LogicalWidth, LogicalHeight}, context.Radial(center, 820.0f, 600.0f,
        {{0.0f, Rgb(0xFFD58A, 0.5f * fade * fade * fade)}, {0.45f, Rgb(0xC23B2E, 0.18f * fade * fade)}, {1.0f, Rgb(0xC23B2E, 0.0f)}}));

    const float ring = 60.0f + 640.0f * EaseOutCubic(p);
    context.StrokeEllipse({center.x - ring, center.y - ring * 0.62f, ring * 2.0f, ring * 1.24f}, WithAlpha(theme::GoldLight, fade), 1.0f + 6.0f * fade);
    const float ring2 = 40.0f + 480.0f * EaseOutCubic(Clamp01(p * 1.25f - 0.1f));
    context.StrokeEllipse({center.x - ring2, center.y - ring2 * 0.62f, ring2 * 2.0f, ring2 * 1.24f}, WithAlpha(theme::CinnabarLight, fade * 0.8f), 1.0f + 4.0f * fade);

    const float stamp = EaseOutBackWith(Clamp01(p / 0.22f), 1.6f);
    const float sealAlpha = Clamp01(p * 10.0f) * Clamp01((1.0f - p) / 0.35f);
    context.PushOpacity(sealAlpha);
    context.PushScale(Lerp(2.3f, 1.0f, stamp), center);
    DrawSeal(context, center, 128.0f, "炸", theme::Cinnabar, -8.0f, 84.0f);
    context.PopTransform();
    context.PopOpacity();
}

void GameScene::UpdateActionButtons() {
    LayoutActionButtons();
    const bool ready = InteractionReady();
    // Render and hover paths read these cached values only; state-changing
    // callbacks mark the cache dirty when a recompute is needed.
    const bool showActionButtons = !ready || game_.IsHumanTurn();
    for (Button& button : buttons_) {
        button.visible = showActionButtons && !game_.IsRoundOver();
        if (!button.visible) {
            button.hover = false;
        }
    }
    buttons_[0].text = game_.Autoplay() ? "取消托管" : "托管";
    buttons_[0].enabled = ready;
    buttons_[1].enabled = ready && game_.CanCurrentPlayerPass();
    buttons_[2].enabled = ready && game_.IsHumanTurn();
    buttons_[3].enabled = ready && game_.IsHumanTurn() && !game_.SelectedIndices().empty();
    lastInteractionReady_ = ready;
    actionButtonsDirty_ = false;
}

void GameScene::LayoutActionButtons() {
    constexpr float gap = 12.0f;
    float total = -gap;
    for (const Button& button : buttons_) {
        total += button.rect.width + gap;
    }
    float x = 640.0f - total * 0.5f;
    for (Button& button : buttons_) {
        button.rect.x = x;
        button.rect.y = 482.0f;
        x += button.rect.width + gap;
    }
}

bool GameScene::OnMouseMove(float x, float y) {
    backButton_.UpdateHover(x, y);
    ButtonGroup::UpdateHover(buttons_, x, y);
    const int card = HitPlayerCard(x, y);
    if (dragSelecting_ && card >= 0 && InteractionReady()) {
        if (dragPath_.empty() || dragPath_.back() != card) {
            dragPath_.push_back(card);
        }
        if (card != dragStartCard_) {
            dragMoved_ = true;
        }
    }
    hoverCard_ = card;
    return true;
}

bool GameScene::OnMouseDown(float x, float y) {
    if (backButton_.HitTest(x, y)) {
        backButton_.pressT = 1.0f;
        app_.Audio().Play(audio::SoundId::ButtonClick);
        app_.PushOverlay(std::make_unique<overlays::ReturnToMenuOverlay>(app_));
        return true;
    }
    if (!InteractionReady()) {
        return true;
    }
    const int card = HitPlayerCard(x, y);
    if (card >= 0) {
        dragSelecting_ = true;
        dragMoved_ = false;
        dragStartCard_ = card;
        dragPath_.clear();
        dragPath_.push_back(card);
        hoverCard_ = card;
        return true;
    }

    if (actionButtonsDirty_) {
        UpdateActionButtons();
    }
    const int hit = ButtonGroup::Hit(buttons_, x, y);
    if (hit < 0) {
        return false;
    }
    if (hit == 0) {
        game_.ToggleAutoplay();
        app_.Audio().Play(audio::SoundId::ButtonClick);
    } else if (hit == 1) {
        game_.PassHuman();
    } else if (hit == 2) {
        game_.ApplyHint();
    } else if (hit == 3) {
        game_.PlaySelected();
    }
    ConsumeEvents();
    actionButtonsDirty_ = true;
    UpdateActionButtons();
    return true;
}

bool GameScene::OnMouseUp(float x, float y) {
    if (!dragSelecting_) {
        return true;
    }
    const int card = HitPlayerCard(x, y);
    if (card >= 0) {
        if (dragPath_.empty() || dragPath_.back() != card) {
            dragPath_.push_back(card);
        }
        if (card != dragStartCard_) {
            dragMoved_ = true;
        }
    }

    if (InteractionReady()) {
        if (dragMoved_ && dragPath_.size() > 1) {
            if (game_.SelectBestPatternFromDraggedCards(dragPath_)) {
                app_.Audio().Play(audio::SoundId::SelectCard);
            } else {
                app_.Audio().Play(audio::SoundId::InvalidMove);
            }
        } else if (dragStartCard_ >= 0) {
            const bool wasSelected = game_.SelectedIndices().contains(dragStartCard_);
            game_.TogglePlayerCard(dragStartCard_);
            app_.Audio().Play(wasSelected ? audio::SoundId::DeselectCard : audio::SoundId::SelectCard);
        }
        actionButtonsDirty_ = true;
    }

    dragSelecting_ = false;
    dragMoved_ = false;
    dragStartCard_ = -1;
    dragPath_.clear();
    if (actionButtonsDirty_) {
        UpdateActionButtons();
    }
    return true;
}

bool GameScene::InteractionReady() const {
    return handsSorted_ && sortAnimation_ <= 0.0f && !game_.IsRoundOver();
}

int GameScene::HitPlayerCard(float x, float y) const {
    const auto& hand = game_.Players()[0].hand;
    for (int i = static_cast<int>(hand.size()) - 1; i >= 0; --i) {
        Rect rect = CardRect(i);
        if (game_.SelectedIndices().contains(i)) {
            rect.y -= layout::SelectedLift;
        }
        if (Rect_Contains(&rect, x, y)) {
            return i;
        }
    }
    return -1;
}

Rect GameScene::CardRect(int index) const {
    const auto& hand = game_.Players()[0].hand;
    return CardRectFor(index, static_cast<int>(hand.size()));
}

Rect GameScene::CardRectFor(int index, int count) const {
    const float step = VisibleStepForWidth(PlayerCardWidth);
    const float totalW = CardRowWidth(count, PlayerCardWidth);
    const float startX = 640.0f - totalW * 0.5f;
    return {startX + static_cast<float>(index) * step, layout::HandBottom - PlayerCardHeight, PlayerCardWidth, PlayerCardHeight};
}

Rect GameScene::AiCardRectFor(rules::PlayerId player, int index, int count) const {
    const Rect info = layout::AiInfoArea(player);
    const float cardH = layout::AiCardHeight;
    const float cardW = cardH / 1.4f;
    const float step = (info.width - cardW) / static_cast<float>(FullHandCardCount - 1);
    const float y = info.y + 36.0f;
    if (player == PLAYER_AI2) {
        // Right-aligned so the remaining cards stay next to AI2's avatar.
        const float right = info.x + info.width - cardW;
        return {right - static_cast<float>(count - 1 - index) * step, y, cardW, cardH};
    }
    return {info.x + static_cast<float>(index) * step, y, cardW, cardH};
}

void GameScene::ConsumeEvents() {
    for (const game::GameEvent& event : game_.Events()) {
        switch (event.type) {
        case game::GameEventType::RoundStarted:
            break;
        case game::GameEventType::CardsPlayed:
            app_.Audio().Play(audio::SoundId::PlayCards);
            playAnimation_ = 1.0f;
            lastAnimatedPlayer_ = event.player;
            passTimers_[static_cast<std::size_t>(rules::PlayerIndex(event.player))] = 0.0f;
            break;
        case game::GameEventType::Passed:
            app_.Audio().Play(audio::SoundId::Pass);
            passTimers_[static_cast<std::size_t>(rules::PlayerIndex(event.player))] = PassChipSeconds;
            break;
        case game::GameEventType::InvalidMove:
            app_.Audio().Play(audio::SoundId::InvalidMove);
            break;
        case game::GameEventType::Hint:
            app_.Audio().Play(audio::SoundId::Hint);
            break;
        case game::GameEventType::Bomb:
            app_.Audio().Play(audio::SoundId::Bomb);
            bombAnimation_ = 1.0f;
            break;
        case game::GameEventType::RoundEnded:
            app_.Audio().Play(audio::SoundId::RoundEnd);
            app_.Audio().Play(event.player == PLAYER_HUMAN ? audio::SoundId::Win : audio::SoundId::Lose);
            roundResultPending_ = true;
            roundResultDelay_ = 0.0f;
            break;
        case game::GameEventType::Talk:
            if (event.player != PLAYER_HUMAN) {
                app_.Audio().Play(audio::SoundId::AiTalk);
                app_.PushOverlay(std::make_unique<overlays::TalkBubbleOverlay>(event.player, event.message));
            } else {
                app_.Audio().Play(audio::SoundId::TurnPrompt);
            }
            break;
        case game::GameEventType::None:
            break;
        }
    }
    game_.ClearEvents();
}

void GameScene::UpdateRoundResultDelay(float dt) {
    if (!roundResultPending_ || recordedRound_) {
        return;
    }

    // Keep the final table visible briefly so the last played cards and revealed
    // AI hands can be read before the modal result screen covers them.
    roundResultDelay_ += dt;
    if (roundResultDelay_ >= RoundResultDelaySeconds) {
        ShowRoundResultOverlay();
    }
}

void GameScene::ShowRoundResultOverlay() {
    if (!game_.IsRoundOver() || recordedRound_) {
        return;
    }

    const stats::RoundRecord record = game_.LastRoundRecord();
    if (!app_.ViewerMode()) {
        app_.Recorder().AppendToday(record);
    }
    for (int i = 0; i < 3; ++i) {
        todayScores_[i] += record.scores[i];
    }
    app_.PushOverlay(std::make_unique<overlays::RoundResultOverlay>(app_, record));
    recordedRound_ = true;
    roundResultPending_ = false;
}

} // namespace pdk::scenes
