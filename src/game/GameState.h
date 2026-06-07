#pragma once

#include "game/AiPlayer.h"
#include "game/Player.h"
#include "rules/PaoDeKuaiRules.h"
#include "stats/DailyStat.h"

#include <array>
#include <optional>
#include <random>
#include <set>
#include <vector>

namespace pdk::game {

enum class GameEventType {
    None,
    RoundStarted,
    CardsPlayed,
    Passed,
    InvalidMove,
    Hint,
    Bomb,
    RoundEnded,
    Talk
};

struct GameEvent {
    GameEventType type{GameEventType::None};
    rules::PlayerId player{rules::PlayerId::Player};
    std::string message;
    rules::Cards cards;
};

class GameState {
public:
    GameState();

    void StartNewRound(const std::string& playerName, unsigned seed = std::random_device{}());
    void Update(float dt);

    bool IsRoundOver() const { return roundOver_; }
    bool IsHumanTurn() const { return currentPlayer_ == rules::PlayerId::Player && !roundOver_; }
    rules::PlayerId CurrentPlayer() const { return currentPlayer_; }
    const std::array<PlayerState, 3>& Players() const { return players_; }
    const rules::Cards& LastCards() const { return lastCards_; }
    const std::optional<rules::HandPattern>& LastPattern() const { return lastPattern_; }
    rules::PlayerId LastMovePlayer() const { return lastMovePlayer_; }
    const std::set<int>& SelectedIndices() const { return selectedIndices_; }
    const std::vector<int>& HintIndices() const { return hintIndices_; }
    const std::vector<GameEvent>& Events() const { return events_; }
    const std::string& Toast() const { return toast_; }
    const std::string& TalkText() const { return talkText_; }
    rules::PlayerId TalkPlayer() const { return talkPlayer_; }
    bool Autoplay() const { return autoplay_; }
    const stats::RoundRecord& LastRoundRecord() const { return lastRoundRecord_; }
    bool CanCurrentPlayerPass() const;
    bool IsInLeadState() const { return CurrentPlayerLeads(); }

    void ClearEvents() { events_.clear(); }
    void ToggleAutoplay();
    void TogglePlayerCard(int handIndex);
    void ClearSelection();
    void SortHands();
    bool PlaySelected();
    bool PassHuman();
    bool ApplyHint();
    bool SelectByHoverPattern(int handIndex);
    bool SelectBestPatternFromDraggedCards(const std::vector<int>& handIndices);

    void TestSetRound(
        const std::array<rules::Cards, 3>& hands,
        rules::PlayerId currentPlayer,
        const std::optional<rules::HandPattern>& previousPattern,
        rules::PlayerId lastMovePlayer);

private:
    bool CurrentPlayerLeads() const;
    AiContext MakeAiContext(rules::PlayerId player) const;
    bool HasPlayableFollow(rules::PlayerId player) const;
    float NextThinkDelay();
    void AdvanceTurn();
    void PlayCards(rules::PlayerId player, const rules::Cards& cards, const rules::HandPattern& pattern);
    bool Pass(rules::PlayerId player);
    void FinishRound(rules::PlayerId winner);
    void RemoveCardsFromHand(rules::PlayerId player, const rules::Cards& cards);
    rules::Cards SelectedCards() const;
    void AddEvent(GameEvent event);
    void MaybeTalk(rules::PlayerId player, const std::string& trigger);

    rules::PaoDeKuaiRules rules_;
    std::array<PlayerState, 3> players_;
    std::array<AiPlayer, 3> aiPlayers_;
    rules::PlayerId currentPlayer_{rules::PlayerId::Player};
    rules::PlayerId lastMovePlayer_{rules::PlayerId::Player};
    std::optional<rules::HandPattern> lastPattern_;
    rules::Cards lastCards_;
    int passCount_{0};
    bool roundOver_{true};
    bool autoplay_{false};
    float aiDelay_{0.0f};
    float talkCooldown_{0.0f};
    std::set<int> selectedIndices_;
    std::vector<int> hintIndices_;
    std::vector<rules::BombScoreEvent> bombs_;
    std::mt19937 rng_{std::random_device{}()};
    std::string startedAt_;
    std::string playerName_;
    std::vector<GameEvent> events_;
    std::string toast_;
    std::string talkText_;
    rules::PlayerId talkPlayer_{rules::PlayerId::Ai1};
    stats::RoundRecord lastRoundRecord_;
};

} // namespace pdk::game
