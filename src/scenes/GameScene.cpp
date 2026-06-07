#include "scenes/GameScene.h"

#include "app/App.h"
#include "audio/SoundIds.h"
#include "overlays/ReturnToMenuOverlay.h"
#include "overlays/RoundResultOverlay.h"
#include "overlays/TalkBubbleOverlay.h"
#include "stats/StatStore.h"

#include <algorithm>
#include <sstream>

namespace pdk::scenes {
namespace {

bool ContainsIndex(const std::vector<int>& values, int index) {
    return std::find(values.begin(), values.end(), index) != values.end();
}

constexpr D2D1_COLOR_F TableGreen = {0.04f, 0.30f, 0.18f, 1.0f};
constexpr float DealCardInterval = 1.0f / 6.0f;
constexpr int DealSoundCount = 16;
constexpr int FullHandCardCount = 16;
constexpr float FixedCardScale = 1.5f;
constexpr float DealSourceCenterX = 640.0f;
constexpr float DealSourceCenterY = 330.0f;
constexpr float SortAnimationSpeed = 2.4f;
constexpr float RoundResultDelaySeconds = 0.75f;
constexpr float PlayerCardBaseWidth = 70.0f;
constexpr float PlayerCardBaseHeight = 98.0f;
constexpr float AiFullHandHorizontalPadding = 52.0f;
constexpr float AiCardTopOffset = 45.0f;

std::string PlayerName(rules::PlayerId id) {
    switch (id) {
    case rules::PlayerId::Player: return "玩家";
    case rules::PlayerId::Ai1: return "AI1";
    case rules::PlayerId::Ai2: return "AI2";
    }
    return "";
}

float Lerp(float from, float to, float t) {
    return from + (to - from) * std::clamp(t, 0.0f, 1.0f);
}

float AtlasAspectRatio() {
    const resources::CardAtlasInfo& info = resources::GetCardAtlasInfo();
    return static_cast<float>(info.cardHeight) / static_cast<float>(info.cardWidth);
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

} // namespace

GameScene::GameScene(app::App& app, bool mock) : app_(app), mock_(mock) {
    backButton_ = {{24.0f, 24.0f, 44.0f, 44.0f}, "⬅️", ButtonShape::Circle, 24.0f};
    buttons_ = {
        {{790.0f, 612.0f, 100.0f, 42.0f}, "托管"},
        {{902.0f, 612.0f, 90.0f, 42.0f}, "不要"},
        {{1004.0f, 612.0f, 90.0f, 42.0f}, "提示"},
        {{1106.0f, 612.0f, 100.0f, 42.0f}, "出牌"}
    };
}

void GameScene::OnEnter() {
    if (!app_.CardAtlas().Loaded()) {
        app_.LoadGameResources();
    }
    game_.StartNewRound(app_.Settings().playerName, mock_ ? 20260606u : std::random_device{}());
    recordedRound_ = false;
    roundResultPending_ = false;
    todayScores_ = stats::StatStore().SummarizeDay(stats::TodayDateKey()).scores;
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
    backButton_.hover = false;
    dragStartCard_ = -1;
    dragPath_.clear();
    lastAnimatedPlayer_ = rules::PlayerId::Player;
    actionButtonsDirty_ = true;
    lastInteractionReady_ = InteractionReady();
    UpdateActionButtons();
}

void GameScene::Update(float dt) {
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
    playAnimation_ = std::max(0.0f, playAnimation_ - dt * 3.2f);
    bombAnimation_ = std::max(0.0f, bombAnimation_ - dt * 2.4f);
    if (InteractionReady()) {
        game_.Update(dt);
    }
    const bool hadEvents = !game_.Events().empty();
    ConsumeEvents();
    UpdateRoundResultDelay(dt);

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

void GameScene::Render(graphics::RenderContext& context) {
    if (!app_.CardAtlas().Loaded()) {
        app_.LoadGameResources();
    }
    context.Clear(TableGreen);
    context.FillRect({0.0f, 0.0f, 1280.0f, 720.0f}, TableGreen);
    context.FillEllipse({70.0f, 88.0f, 1140.0f, 470.0f}, TableGreen);
    context.StrokeEllipse({70.0f, 88.0f, 1140.0f, 470.0f}, Color(0.72f, 0.64f, 0.34f, 0.55f), 3.0f);

    DrawAiArea(context, rules::PlayerId::Ai1, {95.0f, 24.0f, 400.0f, 132.0f});
    DrawAiArea(context, rules::PlayerId::Ai2, {785.0f, 24.0f, 400.0f, 132.0f});
    if (bombAnimation_ > 0.0f) {
        const float pulse = 1.0f + bombAnimation_ * 0.08f;
        context.StrokeEllipse({70.0f - 8.0f * pulse, 88.0f - 8.0f * pulse, 1140.0f + 16.0f * pulse, 470.0f + 16.0f * pulse}, Color(0.98f, 0.74f, 0.18f, bombAnimation_), 5.0f);
    }
    DrawPlayedCards(context);
    DrawPlayerHand(context);
    DrawDealPile(context);

    ButtonGroup::DrawAll(context, buttons_);
    backButton_.Draw(context);

    const std::string current = "当前轮到: " + PlayerName(game_.CurrentPlayer());
    context.DrawTextUtf8(current, {510.0f, 28.0f, 260.0f, 36.0f}, 21.0f, Color(0.95f, 0.96f, 0.83f), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    std::ostringstream playerInfo;
    playerInfo << app_.Settings().playerName << "  今日分 " << todayScores_[0];
    context.DrawTextUtf8(playerInfo.str(), {60.0f, 676.0f, 300.0f, 32.0f}, 20.0f, Color(0.95f, 0.96f, 0.83f));
    if (game_.LastPattern()) {
        context.DrawTextUtf8("上家: " + PlayerName(game_.LastMovePlayer()) + " " + rules::PatternDescription(*game_.LastPattern()),
            {390.0f, 92.0f, 520.0f, 32.0f}, 18.0f, Color(0.87f, 0.94f, 0.82f), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    if (!game_.Toast().empty()) {
        context.FillRect({350.0f, 626.0f, 580.0f, 34.0f}, Color(0.08f, 0.13f, 0.11f, 0.82f));
        context.DrawTextUtf8(game_.Toast(), {365.0f, 630.0f, 550.0f, 26.0f}, 17.0f, Color(0.96f, 0.94f, 0.80f), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
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

void GameScene::DrawPlayerHand(graphics::RenderContext& context) {
    const auto& hand = game_.Players()[0].hand;
    for (int i = 0; i < static_cast<int>(hand.size()); ++i) {
        const bool selected = game_.SelectedIndices().contains(i);
        const bool hint = ContainsIndex(game_.HintIndices(), i);
        const bool inDragPath = dragSelecting_ && ContainsIndex(dragPath_, i);
        core::Rect rect = CardRect(i);
        if (!handsSorted_) {
            const float t = std::clamp((dealElapsed_ - static_cast<float>(i) * DealCardInterval) / DealCardInterval, 0.0f, 1.0f);
            rect.x = Lerp(DealSourceCenterX - rect.width * 0.5f, rect.x, t);
            rect.y = Lerp(DealSourceCenterY - rect.height * 0.5f, rect.y, t);
            DrawCard(context, app_.CardAtlas(), hand[static_cast<std::size_t>(i)], rect);
            continue;
        } else if (sortAnimation_ > 0.0f) {
            const rules::Cards& oldHand = handsBeforeSort_[rules::PlayerIndex(rules::PlayerId::Player)];
            const int oldIndex = FindCardIndex(oldHand, hand[static_cast<std::size_t>(i)]);
            if (oldIndex >= 0) {
                const core::Rect from = CardRectFor(oldIndex, static_cast<int>(oldHand.size()));
                const float t = 1.0f - sortAnimation_;
                rect.x = Lerp(from.x, rect.x, t);
                rect.y = Lerp(from.y, rect.y, t);
            }
        }
        DrawCard(context, app_.CardAtlas(), hand[static_cast<std::size_t>(i)], rect, selected, hoverCard_ == i, hint || inDragPath);
    }
}

void GameScene::DrawPlayedCards(graphics::RenderContext& context) {
    const auto& cards = game_.LastCards();
    if (cards.empty()) {
        context.DrawTextUtf8("桌面等待出牌", {430.0f, 300.0f, 420.0f, 34.0f}, 22.0f, Color(0.70f, 0.84f, 0.72f), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        return;
    }
    const float cardW = 87.0f;
    const float cardH = 123.0f;
    const float step = VisibleStepForWidth(cardW);
    const float totalW = CardRowWidth(static_cast<int>(cards.size()), cardW);
    float x = 640.0f - totalW * 0.5f;
    core::Point source{640.0f, 562.0f};
    if (lastAnimatedPlayer_ == rules::PlayerId::Ai1) {
        source = {275.0f, 118.0f};
    } else if (lastAnimatedPlayer_ == rules::PlayerId::Ai2) {
        source = {1005.0f, 118.0f};
    }
    const float t = 1.0f - playAnimation_;
    const float scale = playAnimation_ > 0.0f ? Lerp(0.35f, 1.0f, t) : 1.0f;
    for (rules::Card card : cards) {
        core::Rect finalRect{x, 255.0f, cardW, cardH};
        core::Rect drawRect = finalRect;
        if (playAnimation_ > 0.0f) {
            const float finalCenterX = finalRect.x + finalRect.width * 0.5f;
            const float finalCenterY = finalRect.y + finalRect.height * 0.5f;
            const float cx = Lerp(source.x, finalCenterX, t);
            const float cy = Lerp(source.y, finalCenterY, t);
            drawRect.width = finalRect.width * scale;
            drawRect.height = finalRect.height * scale;
            drawRect.x = cx - drawRect.width * 0.5f;
            drawRect.y = cy - drawRect.height * 0.5f;
        }
        DrawCard(context, app_.CardAtlas(), card, drawRect);
        x += step;
    }
}

void GameScene::DrawAiArea(graphics::RenderContext& context, rules::PlayerId player, const core::Rect& area) {
    const auto& state = game_.Players()[rules::PlayerIndex(player)];
    DrawPanel(context, area, Color(0.04f, 0.15f, 0.13f, 0.88f));
    std::ostringstream text;
    text << state.name << "  剩 " << state.hand.size() << " 张  今日分 " << todayScores_[rules::PlayerIndex(player)];
    if (game_.CurrentPlayer() == player) {
        text << "  思考中";
    }
    context.DrawTextUtf8(text.str(), {area.x + 16.0f, area.y + 12.0f, area.width - 32.0f, 28.0f}, 19.0f, Color(0.94f, 0.96f, 0.84f));
    const int count = static_cast<int>(state.hand.size());
    for (int i = 0; i < count; ++i) {
        core::Rect target = AiCardRectFor(i, count, area);
        if (!handsSorted_) {
            const float t = std::clamp((dealElapsed_ - static_cast<float>(i) * DealCardInterval) / DealCardInterval, 0.0f, 1.0f);
            target.x = Lerp(DealSourceCenterX - target.width * 0.5f, target.x, t);
            target.y = Lerp(DealSourceCenterY - target.height * 0.5f, target.y, t);
        } else if (sortAnimation_ > 0.0f) {
            const int playerIndex = rules::PlayerIndex(player);
            const rules::Cards& oldHand = handsBeforeSort_[playerIndex];
            const int oldIndex = FindCardIndex(oldHand, state.hand[static_cast<std::size_t>(i)]);
            if (oldIndex >= 0) {
                // AI cards stay face-down, but their backs still move from the
                // dealt order to the sorted order so all hands feel consistent.
                const core::Rect from = AiCardRectFor(oldIndex, static_cast<int>(oldHand.size()), area);
                const float t = 1.0f - sortAnimation_;
                target.x = Lerp(from.x, target.x, t);
                target.y = Lerp(from.y, target.y, t);
            }
        }
        // After the round ends the result overlay is intentionally delayed, so
        // reveal the AI leftovers here to let the player inspect what was held.
        if (game_.IsRoundOver()) {
            DrawCard(context, app_.CardAtlas(), state.hand[static_cast<std::size_t>(i)], target);
        } else {
            DrawCardBack(context, app_.CardAtlas(), target);
        }
    }
}

void GameScene::DrawDealPile(graphics::RenderContext& context) {
    if (handsSorted_) {
        return;
    }
    const float cardW = 70.0f * FixedCardScale;
    const float cardH = 98.0f * FixedCardScale;
    DrawCardBack(context, app_.CardAtlas(), {DealSourceCenterX - cardW * 0.5f, DealSourceCenterY - cardH * 0.5f, cardW, cardH});
}

void GameScene::UpdateActionButtons() {
    LayoutActionButtons();
    const bool ready = InteractionReady();
    // Render and hover paths read these cached values only; state-changing
    // callbacks mark the cache dirty when a recompute is needed.
    const bool showActionButtons = !ready || game_.IsHumanTurn();
    for (Button& button : buttons_) {
        button.visible = showActionButtons;
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
    const core::Rect first = CardRectFor(0, FullHandCardCount);
    const float x = first.x;
    const float y = first.y - 52.0f;
    buttons_[0].rect = {x, y, 100.0f, 42.0f};
    buttons_[1].rect = {x + 112.0f, y, 90.0f, 42.0f};
    buttons_[2].rect = {x + 214.0f, y, 90.0f, 42.0f};
    buttons_[3].rect = {x + 316.0f, y, 100.0f, 42.0f};
}

bool GameScene::InteractionReady() const {
    return handsSorted_ && sortAnimation_ <= 0.0f && !game_.IsRoundOver();
}

int GameScene::HitPlayerCard(float x, float y) const {
    const auto& hand = game_.Players()[0].hand;
    for (int i = static_cast<int>(hand.size()) - 1; i >= 0; --i) {
        core::Rect rect = CardRect(i);
        if (game_.SelectedIndices().contains(i)) {
            rect.y -= 18.0f;
        }
        if (rect.Contains(x, y)) {
            return i;
        }
    }
    return -1;
}

core::Rect GameScene::CardRect(int index) const {
    const auto& hand = game_.Players()[0].hand;
    return CardRectFor(index, static_cast<int>(hand.size()));
}

core::Rect GameScene::CardRectFor(int index, int count) const {
    const float scale = FixedCardScale;
    const float cardW = PlayerCardBaseWidth * scale;
    const float cardH = PlayerCardBaseHeight * scale;
    const float step = VisibleStepForWidth(cardW);
    const float totalW = CardRowWidth(count, cardW);
    const float startX = 640.0f - totalW * 0.5f;
    return {startX + index * step, 612.0f - cardH, cardW, cardH};
}

core::Rect GameScene::AiCardRectFor(int index, int, const core::Rect& area) const {
    const resources::CardAtlasInfo& info = resources::GetCardAtlasInfo();
    const float visibleRatio = static_cast<float>(info.mainX) / static_cast<float>(info.cardWidth);
    const float fullHandWidth = area.width - AiFullHandHorizontalPadding;
    const float cardW = fullHandWidth / (1.0f + static_cast<float>(FullHandCardCount - 1) * visibleRatio);
    const float cardH = cardW * AtlasAspectRatio();
    const float step = VisibleStepForWidth(cardW);
    const float x = area.x + (AiFullHandHorizontalPadding * 0.5f) + static_cast<float>(index) * step;
    return {x, area.y + AiCardTopOffset, cardW, cardH};
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
            break;
        case game::GameEventType::Passed:
            app_.Audio().Play(audio::SoundId::Pass);
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
            app_.Audio().Play(event.player == rules::PlayerId::Player ? audio::SoundId::Win : audio::SoundId::Lose);
            roundResultPending_ = true;
            roundResultDelay_ = 0.0f;
            break;
        case game::GameEventType::Talk:
            if (event.player != rules::PlayerId::Player) {
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
