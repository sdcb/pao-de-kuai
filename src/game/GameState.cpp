#include "game/GameState.h"

#include "stats/StatStore.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <sstream>

namespace pdk::game {
namespace {

int Index(rules::PlayerId player) {
    return rules::PlayerIndex(player);
}

rules::PlayerId NextPlayer(rules::PlayerId player) {
    return rules::PlayerFromIndex((Index(player) + 2) % 3);
}

bool SameCard(rules::Card lhs, rules::Card rhs) {
    return lhs.rank == rhs.rank && lhs.suit == rhs.suit;
}

std::string PlayerDisplayName(const std::array<PlayerState, 3>& players, rules::PlayerId player) {
    return players[Index(player)].name;
}

int DragPatternTieBreaker(rules::PatternType type) {
    switch (type) {
    case rules::PatternType::Straight: return 7000;
    case rules::PatternType::Plane: return 6500;
    case rules::PatternType::ConsecutivePairs: return 6000;
    case rules::PatternType::TripleWithPair: return 5000;
    case rules::PatternType::Bomb: return 4000;
    case rules::PatternType::TripleWithOne: return 3000;
    case rules::PatternType::Pair: return 2000;
    case rules::PatternType::Single: return 1000;
    case rules::PatternType::Invalid: break;
    }
    return 0;
}

std::map<rules::Rank, int> CountRanks(const rules::Cards& cards) {
    std::map<rules::Rank, int> counts;
    for (rules::Card card : cards) {
        counts[card.rank]++;
    }
    return counts;
}

bool IsConsecutiveRanks(const std::vector<rules::Rank>& ranks) {
    if (ranks.empty()) {
        return false;
    }
    for (std::size_t i = 1; i < ranks.size(); ++i) {
        if (rules::RankValue(ranks[i]) != rules::RankValue(ranks[i - 1]) + 1) {
            return false;
        }
    }
    return ranks.back() != rules::Rank::Two;
}

rules::Rank MaxRank(const std::vector<rules::Rank>& ranks) {
    return *std::max_element(ranks.begin(), ranks.end(), [](rules::Rank lhs, rules::Rank rhs) {
        return rules::RankValue(lhs) < rules::RankValue(rhs);
    });
}

std::optional<rules::HandPattern> IdentifyDragOnlyPattern(const rules::Cards& cards) {
    const int total = static_cast<int>(cards.size());
    const auto counts = CountRanks(cards);
    if (total == 3 && counts.size() == 1) {
        return rules::HandPattern{rules::PatternType::TripleWithOne, cards.front().rank, total, 1, true};
    }

    if (total < 6 || total % 3 != 0) {
        return std::nullopt;
    }

    std::vector<rules::Rank> tripleRanks;
    tripleRanks.reserve(counts.size());
    for (const auto& [rank, count] : counts) {
        if (rank == rules::Rank::Two || count != 3) {
            return std::nullopt;
        }
        tripleRanks.push_back(rank);
    }
    std::sort(tripleRanks.begin(), tripleRanks.end(), [](rules::Rank lhs, rules::Rank rhs) {
        return rules::RankValue(lhs) < rules::RankValue(rhs);
    });
    if (!IsConsecutiveRanks(tripleRanks)) {
        return std::nullopt;
    }
    return rules::HandPattern{
        rules::PatternType::Plane,
        MaxRank(tripleRanks),
        total,
        static_cast<int>(tripleRanks.size()),
        true
    };
}

std::string DragPatternDescription(const rules::HandPattern& pattern) {
    if (pattern.type == rules::PatternType::TripleWithOne && pattern.cardCount == 3) {
        return "三张 " + rules::RankName(pattern.mainRank);
    }
    if (pattern.type == rules::PatternType::Plane && pattern.cardCount == pattern.groupCount * 3) {
        return "飞机主体 " + rules::RankName(pattern.mainRank);
    }
    return rules::PatternDescription(pattern);
}

} // namespace

GameState::GameState() {
    players_[0].name = "\xE6\x9D\x8E\xE5\xA7\x90";
    players_[1].name = "AI1";
    players_[2].name = "AI2";
}

void GameState::StartNewRound(const std::string& playerName, unsigned seed) {
    playerName_ = playerName.empty() ? "\xE6\x9D\x8E\xE5\xA7\x90" : playerName;
    players_[0] = PlayerState{playerName_, {}, false};
    players_[1] = PlayerState{"AI1", {}, false};
    players_[2] = PlayerState{"AI2", {}, false};
    selectedIndices_.clear();
    hintIndices_.clear();
    bombs_.clear();
    lastCards_.clear();
    lastPattern_.reset();
    passCount_ = 0;
    roundOver_ = false;
    aiDelay_ = 0.45f;
    talkCooldown_ = 0.0f;
    talkText_.clear();
    toast_ = "新一局开始";
    startedAt_ = stats::NowTimeText();
    lastRoundRecord_ = {};

    rules::Cards deck = rules_.CreateDeck();
    rng_.seed(seed);
    std::mt19937 rng(seed);
    rules::Shuffle(deck, rng);
    for (std::size_t i = 0; i < deck.size(); ++i) {
        players_[i % 3].hand.push_back(deck[i]);
    }
    std::vector<rules::Cards> hands;
    for (const PlayerState& player : players_) {
        hands.push_back(player.hand);
    }
    currentPlayer_ = rules::PlayerFromIndex(rules::FindFirstPlayerBySpadeThree(hands));
    lastMovePlayer_ = currentPlayer_;
    aiDelay_ = NextThinkDelay();
    AddEvent(GameEvent{GameEventType::RoundStarted, currentPlayer_, "黑桃 3 玩家先出", {}});
}

void GameState::Update(float dt) {
    if (roundOver_) {
        return;
    }
    if (talkCooldown_ > 0.0f) {
        talkCooldown_ -= dt;
    }

    const bool aiControlled = currentPlayer_ != rules::PlayerId::Player || autoplay_;
    if (!aiControlled) {
        return;
    }

    aiDelay_ -= dt;
    if (aiDelay_ > 0.0f) {
        return;
    }

    AiMoveChoice choice = aiPlayers_[Index(currentPlayer_)].ChooseMove(
        players_[Index(currentPlayer_)].hand,
        MakeAiContext(currentPlayer_));
    if (choice.pass) {
        Pass(currentPlayer_);
    } else {
        PlayCards(currentPlayer_, choice.cards, choice.pattern);
    }
    aiDelay_ = NextThinkDelay();
}

void GameState::ToggleAutoplay() {
    autoplay_ = !autoplay_;
    toast_ = autoplay_ ? "托管已开启" : "托管已取消";
}

void GameState::TogglePlayerCard(int handIndex) {
    if (!IsHumanTurn() || handIndex < 0 || handIndex >= static_cast<int>(players_[0].hand.size())) {
        return;
    }
    hintIndices_.clear();
    if (selectedIndices_.contains(handIndex)) {
        selectedIndices_.erase(handIndex);
    } else {
        selectedIndices_.insert(handIndex);
    }
}

void GameState::ClearSelection() {
    selectedIndices_.clear();
    hintIndices_.clear();
}

void GameState::SortHands() {
    for (PlayerState& player : players_) {
        rules::SortByGameOrder(player.hand);
    }
}

rules::Cards GameState::SelectedCards() const {
    rules::Cards cards;
    const auto& hand = players_[0].hand;
    for (int index : selectedIndices_) {
        if (index >= 0 && index < static_cast<int>(hand.size())) {
            cards.push_back(hand[static_cast<std::size_t>(index)]);
        }
    }
    return cards;
}

bool GameState::PlaySelected() {
    if (!IsHumanTurn()) {
        return false;
    }
    const rules::Cards cards = SelectedCards();
    const int handSize = static_cast<int>(players_[0].hand.size());
    const auto validation = CurrentPlayerLeads()
        ? rules_.ValidateLeadMove(cards, handSize)
        : rules_.ValidateFollowMove(cards, *lastPattern_, handSize);
    if (!validation.ok) {
        toast_ = validation.reason;
        AddEvent(GameEvent{GameEventType::InvalidMove, rules::PlayerId::Player, validation.reason, cards});
        return false;
    }
    PlayCards(rules::PlayerId::Player, cards, validation.pattern);
    selectedIndices_.clear();
    hintIndices_.clear();
    return true;
}

bool GameState::PassHuman() {
    if (!IsHumanTurn()) {
        return false;
    }
    return Pass(rules::PlayerId::Player);
}

bool GameState::ApplyHint() {
    if (!IsHumanTurn()) {
        return false;
    }
    AiMoveChoice choice = aiPlayers_[0].ChooseMove(players_[0].hand, MakeAiContext(rules::PlayerId::Player));
    if (choice.pass) {
        if (!CurrentPlayerLeads()) {
            return Pass(rules::PlayerId::Player);
        }
        toast_ = choice.reason;
        AddEvent(GameEvent{GameEventType::Hint, rules::PlayerId::Player, choice.reason, {}});
        return false;
    }

    std::set<int> recommendedIndices;
    std::vector<int> recommendedHints;
    for (std::size_t i = 0; i < players_[0].hand.size(); ++i) {
        for (rules::Card card : choice.cards) {
            if (SameCard(players_[0].hand[i], card)) {
                const int index = static_cast<int>(i);
                recommendedIndices.insert(index);
                recommendedHints.push_back(index);
            }
        }
    }

    // Hint click acts like a toggle for the exact recommended cards: switch to
    // the recommendation when selection differs, clear it when already selected.
    if (selectedIndices_ == recommendedIndices) {
        selectedIndices_.clear();
        hintIndices_.clear();
        toast_ = "已取消提示选择";
    } else {
        selectedIndices_ = std::move(recommendedIndices);
        hintIndices_ = std::move(recommendedHints);
        toast_ = "已按 AI 逻辑选中推荐牌";
    }
    AddEvent(GameEvent{GameEventType::Hint, rules::PlayerId::Player, choice.reason, choice.cards});
    return true;
}

bool GameState::SelectByHoverPattern(int handIndex) {
    if (!IsHumanTurn() || handIndex < 0 || handIndex >= static_cast<int>(players_[0].hand.size())) {
        return false;
    }
    return SelectBestPatternFromDraggedCards({handIndex});
}

bool GameState::SelectBestPatternFromDraggedCards(const std::vector<int>& handIndices) {
    if (!IsHumanTurn()) {
        return false;
    }

    struct Candidate {
        rules::Cards cards;
        rules::HandPattern pattern;
        int score{};
    };

    const rules::Cards& hand = players_[0].hand;
    const int n = static_cast<int>(hand.size());
    std::vector<bool> inDragPath(static_cast<std::size_t>(n), false);
    for (int handIndex : handIndices) {
        if (handIndex >= 0 && handIndex < n) {
            inDragPath[static_cast<std::size_t>(handIndex)] = true;
        }
    }
    if (std::none_of(inDragPath.begin(), inDragPath.end(), [](bool value) { return value; })) {
        return false;
    }

    std::vector<Candidate> candidates;
    const std::uint64_t limit = n >= 63 ? 0 : (1ull << n);
    for (std::uint64_t mask = 1; mask < limit; ++mask) {
        rules::Cards cards;
        bool outsideDragPath = false;
        for (int i = 0; i < n; ++i) {
            if ((mask & (1ull << i)) != 0) {
                if (!inDragPath[static_cast<std::size_t>(i)]) {
                    outsideDragPath = true;
                    break;
                }
                cards.push_back(hand[static_cast<std::size_t>(i)]);
            }
        }
        if (outsideDragPath) {
            continue;
        }

        rules::HandPattern pattern;
        const auto validation = rules_.ValidateLeadMove(cards, n);
        if (validation.ok) {
            pattern = validation.pattern;
        } else if (const auto dragOnlyPattern = IdentifyDragOnlyPattern(cards)) {
            // Drag selection is a visual grouping helper, not a promise that the
            // current selection can be played; PlaySelected still enforces rules.
            pattern = *dragOnlyPattern;
        } else {
            continue;
        }

        const int score = pattern.cardCount * 100000
            + DragPatternTieBreaker(pattern.type)
            + rules::RankValue(pattern.mainRank);
        candidates.push_back(Candidate{cards, pattern, score});
    }

    if (candidates.empty()) {
        return false;
    }

    const Candidate& chosen = *std::max_element(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        return lhs.score < rhs.score;
    });
    std::set<int> chosenIndices;
    std::vector<int> chosenHints;
    for (std::size_t i = 0; i < hand.size(); ++i) {
        for (rules::Card card : chosen.cards) {
            if (SameCard(hand[i], card)) {
                const int index = static_cast<int>(i);
                chosenIndices.insert(index);
                chosenHints.push_back(index);
            }
        }
    }

