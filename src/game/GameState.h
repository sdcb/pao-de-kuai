#pragma once

#include "game/AiPlayer.h"
#include "game/ExternalAiController.h"
#include "game/Player.h"
#include "rules/PaoDeKuaiRules.h"
#include "stats/DailyStat.h"

#include <array>
#include <memory>
#include <optional>
#include <set>
#include <string>
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

enum class TalkKind {
    NormalPlay,
    Pass,
    BombPlay,
    AlmostOut,
    ForcedBreakGoodHand,
    CannotBeatBigMove,
    BigMoveTaunt,
    HumanGoodBomb,
    HumanGoodStraight,
    HumanGoodPlane,
    HumanGoodConsecutivePairs,
    RoundEndGoodStraight,
    RoundEndGoodPlane,
    RoundEndGoodBomb,
    RoundEndGoodConsecutivePairs,
    Count
};

class GameState {
public:
    GameState();

    void StartNewRound(const std::string& playerName, unsigned seed = 0);
    void Update(float dt);

    bool IsRoundOver() const { return roundOver_; }
    bool IsHumanTurn() const { return currentPlayer_ == rules::PlayerId::Player && !roundOver_; }
    rules::PlayerId CurrentPlayer() const { return currentPlayer_; }
    const std::array<PlayerState, 3>& Players() const { return players_; }
    const rules::Cards& LastCards() const { return lastCards_; }
    const std::optional<rules::HandPattern>& LastPattern() const { return lastPattern_; }
    rules::PlayerId LastMovePlayer() const { return lastMovePlayer_; }
    const rules::Cards& PlayedCards() const { return playedCards_; }
    const std::array<std::optional<PassObservation>, 3>& PassObservations() const { return passObservations_; }
    const std::set<int>& SelectedIndices() const { return selectedIndices_; }
    const std::vector<int>& HintIndices() const { return hintIndices_; }
    const std::vector<GameEvent>& Events() const { return events_; }
    const std::string& Toast() const { return toast_; }
    const std::string& TalkText() const { return talkText_; }
    rules::PlayerId TalkPlayer() const { return talkPlayer_; }
    bool Autoplay() const { return autoplay_; }
    const stats::RoundRecord& LastRoundRecord() const { return lastRoundRecord_; }
    const std::vector<TurnRecord>& TurnRecords() const { return turnRecords_; }
    bool ExternalAiPending() const { return externalAiPending_; }
    bool RemoteAiPending() const;
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
    void SetExternalAiController(std::shared_ptr<ExternalAiController> controller);
    void SetExternalAiControllers(std::vector<std::shared_ptr<ExternalAiController>> controllers);
    void SetLocalAiStrategy(rules::PlayerId player, std::unique_ptr<AiStrategy> strategy);

    void TestSetRound(
        const std::array<rules::Cards, 3>& hands,
        rules::PlayerId currentPlayer,
        const std::optional<rules::HandPattern>& previousPattern,
        rules::PlayerId lastMovePlayer);

private:
    bool CurrentPlayerLeads() const;
    AiContext MakeAiContext(rules::PlayerId player) const;
    bool HasPlayableFollow(rules::PlayerId player) const;
    std::vector<std::pair<rules::Cards, rules::HandPattern>> LegalMoves(rules::PlayerId player) const;
    TurnSnapshot Snapshot() const;
    GameAction ActionFromCards(const rules::Cards& cards, bool pass = false, std::string talk = {}) const;
    TurnRecord BuildTurnRecord(
        const TurnSnapshot& before,
        rules::PlayerId actor,
        TurnDecisionSource source,
        TurnDecisionReason reason,
        const GameAction& requested,
        const GameAction& final,
        const rules::Cards& finalCards,
        const std::optional<rules::HandPattern>& finalPattern,
        bool accepted,
        const std::string& validationMessage,
        TurnDecisionTrace trace) const;
    void AppendRecord(TurnRecord record);
    TurnDecisionTrace SyntheticTrace(const TurnRecord& record) const;
    std::shared_ptr<ExternalAiController> AiControllerFor(rules::PlayerId player) const;
    bool UsesRemoteAi(rules::PlayerId player) const;
    bool ApplyExternalAiResult(const ExternalAiResult& result);
    bool ApplyLocalAiResult(const AiMoveChoice& choice, TurnDecisionSource source);
    void StartExternalAiTurn();
    bool TryCompleteExternalAiTurn();
    void PlayLocalAiTurn(rules::PlayerId player);
    float NextThinkDelay();
    void AdvanceTurn();
    void RecordPassObservation(rules::PlayerId player, const rules::HandPattern& pattern);
    void PlayCards(rules::PlayerId player, const rules::Cards& cards, const rules::HandPattern& pattern, int disruptionPenalty = 0);
    bool Pass(rules::PlayerId player);
    void FinishRound(rules::PlayerId winner);
    void RemoveCardsFromHand(rules::PlayerId player, const rules::Cards& cards);
    rules::Cards SelectedCards() const;
    void AddEvent(GameEvent event);
    void MaybeTalk(rules::PlayerId player, TalkKind kind, bool force = false);
    void MaybeTalkAboutHumanMove(const rules::HandPattern& pattern);
    void MaybeTalkAboutRoundEndGoodHands(rules::PlayerId winner);
    std::string ChooseTalkText(TalkKind kind);

    rules::PaoDeKuaiRules rules_;
    std::array<PlayerState, 3> players_;
    std::array<AiPlayer, 3> aiPlayers_;
    std::vector<std::shared_ptr<ExternalAiController>> externalAiControllers_;
    std::shared_ptr<ExternalAiController> activeExternalAi_;
    rules::PlayerId currentPlayer_{rules::PlayerId::Player};
    rules::PlayerId lastMovePlayer_{rules::PlayerId::Player};
    rules::PlayerId trickLeader_{rules::PlayerId::Player};
    rules::PlayerId roundLeader_{rules::PlayerId::Player};
    std::optional<rules::HandPattern> lastPattern_;
    std::optional<rules::PlayerId> nextRoundLeader_;
    rules::Cards lastCards_;
    rules::Cards playedCards_;
    std::array<std::optional<PassObservation>, 3> passObservations_{};
    std::array<std::vector<PassObservation>, 3> passHistory_{};
    int passCount_{0};
    bool roundOver_{true};
    bool autoplay_{false};
    float aiDelay_{0.0f};
    float talkCooldown_{0.0f};
    std::set<int> selectedIndices_;
    std::vector<int> hintIndices_;
    std::vector<rules::BombScoreEvent> bombs_;
    std::string startedAt_;
    std::string playerName_;
    std::vector<GameEvent> events_;
    std::string toast_;
    std::string talkText_;
    rules::PlayerId talkPlayer_{rules::PlayerId::Ai1};
    stats::RoundRecord lastRoundRecord_;
    std::array<int, static_cast<std::size_t>(TalkKind::Count)> lastTalkIndices_{};
    std::vector<TurnRecord> turnRecords_;
    bool externalAiPending_{false};
    int nextTurnNo_{1};
};

} // namespace pdk::game
