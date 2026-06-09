#include "game/GameState.h"

#include "stats/StatStore.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <random>
#include <sstream>
#include <utility>

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

std::vector<std::string> RanksOf(const rules::Cards& cards) {
    std::vector<std::string> ranks;
    ranks.reserve(cards.size());
    for (rules::Card card : cards) {
        ranks.push_back(rules::RankName(card.rank));
    }
    return ranks;
}

std::string MoveText(const GameAction& action) {
    if (action.action == "pass") {
        return "不要";
    }
    std::ostringstream out;
    out << "出";
    for (const std::string& rank : action.ranks) {
        out << ' ' << rank;
    }
    return out.str();
}

std::optional<rules::Rank> ParseRank(const std::string& rank) {
    static const std::map<std::string, rules::Rank> ranks{
        {"3", rules::Rank::Three}, {"4", rules::Rank::Four}, {"5", rules::Rank::Five},
        {"6", rules::Rank::Six}, {"7", rules::Rank::Seven}, {"8", rules::Rank::Eight},
        {"9", rules::Rank::Nine}, {"10", rules::Rank::Ten}, {"J", rules::Rank::Jack},
        {"Q", rules::Rank::Queen}, {"K", rules::Rank::King}, {"A", rules::Rank::Ace},
        {"2", rules::Rank::Two}
    };
    const auto it = ranks.find(rank);
    return it == ranks.end() ? std::nullopt : std::optional<rules::Rank>(it->second);
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

bool IsAi(rules::PlayerId player) {
    return player != rules::PlayerId::Player;
}

bool IsForceTalk(TalkKind kind) {
    switch (kind) {
    case TalkKind::BombPlay:
    case TalkKind::ForcedBreakGoodHand:
    case TalkKind::CannotBeatBigMove:
    case TalkKind::BigMoveTaunt:
    case TalkKind::HumanGoodBomb:
    case TalkKind::HumanGoodStraight:
    case TalkKind::HumanGoodPlane:
    case TalkKind::HumanGoodConsecutivePairs:
    case TalkKind::RoundEndGoodStraight:
    case TalkKind::RoundEndGoodPlane:
    case TalkKind::RoundEndGoodBomb:
    case TalkKind::RoundEndGoodConsecutivePairs:
        return true;
    case TalkKind::NormalPlay:
    case TalkKind::Pass:
    case TalkKind::AlmostOut:
    case TalkKind::Count:
        return false;
    }
    return false;
}

const std::vector<std::string>& TalkPool(TalkKind kind) {
    static const std::vector<std::string> normalPlay{
        "这牌我忍半天了！",
        "轮到我了，看我走一手。",
        "别眨眼，我这手有点讲究。"
    };
    static const std::vector<std::string> pass{
        "先不要，你们继续。",
        "这手我让一让。",
        "过了过了，别看我。"
    };
    static const std::vector<std::string> bombPlay{
        "看，我有炸弹，没想到吧？",
        "炸一下，醒醒神！",
        "这炸弹我可憋很久了。"
    };
    static const std::vector<std::string> almostOut{
        "别急别急，我马上跑完。",
        "我手里没几张了，注意点。",
        "再给我一轮，我可能就溜了。"
    };
    static const std::vector<std::string> forcedBreakGoodHand{
        "哎呀，我的好牌都被拆光光了。",
        "这牌本来很顺的，非得拆我一手。",
        "要得起必须打，心疼我的牌型。"
    };
    static const std::vector<std::string> cannotBeatBigMove{
        "这么长一串？我先缓缓。",
        "这谁顶得住啊，我不要了。",
        "你这一下甩这么多，我接不住。"
    };
    static const std::vector<std::string> bigMoveTaunt{
        "看好了，一大把直接甩出去！",
        "这么多牌一起走，帅不帅？",
        "我这一手下去，桌面都清爽了。"
    };
    static const std::vector<std::string> humanGoodBomb{
        "李姐手里还有炸弹？这谁敢动啊。",
        "完了完了，李姐藏着炸弹呢。",
        "这炸弹一亮，我有点慌。"
    };
    static const std::vector<std::string> humanGoodStraight{
        "这顺子也太顺了吧。",
        "李姐这条顺子，漂亮得有点过分。",
        "这么长一串，看得我心里发虚。"
    };
    static const std::vector<std::string> humanGoodPlane{
        "飞机都来了？这牌也太豪华了。",
        "这飞机一起飞，我可拦不住。",
        "李姐这飞机藏得真深。"
    };
    static const std::vector<std::string> humanGoodConsecutivePairs{
        "这一排对子也太整齐了。",
        "连对这么长，我有点接不住。",
        "对子排队过来，压力很大。"
    };
    static const std::vector<std::string> roundEndGoodStraight{
        "哎呀，我还有一条好顺子呢。",
        "这顺子还没来得及跑出去。",
        "可惜了，我手里这顺子挺漂亮。"
    };
    static const std::vector<std::string> roundEndGoodPlane{
        "哎呀，我还有个好飞机呢。",
        "飞机还在手里，结果已经结束了。",
        "这把我的飞机没起飞。"
    };
    static const std::vector<std::string> roundEndGoodBomb{
        "我炸弹还在手里呢，亏大了。",
        "这炸弹没甩出去，太憋屈了。",
        "早知道我就先炸一下了。"
    };
    static const std::vector<std::string> roundEndGoodConsecutivePairs{
        "我这连对还挺整齐，可惜没机会了。",
        "对子排好了，牌局却结束了。",
        "这手连对没打出去，真难受。"
    };

    switch (kind) {
    case TalkKind::NormalPlay: return normalPlay;
    case TalkKind::Pass: return pass;
    case TalkKind::BombPlay: return bombPlay;
    case TalkKind::AlmostOut: return almostOut;
    case TalkKind::ForcedBreakGoodHand: return forcedBreakGoodHand;
    case TalkKind::CannotBeatBigMove: return cannotBeatBigMove;
    case TalkKind::BigMoveTaunt: return bigMoveTaunt;
    case TalkKind::HumanGoodBomb: return humanGoodBomb;
    case TalkKind::HumanGoodStraight: return humanGoodStraight;
    case TalkKind::HumanGoodPlane: return humanGoodPlane;
    case TalkKind::HumanGoodConsecutivePairs: return humanGoodConsecutivePairs;
    case TalkKind::RoundEndGoodStraight: return roundEndGoodStraight;
    case TalkKind::RoundEndGoodPlane: return roundEndGoodPlane;
    case TalkKind::RoundEndGoodBomb: return roundEndGoodBomb;
    case TalkKind::RoundEndGoodConsecutivePairs: return roundEndGoodConsecutivePairs;
    case TalkKind::Count: break;
    }
    return normalPlay;
}

std::optional<TalkKind> HumanGoodTalkKind(const rules::HandPattern& pattern) {
    if (pattern.type == rules::PatternType::Bomb) {
        return TalkKind::HumanGoodBomb;
    }
    if (pattern.type == rules::PatternType::Plane) {
        return TalkKind::HumanGoodPlane;
    }
    if (pattern.type == rules::PatternType::Straight && pattern.cardCount >= 7) {
        return TalkKind::HumanGoodStraight;
    }
    if (pattern.type == rules::PatternType::ConsecutivePairs && pattern.cardCount >= 6) {
        return TalkKind::HumanGoodConsecutivePairs;
    }
    return std::nullopt;
}

int BestConsecutiveRunLength(std::vector<rules::Rank> ranks) {
    if (ranks.empty()) {
        return 0;
    }
    std::sort(ranks.begin(), ranks.end(), [](rules::Rank lhs, rules::Rank rhs) {
        return rules::RankValue(lhs) < rules::RankValue(rhs);
    });
    ranks.erase(std::unique(ranks.begin(), ranks.end()), ranks.end());

    int best = 1;
    int current = 1;
    for (std::size_t i = 1; i < ranks.size(); ++i) {
        if (rules::RankValue(ranks[i]) == rules::RankValue(ranks[i - 1]) + 1) {
            current++;
        } else {
            current = 1;
        }
        best = std::max(best, current);
    }
    return best;
}

std::optional<TalkKind> RoundEndGoodTalkKind(const rules::Cards& hand) {
    const auto counts = CountRanks(hand);

    std::vector<rules::Rank> tripleRanks;
    std::vector<rules::Rank> straightRanks;
    std::vector<rules::Rank> pairRanks;
    bool hasBomb = false;
    for (const auto& [rank, count] : counts) {
        if (rank != rules::Rank::Two) {
            straightRanks.push_back(rank);
        }
        if (rank != rules::Rank::Two && count >= 2) {
            pairRanks.push_back(rank);
        }
        if (rank != rules::Rank::Two && count >= 3) {
            tripleRanks.push_back(rank);
        }
        if (count >= 4 && rank != rules::Rank::Ace && rank != rules::Rank::Two) {
            hasBomb = true;
        }
    }

    if (BestConsecutiveRunLength(tripleRanks) >= 2) {
        return TalkKind::RoundEndGoodPlane;
    }
    if (BestConsecutiveRunLength(straightRanks) >= 7) {
        return TalkKind::RoundEndGoodStraight;
    }
    if (hasBomb) {
        return TalkKind::RoundEndGoodBomb;
    }
    if (BestConsecutiveRunLength(pairRanks) >= 3) {
        return TalkKind::RoundEndGoodConsecutivePairs;
    }
    return std::nullopt;
}

} // namespace