    // Repeating the same drag gesture toggles the selected group off, matching
    // hint-button behavior for an already selected recommendation.
    if (selectedIndices_ == chosenIndices) {
        selectedIndices_.clear();
        hintIndices_.clear();
        toast_ = "已取消拖拽选择";
    } else {
        selectedIndices_ = std::move(chosenIndices);
        hintIndices_ = std::move(chosenHints);
        toast_ = "已按拖拽路线选中 " + DragPatternDescription(chosen.pattern);
    }
    return true;
}

void GameState::TestSetRound(
    const std::array<rules::Cards, 3>& hands,
    rules::PlayerId currentPlayer,
    const std::optional<rules::HandPattern>& previousPattern,
    rules::PlayerId lastMovePlayer) {
    players_[0] = PlayerState{"Tester", hands[0], false};
    players_[1] = PlayerState{"AI1", hands[1], true};
    players_[2] = PlayerState{"AI2", hands[2], true};
    currentPlayer_ = currentPlayer;
    lastMovePlayer_ = lastMovePlayer;
    lastPattern_ = previousPattern;
    lastCards_.clear();
    passCount_ = 0;
    roundOver_ = false;
    autoplay_ = false;
    selectedIndices_.clear();
    hintIndices_.clear();
    bombs_.clear();
    events_.clear();
    toast_.clear();
}

bool GameState::CurrentPlayerLeads() const {
    return !lastPattern_.has_value();
}

bool GameState::CanCurrentPlayerPass() const {
    return IsHumanTurn() && !CurrentPlayerLeads() && !HasPlayableFollow(rules::PlayerId::Player);
}

AiContext GameState::MakeAiContext(rules::PlayerId player) const {
    AiContext context;
    context.leading = CurrentPlayerLeads();
    if (lastPattern_) {
        context.previous = *lastPattern_;
    }
    const int currentIndex = Index(player);
    context.ownRemainingCards = static_cast<int>(players_[currentIndex].hand.size());
    context.currentPlayerIndex = currentIndex;
    for (int i = 0; i < 3; ++i) {
        context.remainingCards[i] = static_cast<int>(players_[i].hand.size());
    }
    context.nextPlayerRemainingCards = static_cast<int>(players_[Index(NextPlayer(player))].hand.size());
    context.minOpponentRemainingCards = std::numeric_limits<int>::max();
    for (int i = 0; i < 3; ++i) {
        if (i != currentIndex) {
            context.minOpponentRemainingCards = std::min(context.minOpponentRemainingCards, context.remainingCards[i]);
        }
    }
    return context;
}

bool GameState::HasPlayableFollow(rules::PlayerId player) const {
    if (CurrentPlayerLeads() || !lastPattern_) {
        return false;
    }
    const rules::Cards& hand = players_[Index(player)].hand;
    // UI callers use this for pass/button state; keep it independent of AI strategy.
    return rules::HasAnyFollowMove(hand, *lastPattern_, static_cast<int>(hand.size()));
}