GameState::GameState() {
    players_[0].name = "\xE6\x9D\x8E\xE5\xA7\x90";
    players_[1].name = "AI1";
    players_[2].name = "AI2";
    lastTalkIndices_.fill(-1);
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
    playedCards_.clear();
    passObservations_.fill(std::nullopt);
    lastPattern_.reset();
    passCount_ = 0;
    roundOver_ = false;
    aiDelay_ = 0.45f;
    talkCooldown_ = 0.0f;
    talkText_.clear();
    lastTalkIndices_.fill(-1);
    turnRecords_.clear();
    nextTurnNo_ = 1;
    externalAiPending_ = false;
    if (externalAi_) {
        externalAi_->Cancel();
    }
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

    if (externalAiPending_) {
        if (TryCompleteExternalAiTurn()) {
            aiDelay_ = NextThinkDelay();
        }
        return;
    }

    const bool aiControlled = currentPlayer_ != rules::PlayerId::Player || autoplay_;
    if (!aiControlled) {
        return;
    }

    aiDelay_ -= dt;
    if (aiDelay_ > 0.0f) {
        return;
    }

    if (externalAi_ && externalAi_->CanHandle(currentPlayer_)) {
        StartExternalAiTurn();
    } else {
        PlayLocalAiTurn(currentPlayer_);
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
    const TurnSnapshot before = Snapshot();
    PlayCards(rules::PlayerId::Player, cards, validation.pattern);
    TurnRecord record = BuildTurnRecord(
        before,
        rules::PlayerId::Player,
        TurnDecisionSource::Human,
        TurnDecisionReason::NormalChoice,
        ActionFromCards(cards),
        ActionFromCards(cards),
        cards,
        validation.pattern,
        true,
        "玩家出牌",
        {});
    AppendRecord(std::move(record));
    selectedIndices_.clear();
    hintIndices_.clear();
    return true;
}

bool GameState::PassHuman() {
    if (!IsHumanTurn()) {
        return false;
    }
    const TurnSnapshot before = Snapshot();
    if (!Pass(rules::PlayerId::Player)) {
        return false;
    }
    TurnRecord record = BuildTurnRecord(
        before,
        rules::PlayerId::Player,
        TurnDecisionSource::Human,
        TurnDecisionReason::CannotBeat,
        ActionFromCards({}, true),
        ActionFromCards({}, true),
        {},
        std::nullopt,
        true,
        "玩家不要",
        {});
    AppendRecord(std::move(record));
    return true;
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
    struct IndexedCandidate {
        std::set<int> indices;
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

    std::vector<int> dragIndices;
    dragIndices.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        if (inDragPath[static_cast<std::size_t>(i)]) {
            dragIndices.push_back(i);
        }
    }

    const std::uint64_t dragLimit = dragIndices.size() >= 63 ? 0 : (1ull << dragIndices.size());
    auto scorePattern = [](const rules::HandPattern& pattern) {
        return pattern.cardCount * 100000
            + DragPatternTieBreaker(pattern.type)
            + rules::RankValue(pattern.mainRank);
    };

    std::vector<Candidate> candidates;
    for (std::uint64_t mask = 1; mask < dragLimit; ++mask) {
        rules::Cards cards;
        cards.reserve(dragIndices.size());
        for (std::size_t i = 0; i < dragIndices.size(); ++i) {
            if ((mask & (1ull << i)) != 0) {
                cards.push_back(hand[static_cast<std::size_t>(dragIndices[i])]);
            }
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

        candidates.push_back(Candidate{cards, pattern, scorePattern(pattern)});
    }

    std::vector<IndexedCandidate> additiveCandidates;
    if (!selectedIndices_.empty()) {
        // When a core group is already selected, dragging over loose cards should
        // first try to complete a larger legal move such as three-with-two or a
        // plane with wings. The dragged cards alone may not be a valid pattern.
        for (std::uint64_t mask = 1; mask < dragLimit; ++mask) {
            std::set<int> indices = selectedIndices_;
            for (std::size_t i = 0; i < dragIndices.size(); ++i) {
                if ((mask & (1ull << i)) != 0) {
                    indices.insert(dragIndices[i]);
                }
            }
            if (indices == selectedIndices_) {
                continue;
            }

            rules::Cards cards;
            cards.reserve(indices.size());
            for (int index : indices) {
                cards.push_back(hand[static_cast<std::size_t>(index)]);
            }
            const auto validation = rules_.ValidateLeadMove(cards, n);
            if (!validation.ok) {
                continue;
            }

            additiveCandidates.push_back(IndexedCandidate{std::move(indices), validation.pattern, scorePattern(validation.pattern)});
        }
    }

    if (candidates.empty() && additiveCandidates.empty()) {
        return false;
    }

    std::set<int> chosenIndices;
    rules::HandPattern chosenPattern{};
    if (!candidates.empty()) {
        const Candidate& chosen = *std::max_element(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
            return lhs.score < rhs.score;
        });
        chosenPattern = chosen.pattern;
        for (std::size_t i = 0; i < hand.size(); ++i) {
            for (rules::Card card : chosen.cards) {
                if (SameCard(hand[i], card)) {
                    const int index = static_cast<int>(i);
                    chosenIndices.insert(index);
                }
            }
        }
    }

    // Repeating the same drag gesture toggles the selected group off, matching
    // hint-button behavior for an already selected recommendation.
    if (!chosenIndices.empty() && selectedIndices_ == chosenIndices) {
        selectedIndices_.clear();
        hintIndices_.clear();
        toast_ = "已取消拖拽选择";
    } else {
        std::set<int> finalIndices = chosenIndices;
        rules::HandPattern finalPattern = chosenPattern;
        if (!selectedIndices_.empty()) {
            if (!additiveCandidates.empty()) {
                const IndexedCandidate& additive = *std::max_element(additiveCandidates.begin(), additiveCandidates.end(), [](const IndexedCandidate& lhs, const IndexedCandidate& rhs) {
                    return lhs.score < rhs.score;
                });
                finalIndices = additive.indices;
                finalPattern = additive.pattern;
            } else {
                finalIndices = selectedIndices_;
                finalIndices.insert(chosenIndices.begin(), chosenIndices.end());
            }
        }

        selectedIndices_ = std::move(finalIndices);
        hintIndices_.assign(selectedIndices_.begin(), selectedIndices_.end());
        toast_ = "已按拖拽路线选中 " + DragPatternDescription(finalPattern);
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
    playedCards_.clear();
    passObservations_.fill(std::nullopt);
    passCount_ = 0;
    roundOver_ = false;
    autoplay_ = false;
    selectedIndices_.clear();
    hintIndices_.clear();
    bombs_.clear();
    events_.clear();
    toast_.clear();
    turnRecords_.clear();
    nextTurnNo_ = 1;
    externalAiPending_ = false;
    if (externalAi_) {
        externalAi_->Cancel();
    }
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
    context.playedCards = playedCards_;
    context.passObservations = passObservations_;
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

std::vector<std::pair<rules::Cards, rules::HandPattern>> GameState::LegalMoves(rules::PlayerId player) const {
    const rules::Cards& hand = players_[Index(player)].hand;
    const int n = static_cast<int>(hand.size());
    std::vector<std::pair<rules::Cards, rules::HandPattern>> moves;
    if (n <= 0 || n >= 63) {
        return moves;
    }
    const std::uint64_t limit = 1ull << n;
    for (std::uint64_t mask = 1; mask < limit; ++mask) {
        rules::Cards cards;
        cards.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            if ((mask & (1ull << i)) != 0) {
                cards.push_back(hand[static_cast<std::size_t>(i)]);
            }
        }
        const auto validation = CurrentPlayerLeads()
            ? rules_.ValidateLeadMove(cards, n)
            : rules_.ValidateFollowMove(cards, *lastPattern_, n);
        if (validation.ok) {
            moves.emplace_back(std::move(cards), validation.pattern);
        }
    }
    return moves;
}

TurnSnapshot GameState::Snapshot() const {
    return TurnSnapshot{
        {players_[0].hand, players_[1].hand, players_[2].hand},
        lastCards_,
        lastPattern_,
        lastMovePlayer_,
        currentPlayer_,
        passCount_
    };
}

GameAction GameState::ActionFromCards(const rules::Cards& cards, bool pass, std::string talk) const {
    return GameAction{pass ? "pass" : "play", pass ? std::vector<std::string>{} : RanksOf(cards), std::move(talk)};
}

TurnRecord GameState::BuildTurnRecord(
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
    TurnDecisionTrace trace) const {
    TurnRecord record;
    record.turnNo = nextTurnNo_;
    record.actor = actor;
    record.source = source;
    record.reason = reason;
    record.before = before;
    record.after = Snapshot();
    record.requestedAction = requested;
    record.finalAction = final;
    record.finalCards = finalCards;
    record.finalPattern = finalPattern;
    record.accepted = accepted;
    record.validationMessage = validationMessage;
    record.trace = std::move(trace);
    return record;
}

void GameState::AppendRecord(TurnRecord record) {
    if (record.trace.reasoningContent.empty()) {
        record.trace = SyntheticTrace(record);
    }
    turnRecords_.push_back(std::move(record));
    nextTurnNo_++;
}

TurnDecisionTrace GameState::SyntheticTrace(const TurnRecord& record) const {
    TurnDecisionTrace trace;
    std::ostringstream reasoning;
    reasoning << "本地记录：" << PlayerLabel(record.actor) << ' ';
    if (record.reason == TurnDecisionReason::CannotBeat) {
        reasoning << "按规则要不起，只能不要。";
    } else if (record.reason == TurnDecisionReason::OnlyLegalMove) {
        reasoning << "只有一种合法选择，直接执行 " << MoveText(record.finalAction) << "。";
    } else {
        reasoning << "执行 " << MoveText(record.finalAction) << "。";
    }
    trace.reasoningContent = reasoning.str();
    if (record.actor == rules::PlayerId::Ai1) {
        trace.toolCallId = "synthetic_turn_" + std::to_string(record.turnNo);
        trace.toolArgumentsJson = ActionArgumentsJson(record.finalAction);
        trace.toolResultJson = "{\"accepted\":true}";
    }
    return trace;
}

void GameState::SetExternalAiController(std::shared_ptr<ExternalAiController> controller) {
    if (externalAi_) {
        externalAi_->Cancel();
    }
    externalAi_ = std::move(controller);
    externalAiPending_ = false;
}

void GameState::PlayLocalAiTurn(rules::PlayerId player) {
    const TurnSnapshot before = Snapshot();
    const auto legal = LegalMoves(player);
    TurnDecisionSource source = TurnDecisionSource::LocalAi;
    TurnDecisionReason reason = legal.size() == 1 ? TurnDecisionReason::OnlyLegalMove : TurnDecisionReason::NormalChoice;
    AiMoveChoice choice;
    if (legal.empty() && !CurrentPlayerLeads()) {
        source = TurnDecisionSource::System;
        reason = TurnDecisionReason::CannotBeat;
        choice.pass = true;
    } else {
        choice = aiPlayers_[Index(player)].ChooseMove(players_[Index(player)].hand, MakeAiContext(player));
        if (choice.pass) {
            reason = TurnDecisionReason::CannotBeat;
        }
    }

    if (choice.pass) {
        if (!Pass(player)) {
            return;
        }
        TurnRecord record = BuildTurnRecord(
            before,
            player,
            source,
            reason,
            ActionFromCards({}, true),
            ActionFromCards({}, true),
            {},
            std::nullopt,
            true,
            "本地 AI 不要",
            {});
        AppendRecord(std::move(record));
        return;
    }

    PlayCards(player, choice.cards, choice.pattern, choice.disruptionPenalty);
    TurnRecord record = BuildTurnRecord(
        before,
        player,
        source,
        reason,
        ActionFromCards(choice.cards),
        ActionFromCards(choice.cards),
        choice.cards,
        choice.pattern,
        true,
        "本地 AI 出牌",
        {});
    AppendRecord(std::move(record));
}

void GameState::StartExternalAiTurn() {
    const auto legal = LegalMoves(currentPlayer_);
    if (legal.empty() || legal.size() == 1) {
        PlayLocalAiTurn(currentPlayer_);
        return;
    }

    externalAiPending_ = true;
    externalAi_->Start(ExternalAiRequest{
        nextTurnNo_,
        currentPlayer_,
        Snapshot(),
        turnRecords_
    });
}

bool GameState::TryCompleteExternalAiTurn() {
    if (!externalAi_) {
        externalAiPending_ = false;
        return false;
    }
    std::optional<ExternalAiResult> result = externalAi_->TryGetResult();
    if (!result) {
        return false;
    }
    externalAiPending_ = false;
    if (!ApplyExternalAiResult(*result)) {
        PlayLocalAiTurn(currentPlayer_);
    }
    return true;
}

bool GameState::ApplyExternalAiResult(const ExternalAiResult& result) {
    const TurnSnapshot before = Snapshot();
    const rules::PlayerId actor = currentPlayer_;
    auto useLlmTalk = [&](const std::string& talk) {
        if (talk.empty()) {
            return;
        }
        events_.erase(std::remove_if(events_.begin(), events_.end(), [&](const GameEvent& event) {
            return event.type == GameEventType::Talk && event.player == actor;
        }), events_.end());
        talkPlayer_ = actor;
        talkText_ = talk;
        talkCooldown_ = 5.0f;
        AddEvent(GameEvent{GameEventType::Talk, actor, talk, {}});
    };
    if (!result.ok) {
        TurnDecisionTrace trace;
        trace.reasoningContent = result.reasoningContent;
        trace.toolCallId = result.toolCallId;
        trace.toolArgumentsJson = result.toolArgumentsJson;
        trace.requestLogPath = result.requestLogPath;
        trace.responseLogPath = result.responseLogPath;
        trace.errorMessage = result.errorMessage;
        AiMoveChoice fallback = aiPlayers_[Index(actor)].ChooseMove(players_[Index(actor)].hand, MakeAiContext(actor));
        if (fallback.pass) {
            if (!Pass(actor)) {
                return false;
            }
            TurnRecord record = BuildTurnRecord(
                before,
                actor,
                TurnDecisionSource::LlmAi,
                TurnDecisionReason::LlmFallback,
                result.requestedAction,
                ActionFromCards({}, true),
                {},
                std::nullopt,
                false,
                result.errorMessage,
                std::move(trace));
            AppendRecord(std::move(record));
            return true;
        }
        PlayCards(actor, fallback.cards, fallback.pattern, fallback.disruptionPenalty);
        TurnRecord record = BuildTurnRecord(
            before,
            actor,
            TurnDecisionSource::LlmAi,
            TurnDecisionReason::LlmFallback,
            result.requestedAction,
            ActionFromCards(fallback.cards),
            fallback.cards,
            fallback.pattern,
            false,
            result.errorMessage,
            std::move(trace));
        AppendRecord(std::move(record));
        return true;
    }

    TurnDecisionTrace trace;
    trace.reasoningContent = result.reasoningContent;
    trace.toolCallId = result.toolCallId;
    trace.toolArgumentsJson = result.toolArgumentsJson;
    trace.requestLogPath = result.requestLogPath;
    trace.responseLogPath = result.responseLogPath;

    if (result.requestedAction.action == "pass") {
        if (CurrentPlayerLeads() || HasPlayableFollow(actor)) {
            trace.errorMessage = "LLM pass 不合法";
            ExternalAiResult failed = result;
            failed.ok = false;
            failed.errorMessage = trace.errorMessage;
            return ApplyExternalAiResult(failed);
        }
        Pass(actor);
        useLlmTalk(result.requestedAction.talk);
        TurnRecord record = BuildTurnRecord(
            before,
            actor,
            TurnDecisionSource::LlmAi,
            TurnDecisionReason::CannotBeat,
            result.requestedAction,
            result.requestedAction,
            {},
            std::nullopt,
            true,
            "LLM 不要合法",
            std::move(trace));
        AppendRecord(std::move(record));
        return true;
    }

    rules::Cards cards;
    std::vector<bool> used(players_[Index(actor)].hand.size(), false);
    for (const std::string& rankText : result.requestedAction.ranks) {
        const auto rank = ParseRank(rankText);
        if (!rank) {
            ExternalAiResult failed = result;
            failed.ok = false;
            failed.errorMessage = "LLM 返回未知点数";
            return ApplyExternalAiResult(failed);
        }
        bool found = false;
        const rules::Cards& hand = players_[Index(actor)].hand;
        for (std::size_t i = 0; i < hand.size(); ++i) {
            if (!used[i] && hand[i].rank == *rank) {
                used[i] = true;
                cards.push_back(hand[i]);
                found = true;
                break;
            }
        }
        if (!found) {
            ExternalAiResult failed = result;
            failed.ok = false;
            failed.errorMessage = "LLM 返回了手牌中不存在的点数";
            return ApplyExternalAiResult(failed);
        }
    }

    const int handSize = static_cast<int>(players_[Index(actor)].hand.size());
    const auto validation = CurrentPlayerLeads()
        ? rules_.ValidateLeadMove(cards, handSize)
        : rules_.ValidateFollowMove(cards, *lastPattern_, handSize);
    if (!validation.ok) {
        ExternalAiResult failed = result;
        failed.ok = false;
        failed.errorMessage = validation.reason;
        return ApplyExternalAiResult(failed);
    }

    PlayCards(actor, cards, validation.pattern);
    useLlmTalk(result.requestedAction.talk);
    TurnRecord record = BuildTurnRecord(
        before,
        actor,
        TurnDecisionSource::LlmAi,
        TurnDecisionReason::NormalChoice,
        result.requestedAction,
        ActionFromCards(cards, false, result.requestedAction.talk),
        cards,
        validation.pattern,
        true,
        "LLM 出牌合法",
        std::move(trace));
    AppendRecord(std::move(record));
    return true;
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

void GameState::RecordPassObservation(rules::PlayerId player, const rules::HandPattern& pattern) {
    const int index = Index(player);
    PassObservation observation{pattern, static_cast<int>(players_[index].hand.size())};
    std::optional<PassObservation>& existing = passObservations_[static_cast<std::size_t>(index)];

    if (existing && existing->pattern.type == rules::PatternType::Single && pattern.type == rules::PatternType::Single) {
        // For singles, a lower failed-to-beat rank is stronger information:
        // failing to beat Q proves K/A/2 are unavailable, while failing to beat K
        // still leaves open the possibility that the player has a K.
        if (rules::RankValue(pattern.mainRank) < rules::RankValue(existing->pattern.mainRank)) {
            existing = observation;
        }
        return;
    }

    if (existing && existing->pattern.type == rules::PatternType::Single && pattern.type != rules::PatternType::Single) {
        return;
    }

    existing = observation;
}

void GameState::PlayCards(rules::PlayerId player, const rules::Cards& cards, const rules::HandPattern& pattern, int disruptionPenalty) {
    const bool wasFollowing = !CurrentPlayerLeads();
    RemoveCardsFromHand(player, cards);
    players_[Index(player)].hasPlayedCards = true;
    playedCards_.insert(playedCards_.end(), cards.begin(), cards.end());
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
    }

    if (player == rules::PlayerId::Player) {
        MaybeTalkAboutHumanMove(pattern);
    } else if (pattern.type == rules::PatternType::Bomb) {
        MaybeTalk(player, TalkKind::BombPlay, true);
    } else if (wasFollowing && disruptionPenalty >= 320) {
        MaybeTalk(player, TalkKind::ForcedBreakGoodHand, true);
    } else if (static_cast<int>(cards.size()) >= 7) {
        MaybeTalk(player, TalkKind::BigMoveTaunt, true);
    } else if (players_[Index(player)].hand.size() <= 3) {
        MaybeTalk(player, TalkKind::AlmostOut);
    } else {
        MaybeTalk(player, TalkKind::NormalPlay);
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
    RecordPassObservation(player, *lastPattern_);
    const std::string message = PlayerDisplayName(players_, player) + " 不要";
    toast_ = message;
    AddEvent(GameEvent{GameEventType::Passed, player, message, {}});
    if (IsAi(player)) {
        if (lastPattern_ && lastPattern_->cardCount >= 7) {
            MaybeTalk(player, TalkKind::CannotBeatBigMove, true);
        } else {
            MaybeTalk(player, TalkKind::Pass);
        }
    }

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
    MaybeTalkAboutRoundEndGoodHands(winner);
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

void GameState::MaybeTalk(rules::PlayerId player, TalkKind kind, bool force) {
    // Key reactions should be heard even if a recent ordinary line started the cooldown.
    if (player == rules::PlayerId::Player || (!force && !IsForceTalk(kind) && talkCooldown_ > 0.0f)) {
        return;
    }
    talkPlayer_ = player;
    talkText_ = ChooseTalkText(kind);
    talkCooldown_ = 5.0f;
    AddEvent(GameEvent{GameEventType::Talk, player, talkText_, {}});
}

void GameState::MaybeTalkAboutHumanMove(const rules::HandPattern& pattern) {
    const auto kind = HumanGoodTalkKind(pattern);
    if (!kind) {
        return;
    }

    std::uniform_int_distribution<int> dist(0, 1);
    const rules::PlayerId speaker = dist(rng_) == 0 ? rules::PlayerId::Ai1 : rules::PlayerId::Ai2;
    MaybeTalk(speaker, *kind, true);
}

void GameState::MaybeTalkAboutRoundEndGoodHands(rules::PlayerId winner) {
    struct Candidate {
        rules::PlayerId player;
        TalkKind kind;
        int priority{};
    };

    std::optional<Candidate> best;
    for (rules::PlayerId player : {rules::PlayerId::Ai1, rules::PlayerId::Ai2}) {
        if (player == winner) {
            continue;
        }
        const auto kind = RoundEndGoodTalkKind(players_[Index(player)].hand);
        if (!kind) {
            continue;
        }

        int priority = 0;
        switch (*kind) {
        case TalkKind::RoundEndGoodPlane: priority = 400; break;
        case TalkKind::RoundEndGoodStraight: priority = 300; break;
        case TalkKind::RoundEndGoodBomb: priority = 200; break;
        case TalkKind::RoundEndGoodConsecutivePairs: priority = 100; break;
        default: break;
        }
        if (!best || priority > best->priority) {
            best = Candidate{player, *kind, priority};
        }
    }

    if (best) {
        MaybeTalk(best->player, best->kind, true);
    }
}

std::string GameState::ChooseTalkText(TalkKind kind) {
    const std::vector<std::string>& pool = TalkPool(kind);
    const std::size_t kindIndex = static_cast<std::size_t>(kind);
    if (pool.empty()) {
        return {};
    }

    std::uniform_int_distribution<int> dist(0, static_cast<int>(pool.size() - 1));
    int selected = dist(rng_);
    if (pool.size() > 1 && selected == lastTalkIndices_[kindIndex]) {
        selected = (selected + 1) % static_cast<int>(pool.size());
    }
    lastTalkIndices_[kindIndex] = selected;
    return pool[static_cast<std::size_t>(selected)];
}

} // namespace pdk::game