float GameState::NextThinkDelay() {
    return currentPlayer_ == rules::PlayerId::Player ? 0.35f : 0.75f;
}

void GameState::AdvanceTurn() {
    currentPlayer_ = NextPlayer(currentPlayer_);
    if (currentPlayer_ == rules::PlayerId::Player) {
        AddEvent(GameEvent{GameEventType::Talk, rules::PlayerId::Player, "轮到你", {}});
    }
}

void GameState::PlayCards(rules::PlayerId player, const rules::Cards& cards, const rules::HandPattern& pattern) {
    RemoveCardsFromHand(player, cards);
    players_[Index(player)].hasPlayedCards = true;
    lastCards_ = cards;
    lastPattern_ = pattern;
    lastMovePlayer_ = player;
    passCount_ = 0;

    std::string message = PlayerDisplayName(players_, player) + " 出了 " + rules::PatternDescription(pattern);
    toast_ = message;
    AddEvent(GameEvent{GameEventType::CardsPlayed, player, message, cards});

    if (pattern.type == rules::PatternType::Bomb) {
        bombs_.push_back(rules::BombScoreEvent{player, 20});
        AddEvent(GameEvent{GameEventType::Bomb, player, "炸弹 +20", cards});
        MaybeTalk(player, "bomb");
    } else {
        MaybeTalk(player, "play");
    }

    if (players_[Index(player)].hand.empty()) {
        FinishRound(player);
        return;
    }

    AdvanceTurn();
}

bool GameState::Pass(rules::PlayerId player) {
    if (roundOver_ || CurrentPlayerLeads()) {
        toast_ = "当前需要主动出牌，不能不要";
        AddEvent(GameEvent{GameEventType::InvalidMove, player, toast_, {}});
        return false;
    }
    if (HasPlayableFollow(player)) {
        toast_ = "要得起必须出";
        AddEvent(GameEvent{GameEventType::InvalidMove, player, toast_, {}});
        return false;
    }

    passCount_++;
    const std::string message = PlayerDisplayName(players_, player) + " 不要";
    toast_ = message;
    AddEvent(GameEvent{GameEventType::Passed, player, message, {}});

    if (passCount_ >= 2) {
        currentPlayer_ = lastMovePlayer_;
        lastPattern_.reset();
        lastCards_.clear();
        passCount_ = 0;
        toast_ = PlayerDisplayName(players_, currentPlayer_) + " 重新领出";
        return true;
    }

    AdvanceTurn();
    return true;
}

void GameState::FinishRound(rules::PlayerId winner) {
    roundOver_ = true;
    rules::RoundScoreInput input;
    input.winner = winner;
    input.bombs = bombs_;
    for (int i = 0; i < 3; ++i) {
        input.remainingCards[i] = static_cast<int>(players_[i].hand.size());
        input.hasPlayedCards[i] = players_[i].hasPlayedCards;
    }
    const rules::RoundScoreResult score = rules::CalculateRoundScore(input);

    lastRoundRecord_ = stats::RoundRecord{
        startedAt_,
        stats::NowTimeText(),
        winner,
        playerName_,
        score.scores,
        input.remainingCards,
        bombs_,
        score.spring
    };

    std::ostringstream out;
    out << (winner == rules::PlayerId::Player ? "胜利" : "失败")
        << "  本局分: 玩家 " << score.scores[0]
        << " AI1 " << score.scores[1]
        << " AI2 " << score.scores[2];
    toast_ = out.str();
    AddEvent(GameEvent{GameEventType::RoundEnded, winner, toast_, {}});
}

void GameState::RemoveCardsFromHand(rules::PlayerId player, const rules::Cards& cards) {
    auto& hand = players_[Index(player)].hand;
    for (rules::Card card : cards) {
        const auto it = std::find_if(hand.begin(), hand.end(), [card](rules::Card owned) {
            return SameCard(owned, card);
        });
        if (it != hand.end()) {
            hand.erase(it);
        }
    }
}

void GameState::AddEvent(GameEvent event) {
    events_.push_back(std::move(event));
}

void GameState::MaybeTalk(rules::PlayerId player, const std::string& trigger) {
    if (player == rules::PlayerId::Player || talkCooldown_ > 0.0f) {
        return;
    }
    talkPlayer_ = player;
    if (trigger == "bomb") {
        talkText_ = "看，我有炸弹，没想到吧？";
    } else if (players_[Index(player)].hand.size() <= 3) {
        talkText_ = "别急别急，我马上跑完。";
    } else {
        talkText_ = "这牌我忍半天了！";
    }
    talkCooldown_ = 5.0f;
    AddEvent(GameEvent{GameEventType::Talk, player, talkText_, {}});
}

} // namespace pdk::game
