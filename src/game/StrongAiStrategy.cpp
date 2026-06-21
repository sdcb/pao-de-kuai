#include "game/AiStrategy.h"
#include "game/AiStrategyInternal.h"

#include "rules/Deck.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace pdk::game {
namespace ai_internal {

int UnknownPatternBeaterPressureForPattern(
    const Candidate& candidate,
    const rules::HandPattern& pattern,
    const AiContext& context);
int StrongAdjustment(const Candidate& candidate, const AiContext& context);

bool SameObservationClass(const rules::HandPattern& lhs, const rules::HandPattern& rhs) {
    if (lhs.type != rhs.type) {
        return false;
    }
    switch (lhs.type) {
    case rules::PatternType::Straight:
    case rules::PatternType::ConsecutivePairs:
    case rules::PatternType::Plane:
        return lhs.cardCount == rhs.cardCount && lhs.groupCount == rhs.groupCount;
    case rules::PatternType::Single:
    case rules::PatternType::Pair:
    case rules::PatternType::TripleWithOne:
    case rules::PatternType::TripleWithPair:
    case rules::PatternType::Bomb:
        return lhs.cardCount == rhs.cardCount;
    case rules::PatternType::Invalid:
        return false;
    }
    return false;
}

bool ObservationProvesCannotBeat(const Candidate& candidate, const PassObservation& observation) {
    if (observation.remainingCards <= 0) {
        return false;
    }
    if (candidate.pattern.type == rules::PatternType::Bomb &&
        observation.pattern.type != rules::PatternType::Bomb) {
        // Passing on any non-bomb follow proves the player had no bomb then.
        return true;
    }
    if (!SameObservationClass(candidate.pattern, observation.pattern)) {
        return false;
    }
    return rules::RankValue(candidate.pattern.mainRank) >= rules::RankValue(observation.pattern.mainRank);
}

int ProvenControlBonus(const Candidate& candidate, const AiContext& context) {
    if (!context.leading) {
        return 0;
    }

    int provenOpponents = 0;
    for (int i = 0; i < static_cast<int>(context.passObservations.size()); ++i) {
        if (i == context.currentPlayerIndex || context.remainingCards[i] <= 0) {
            continue;
        }
        bool proven = false;
        const auto& history = context.passHistory[static_cast<std::size_t>(i)];
        for (const PassObservation& observation : history) {
            if (ObservationProvesCannotBeat(candidate, observation)) {
                proven = true;
                break;
            }
        }
        const std::optional<PassObservation>& observation = context.passObservations[static_cast<std::size_t>(i)];
        if (!proven && observation && ObservationProvesCannotBeat(candidate, *observation)) {
            proven = true;
        }
        if (proven) {
            provenOpponents++;
        }
    }

    if (provenOpponents == 0) {
        return 0;
    }
    return provenOpponents == 1
        ? 520 + candidate.pattern.cardCount * 35
        : 1450 + candidate.pattern.cardCount * 70;
}

bool IsWholeHandLead(const rules::Cards& cards) {
    return !cards.empty() && rules::ValidateLead(cards, static_cast<int>(cards.size())).ok;
}

bool CanFinishWithinTwoLeads(const rules::Cards& cards) {
    const int n = static_cast<int>(cards.size());
    if (n <= 0) {
        return true;
    }
    if (IsWholeHandLead(cards)) {
        return true;
    }
    if (n > 16) {
        return false;
    }

    const std::uint64_t limit = 1ull << n;
    for (std::uint64_t mask = 1; mask < limit - 1; ++mask) {
        rules::Cards first;
        rules::Cards second;
        first.reserve(n);
        second.reserve(n);
        for (int i = 0; i < n; ++i) {
            if ((mask & (1ull << i)) != 0) {
                first.push_back(cards[static_cast<std::size_t>(i)]);
            } else {
                second.push_back(cards[static_cast<std::size_t>(i)]);
            }
        }
        if (rules::ValidateLead(first, n).ok &&
            rules::ValidateLead(second, static_cast<int>(second.size())).ok) {
            return true;
        }
    }
    return false;
}

int FinishPlanBonus(const rules::Cards& remainder) {
    if (remainder.empty()) {
        return 0;
    }
    if (IsWholeHandLead(remainder)) {
        return 3600 + static_cast<int>(remainder.size()) * 80;
    }
    if (CanFinishWithinTwoLeads(remainder)) {
        return 1700 + static_cast<int>(remainder.size()) * 35;
    }
    return 0;
}

std::uint64_t CardSetMask(const rules::Cards& cards) {
    std::uint64_t mask = 0;
    for (rules::Card card : cards) {
        const int rankOffset = rules::RankValue(card.rank) - rules::RankValue(rules::Rank::Three);
        const int bit = rankOffset * 4 + static_cast<int>(card.suit);
        mask |= 1ull << bit;
    }
    return mask;
}

std::uint64_t PatternKey(const rules::HandPattern& pattern) {
    std::uint64_t key = static_cast<std::uint64_t>(pattern.type);
    key = (key << 8) | static_cast<std::uint64_t>(rules::RankValue(pattern.mainRank));
    key = (key << 8) | static_cast<std::uint64_t>(pattern.cardCount);
    key = (key << 8) | static_cast<std::uint64_t>(pattern.groupCount);
    return key;
}

bool CachedHasAnyFollowMove(const rules::Cards& hand, const rules::HandPattern& previous, int handSizeBeforePlay) {
    struct FollowKey {
        std::uint64_t handMask{};
        std::uint64_t pattern{};
        int handSize{};

        bool operator<(const FollowKey& other) const {
            if (handMask != other.handMask) {
                return handMask < other.handMask;
            }
            if (pattern != other.pattern) {
                return pattern < other.pattern;
            }
            return handSize < other.handSize;
        }

    };

    static std::map<FollowKey, bool> cache;
    const FollowKey key{CardSetMask(hand), PatternKey(previous), handSizeBeforePlay};
    const auto cached = cache.find(key);
    if (cached != cache.end()) {
        return cached->second;
    }
    const bool result = rules::HasAnyFollowMove(hand, previous, handSizeBeforePlay);
    cache[key] = result;
    return result;
}

int MinimumLeadCount(const rules::Cards& cards) {
    const int n = static_cast<int>(cards.size());
    if (n == 0) {
        return 0;
    }
    if (n > 16) {
        return 8;
    }

    static std::map<std::uint64_t, int> cache;
    const std::uint64_t cacheKey = CardSetMask(cards);
    const auto cached = cache.find(cacheKey);
    if (cached != cache.end()) {
        return cached->second;
    }

    const int fullMask = (1 << n) - 1;
    std::vector<int> legalMasks;
    legalMasks.reserve(static_cast<std::size_t>(fullMask));
    for (int mask = 1; mask <= fullMask; ++mask) {
        rules::Cards play;
        play.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            if ((mask & (1 << i)) != 0) {
                play.push_back(cards[static_cast<std::size_t>(i)]);
            }
        }
        if (rules::ValidateLead(play, n).ok) {
            legalMasks.push_back(mask);
        }
    }

    constexpr int inf = 99;
    std::vector<int> dp(static_cast<std::size_t>(fullMask + 1), inf);
    dp[0] = 0;
    for (int mask = 0; mask <= fullMask; ++mask) {
        const int current = dp[static_cast<std::size_t>(mask)];
        if (current >= inf) {
            continue;
        }
        const int remaining = fullMask ^ mask;
        for (int legal : legalMasks) {
            if ((legal & remaining) == legal) {
                int& next = dp[static_cast<std::size_t>(mask | legal)];
                next = std::min(next, current + 1);
            }
        }
    }
    const int result = dp[static_cast<std::size_t>(fullMask)];
    cache[cacheKey] = result;
    return result;
}

int LeadCountPlanBonus(const rules::Cards& remainder) {
    const int count = MinimumLeadCount(remainder);
    if (count == 0) {
        return 0;
    }
    int score = -count * 650;
    if (count == 1) {
        score += 4000;
    } else if (count == 2) {
        score += 2200;
    } else if (count == 3) {
        score += 600;
    }
    return score;
}

int RemainderFinishSafetyBonus(const Candidate& candidate, const AiContext& context) {
    if (candidate.remainder.empty()) {
        return 0;
    }
    const auto result = rules::ValidateLead(candidate.remainder, static_cast<int>(candidate.remainder.size()));
    if (!result.ok) {
        return 0;
    }
    const int pressure = UnknownPatternBeaterPressureForPattern(candidate, result.pattern, context);
    if (pressure == 0) {
        return 2600 + result.pattern.cardCount * 110;
    }
    if (pressure <= 2) {
        return 900 + result.pattern.cardCount * 60;
    }
    return 0;
}

int LooseSingleCount(const rules::Cards& cards) {
    const auto counts = CountRanks(cards);
    int singles = 0;
    for (const auto& [rank, count] : counts) {
        if (count == 1) {
            singles++;
        }
    }
    return singles;
}

int EstimatedControlCount(const rules::Cards& cards) {
    int controls = 0;
    const auto counts = CountRanks(cards);
    for (const auto& [rank, count] : counts) {
        if (rank == rules::Rank::Two || rank == rules::Rank::Ace) {
            controls += count;
        } else if (rank == rules::Rank::King && count >= 1) {
            controls += 1;
        }
        if (count >= 4 && rank >= rules::Rank::Three && rank <= rules::Rank::King) {
            controls += 2;
        }
    }
    return controls;
}

int UnknownBombRankCount(const Candidate& candidate, const AiContext& context) {
    int bombs = 0;
    for (int value = rules::RankValue(rules::Rank::Three); value <= rules::RankValue(rules::Rank::King); ++value) {
        const rules::Rank rank = static_cast<rules::Rank>(value);
        if (UnknownRankCount(candidate, context, rank) >= 4) {
            bombs++;
        }
    }
    return bombs;
}

int UnknownPatternBeaterPressureForPattern(
    const Candidate& candidate,
    const rules::HandPattern& pattern,
    const AiContext& context) {
    int pressure = 0;
    switch (pattern.type) {
    case rules::PatternType::Single:
        for (int value = rules::RankValue(pattern.mainRank) + 1; value <= rules::RankValue(rules::Rank::Two); ++value) {
            pressure += UnknownRankCount(candidate, context, static_cast<rules::Rank>(value));
        }
        break;
    case rules::PatternType::Pair:
        for (int value = rules::RankValue(pattern.mainRank) + 1; value <= rules::RankValue(rules::Rank::Ace); ++value) {
            if (UnknownRankCount(candidate, context, static_cast<rules::Rank>(value)) >= 2) {
                pressure += 2;
            }
        }
        break;
    case rules::PatternType::Straight:
    case rules::PatternType::ConsecutivePairs:
    case rules::PatternType::Plane:
    case rules::PatternType::TripleWithOne:
    case rules::PatternType::TripleWithPair:
        for (int value = rules::RankValue(pattern.mainRank) + 1; value <= rules::RankValue(rules::Rank::Ace); ++value) {
            if (UnknownRankCount(candidate, context, static_cast<rules::Rank>(value)) >= 3) {
                pressure += 1;
            }
        }
        break;
    case rules::PatternType::Bomb:
        for (int value = rules::RankValue(pattern.mainRank) + 1; value <= rules::RankValue(rules::Rank::King); ++value) {
            if (UnknownRankCount(candidate, context, static_cast<rules::Rank>(value)) >= 4) {
                pressure += 4;
            }
        }
        break;
    case rules::PatternType::Invalid:
        break;
    }
    if (pattern.type != rules::PatternType::Bomb) {
        pressure += UnknownBombRankCount(candidate, context) * 3;
    }
    return pressure;
}

int UnknownPatternBeaterPressure(const Candidate& candidate, const AiContext& context) {
    return UnknownPatternBeaterPressureForPattern(candidate, candidate.pattern, context);
}

bool ContainsExactCard(const rules::Cards& cards, rules::Card target) {
    return std::find_if(cards.begin(), cards.end(), [target](rules::Card card) {
        return card.rank == target.rank && card.suit == target.suit;
    }) != cards.end();
}


std::uint32_t MixSeed(std::uint32_t seed, std::uint32_t value) {
    seed ^= value + 0x9e3779b9u + (seed << 6) + (seed >> 2);
    return seed;
}

std::uint32_t CandidateSeed(const Candidate& candidate, const AiContext& context) {
    std::uint32_t seed = 2166136261u;
    seed = MixSeed(seed, static_cast<std::uint32_t>(context.currentPlayerIndex));
    seed = MixSeed(seed, static_cast<std::uint32_t>(context.lastMovePlayerIndex));
    seed = MixSeed(seed, static_cast<std::uint32_t>(candidate.pattern.cardCount));
    seed = MixSeed(seed, static_cast<std::uint32_t>(candidate.pattern.groupCount));
    seed = MixSeed(seed, static_cast<std::uint32_t>(rules::RankValue(candidate.pattern.mainRank)));
    for (rules::Card card : candidate.cards) {
        seed = MixSeed(seed, static_cast<std::uint32_t>(rules::RankValue(card.rank) * 5 + static_cast<int>(card.suit)));
    }
    for (rules::Card card : context.playedCards) {
        seed = MixSeed(seed, static_cast<std::uint32_t>(rules::RankValue(card.rank) * 5 + static_cast<int>(card.suit)));
    }
    return seed;
}

std::uint32_t NextRandom(std::uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state;
}

void ShuffleSample(rules::Cards& cards, std::uint32_t seed) {
    if (cards.size() <= 1) {
        return;
    }
    for (std::size_t i = cards.size(); i > 1; --i) {
        const std::size_t j = static_cast<std::size_t>(NextRandom(seed)) % i;
        std::swap(cards[i - 1], cards[j]);
    }
}

bool IsConsistentWithPassHistory(const rules::Cards& hand, int playerIndex, const AiContext& context) {
    if (playerIndex < 0 || playerIndex >= static_cast<int>(context.passHistory.size())) {
        return true;
    }
    for (const PassObservation& observation : context.passHistory[static_cast<std::size_t>(playerIndex)]) {
        if (CachedHasAnyFollowMove(hand, observation.pattern, observation.remainingCards)) {
            return false;
        }
    }
    const std::optional<PassObservation>& latest = context.passObservations[static_cast<std::size_t>(playerIndex)];
    if (latest && CachedHasAnyFollowMove(hand, latest->pattern, latest->remainingCards)) {
        return false;
    }
    return true;
}

rules::Cards UnknownOpponentCards(const rules::Cards& hand, const AiContext& context) {
    rules::Cards unknown;
    rules::Cards deck = rules::CreatePaoDeKuaiDeck();
    for (rules::Card card : deck) {
        if (!ContainsExactCard(hand, card) && !ContainsExactCard(context.playedCards, card)) {
            unknown.push_back(card);
        }
    }
    return unknown;
}

int SampledControlBonus(const Candidate& candidate, const AiContext& context, const rules::Cards& hand) {
    const int sampleCount = context.currentPlayerIndex == context.roundLeaderIndex ? 7 : 5;
    const int nextIndex = (context.currentPlayerIndex + 2) % 3;
    const int otherIndex = (context.currentPlayerIndex + 1) % 3;
    const int nextCards = context.remainingCards[static_cast<std::size_t>(nextIndex)];
    const int otherCards = context.remainingCards[static_cast<std::size_t>(otherIndex)];
    rules::Cards unknown = UnknownOpponentCards(hand, context);
    if (static_cast<int>(unknown.size()) != nextCards + otherCards) {
        return 0;
    }

    int score = 0;
    std::uint32_t seed = CandidateSeed(candidate, context);
    for (int sample = 0; sample < sampleCount; ++sample) {
        rules::Cards shuffled = unknown;
        ShuffleSample(shuffled, MixSeed(seed, static_cast<std::uint32_t>(sample + 1)));

        rules::Cards nextHand;
        rules::Cards otherHand;
        nextHand.reserve(static_cast<std::size_t>(nextCards));
        otherHand.reserve(static_cast<std::size_t>(otherCards));
        for (int i = 0; i < nextCards; ++i) {
            nextHand.push_back(shuffled[static_cast<std::size_t>(i)]);
        }
        for (int i = 0; i < otherCards; ++i) {
            otherHand.push_back(shuffled[static_cast<std::size_t>(nextCards + i)]);
        }

        if (!IsConsistentWithPassHistory(nextHand, nextIndex, context) ||
            !IsConsistentWithPassHistory(otherHand, otherIndex, context)) {
            continue;
        }

        const bool nextCanBeat = CachedHasAnyFollowMove(nextHand, candidate.pattern, nextCards);
        const bool otherCanBeat = CachedHasAnyFollowMove(otherHand, candidate.pattern, otherCards);
        if (!nextCanBeat && !otherCanBeat) {
            score += context.leading ? 620 : 520;
        } else {
            if (nextCanBeat) {
                score -= context.nextPlayerRemainingCards <= 3 ? 520 : 170;
                if (rules::ValidateFollow(nextHand, candidate.pattern, nextCards).ok) {
                    score -= 900;
                }
            }
            if (otherCanBeat) {
                score -= otherCards <= 3 ? 460 : 130;
                if (rules::ValidateFollow(otherHand, candidate.pattern, otherCards).ok) {
                    score -= 760;
                }
            }
        }
    }
    return score / sampleCount;
}

int NextIndex(int index) {
    return (index + 2) % 3;
}

struct RolloutState {
    std::array<rules::Cards, 3> hands;
    int currentIndex{0};
    int lastMoveIndex{0};
    int trickLeaderIndex{0};
    int roundLeaderIndex{0};
    std::optional<rules::HandPattern> lastPattern;
    rules::Cards playedCards;
    std::array<std::optional<PassObservation>, 3> passObservations{};
    std::array<std::vector<PassObservation>, 3> passHistory{};
    int passCount{0};
};

void RemoveRolloutCards(rules::Cards& hand, const rules::Cards& cards) {
    for (rules::Card card : cards) {
        const auto it = std::find_if(hand.begin(), hand.end(), [card](rules::Card owned) {
            return owned.rank == card.rank && owned.suit == card.suit;
        });
        if (it != hand.end()) {
            hand.erase(it);
        }
    }
}

void RecordRolloutPass(RolloutState& state, int playerIndex, const rules::HandPattern& pattern) {
    PassObservation observation{pattern, static_cast<int>(state.hands[static_cast<std::size_t>(playerIndex)].size())};
    state.passHistory[static_cast<std::size_t>(playerIndex)].push_back(observation);
    state.passObservations[static_cast<std::size_t>(playerIndex)] = observation;
}

AiContext RolloutContext(const RolloutState& state) {
    AiContext context;
    context.leading = !state.lastPattern.has_value();
    if (state.lastPattern) {
        context.previous = *state.lastPattern;
    }
    context.currentPlayerIndex = state.currentIndex;
    context.lastMovePlayerIndex = state.lastMoveIndex;
    context.trickLeaderIndex = state.lastPattern ? state.trickLeaderIndex : state.currentIndex;
    context.roundLeaderIndex = state.roundLeaderIndex;
    context.currentTrickPassCount = state.passCount;
    context.ownRemainingCards = static_cast<int>(state.hands[static_cast<std::size_t>(state.currentIndex)].size());
    for (int i = 0; i < 3; ++i) {
        context.remainingCards[static_cast<std::size_t>(i)] =
            static_cast<int>(state.hands[static_cast<std::size_t>(i)].size());
    }
    context.nextPlayerRemainingCards =
        static_cast<int>(state.hands[static_cast<std::size_t>(NextIndex(state.currentIndex))].size());
    context.minOpponentRemainingCards = 100;
    for (int i = 0; i < 3; ++i) {
        if (i != state.currentIndex) {
            context.minOpponentRemainingCards = std::min(context.minOpponentRemainingCards, context.remainingCards[i]);
        }
    }
    context.playedCards = state.playedCards;
    context.passObservations = state.passObservations;
    context.passHistory = state.passHistory;
    return context;
}

AiMoveChoice ChooseRolloutMove(const rules::Cards& hand, const AiContext& context, bool strongSelf) {
    if (!strongSelf) {
        BasicAiStrategy basic;
        return basic.ChooseMove(hand, context);
    }

    std::vector<Candidate> candidates = GenerateCandidates(hand, context);
    if (candidates.empty()) {
        return AiMoveChoice{true, {}, {}, context.leading ? "rollout strong no lead" : "rollout strong pass"};
    }
    for (Candidate& candidate : candidates) {
        candidate.score += StrongAdjustment(candidate, context);
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        if (lhs.cards.size() != rhs.cards.size()) {
            return lhs.cards.size() > rhs.cards.size();
        }
        return rules::RankValue(lhs.pattern.mainRank) < rules::RankValue(rhs.pattern.mainRank);
    });
    if (context.leading && context.currentPlayerIndex == context.roundLeaderIndex) {
        DeduplicateCandidates(candidates);
    }

    const int planLimit = std::min(context.leading ? 10 : 8, static_cast<int>(candidates.size()));
    for (int i = 0; i < planLimit; ++i) {
        Candidate& candidate = candidates[static_cast<std::size_t>(i)];
        if (!context.leading || candidate.remainder.size() <= 12) {
            const int planBonus = LeadCountPlanBonus(candidate.remainder);
            const int leadPlanWeight = context.currentPlayerIndex == context.roundLeaderIndex ? 3 : 5;
            candidate.score += context.leading ? planBonus * leadPlanWeight : planBonus;
        }
        if (!context.leading && context.currentTrickPassCount > 0) {
            candidate.score += SampledControlBonus(candidate, context, hand);
        }
    }
    std::sort(candidates.begin(), candidates.begin() + planLimit, [](const Candidate& lhs, const Candidate& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        if (lhs.cards.size() != rhs.cards.size()) {
            return lhs.cards.size() > rhs.cards.size();
        }
        return rules::RankValue(lhs.pattern.mainRank) < rules::RankValue(rhs.pattern.mainRank);
    });

    const Candidate& best = candidates.front();
    return AiMoveChoice{false, best.cards, best.pattern, "rollout strong", best.disruptionPenalty};
}

int RolloutWinner(RolloutState state, int strongIndex) {
    BasicAiStrategy basic;
    for (int turn = 0; turn < 240; ++turn) {
        rules::Cards& hand = state.hands[static_cast<std::size_t>(state.currentIndex)];
        AiMoveChoice choice = state.currentIndex == strongIndex
            ? ChooseRolloutMove(hand, RolloutContext(state), true)
            : basic.ChooseMove(hand, RolloutContext(state));
        if (choice.pass) {
            if (!state.lastPattern) {
                return -1;
            }
            RecordRolloutPass(state, state.currentIndex, *state.lastPattern);
            state.passCount++;
            if (state.passCount >= 2) {
                state.currentIndex = state.lastMoveIndex;
                state.lastPattern.reset();
                state.trickLeaderIndex = state.currentIndex;
                state.passCount = 0;
            } else {
                state.currentIndex = NextIndex(state.currentIndex);
            }
            continue;
        }

        const int handSizeBefore = static_cast<int>(hand.size());
        const auto validation = state.lastPattern
            ? rules::ValidateFollow(choice.cards, *state.lastPattern, handSizeBefore)
            : rules::ValidateLead(choice.cards, handSizeBefore);
        if (!validation.ok) {
            return -1;
        }
        if (!state.lastPattern) {
            state.trickLeaderIndex = state.currentIndex;
        }
        RemoveRolloutCards(hand, choice.cards);
        state.playedCards.insert(state.playedCards.end(), choice.cards.begin(), choice.cards.end());
        state.lastPattern = validation.pattern;
        state.lastMoveIndex = state.currentIndex;
        state.passCount = 0;
        if (hand.empty()) {
            return state.currentIndex;
        }
        state.currentIndex = NextIndex(state.currentIndex);
    }
    return -1;
}

int CountMaskBits(int mask) {
    int count = 0;
    while (mask != 0) {
        count += mask & 1;
        mask >>= 1;
    }
    return count;
}

int ScoreRolloutDistribution(
    const Candidate& candidate,
    const AiContext& context,
    int self,
    int next,
    int other,
    rules::Cards nextHand,
    rules::Cards otherHand) {
    RolloutState state;
    state.hands[static_cast<std::size_t>(self)] = candidate.remainder;
    state.hands[static_cast<std::size_t>(next)] = std::move(nextHand);
    state.hands[static_cast<std::size_t>(other)] = std::move(otherHand);
    state.currentIndex = next;
    state.lastMoveIndex = self;
    state.trickLeaderIndex = self;
    state.roundLeaderIndex = context.roundLeaderIndex;
    state.lastPattern = candidate.pattern;
    state.playedCards = context.playedCards;
    state.playedCards.insert(state.playedCards.end(), candidate.cards.begin(), candidate.cards.end());
    state.passObservations = context.passObservations;
    state.passHistory = context.passHistory;

    const int winner = RolloutWinner(std::move(state), self);
    const int selfWinScore = self == context.roundLeaderIndex ? 3800 : 2600;
    if (winner == self) {
        return selfWinScore;
    }
    if (winner >= 0) {
        return winner == other ? -700 : -2300;
    }
    return 0;
}

std::optional<int> EnumeratedRolloutBonus(
    const Candidate& candidate,
    const AiContext& context,
    const rules::Cards& unknown,
    int self,
    int next,
    int other,
    int nextCards) {
    const int n = static_cast<int>(unknown.size());
    if (n > 14 || nextCards < 0 || nextCards > n) {
        return std::nullopt;
    }

    int possible = 0;
    const int limit = 1 << n;
    for (int mask = 0; mask < limit; ++mask) {
        if (CountMaskBits(mask) == nextCards) {
            possible++;
            if (possible > 4000) {
                return std::nullopt;
            }
        }
    }

    int total = 0;
    int count = 0;
    for (int mask = 0; mask < limit; ++mask) {
        if (CountMaskBits(mask) != nextCards) {
            continue;
        }

        rules::Cards nextHand;
        rules::Cards otherHand;
        nextHand.reserve(static_cast<std::size_t>(nextCards));
        otherHand.reserve(static_cast<std::size_t>(n - nextCards));
        for (int i = 0; i < n; ++i) {
            if ((mask & (1 << i)) != 0) {
                nextHand.push_back(unknown[static_cast<std::size_t>(i)]);
            } else {
                otherHand.push_back(unknown[static_cast<std::size_t>(i)]);
            }
        }

        if (!IsConsistentWithPassHistory(nextHand, next, context) ||
            !IsConsistentWithPassHistory(otherHand, other, context)) {
            continue;
        }

        total += ScoreRolloutDistribution(candidate, context, self, next, other, std::move(nextHand), std::move(otherHand));
        count++;
    }

    if (count == 0) {
        return std::nullopt;
    }
    return total / count;
}

int RolloutBonus(const Candidate& candidate, const AiContext& context, const rules::Cards& hand) {
    if (candidate.remainder.size() > 10 && context.minOpponentRemainingCards > 8) {
        return 0;
    }

    const int self = context.currentPlayerIndex;
    const int next = NextIndex(self);
    const int other = NextIndex(next);
    constexpr int sampleCount = 5;
    const int nextCards = context.remainingCards[static_cast<std::size_t>(next)];
    const int otherCards = context.remainingCards[static_cast<std::size_t>(other)];
    rules::Cards unknown = UnknownOpponentCards(hand, context);
    if (static_cast<int>(unknown.size()) != nextCards + otherCards) {
        return 0;
    }

    if (std::optional<int> enumerated = EnumeratedRolloutBonus(candidate, context, unknown, self, next, other, nextCards)) {
        return *enumerated;
    }

    int score = 0;
    int validSamples = 0;
    std::uint32_t seed = CandidateSeed(candidate, context);
    for (int sample = 0; sample < sampleCount * 4 && validSamples < sampleCount; ++sample) {
        rules::Cards shuffled = unknown;
        ShuffleSample(shuffled, MixSeed(seed, static_cast<std::uint32_t>(sample + 17)));

        rules::Cards nextHand;
        rules::Cards otherHand;
        nextHand.reserve(static_cast<std::size_t>(nextCards));
        otherHand.reserve(static_cast<std::size_t>(otherCards));
        for (int i = 0; i < nextCards; ++i) {
            nextHand.push_back(shuffled[static_cast<std::size_t>(i)]);
        }
        for (int i = 0; i < otherCards; ++i) {
            otherHand.push_back(shuffled[static_cast<std::size_t>(nextCards + i)]);
        }
        if (!IsConsistentWithPassHistory(nextHand, next, context) ||
            !IsConsistentWithPassHistory(otherHand, other, context)) {
            continue;
        }
        validSamples++;

        RolloutState state;
        state.hands[static_cast<std::size_t>(self)] = candidate.remainder;
        state.hands[static_cast<std::size_t>(next)] = std::move(nextHand);
        state.hands[static_cast<std::size_t>(other)] = std::move(otherHand);
        state.currentIndex = next;
        state.lastMoveIndex = self;
        state.trickLeaderIndex = self;
        state.roundLeaderIndex = context.roundLeaderIndex;
        state.lastPattern = candidate.pattern;
        state.playedCards = context.playedCards;
        state.playedCards.insert(state.playedCards.end(), candidate.cards.begin(), candidate.cards.end());
        state.passObservations = context.passObservations;
        state.passHistory = context.passHistory;

        const int winner = RolloutWinner(std::move(state), self);
        const int selfWinScore = self == context.roundLeaderIndex ? 3800 : 2600;
        if (winner == self) {
            score += selfWinScore;
        } else if (winner >= 0) {
            score += winner == other ? -700 : -2300;
        }
    }
    return validSamples > 0 ? score / validSamples : 0;
}

bool ShouldUseRollout(const Candidate& candidate, const AiContext& context) {
    return candidate.remainder.size() <= 10 || context.minOpponentRemainingCards <= 8;
}

int StrongAdjustment(const Candidate& candidate, const AiContext& context) {
    int score = 0;
    if (context.leading && candidate.pattern.type == rules::PatternType::Bomb &&
        !candidate.remainder.empty() && candidate.remainder.size() <= 8) {
        score += 1600 + LeadCountPlanBonus(candidate.remainder);
    }

    if (context.leading &&
        context.currentPlayerIndex == context.roundLeaderIndex &&
        context.ownRemainingCards >= 13 &&
        candidate.pattern.cardCount >= 5 &&
        candidate.remainder.size() <= 11) {
        score += 950 + candidate.pattern.cardCount * 85;
    }

    if (!context.leading &&
        context.currentTrickPassCount > 0 &&
        !candidate.remainder.empty()) {
        const int pressure = UnknownPatternBeaterPressure(candidate, context);
        if (pressure == 0) {
            score += 950 + candidate.pattern.cardCount * 70;
        } else if (pressure <= 2 && candidate.remainder.size() <= 8) {
            score += 280 + candidate.pattern.cardCount * 35;
        }
    }

    if (!context.leading &&
        context.previous.type == rules::PatternType::Single &&
        candidate.pattern.type == rules::PatternType::Single &&
        context.ownRemainingCards > 10 &&
        context.minOpponentRemainingCards > 5 &&
        rules::RankValue(context.previous.mainRank) <= rules::RankValue(rules::Rank::Seven) &&
        candidate.pattern.mainRank >= rules::Rank::King) {
        const int previousRank = rules::RankValue(context.previous.mainRank);
        const int candidateRank = rules::RankValue(candidate.pattern.mainRank);
        bool lowerBeaterExists = false;
        for (rules::Card card : candidate.remainder) {
            const int rankValue = rules::RankValue(card.rank);
            if (rankValue > previousRank && rankValue < candidateRank) {
                lowerBeaterExists = true;
                break;
            }
        }
        if (lowerBeaterExists) {
            score -= candidate.pattern.mainRank == rules::Rank::Two ? 1900 : 1150;
        }
    }

    if (!context.leading && !candidate.remainder.empty() &&
        context.minOpponentRemainingCards > 2 &&
        (candidate.pattern.type == rules::PatternType::Single ||
         candidate.pattern.type == rules::PatternType::Pair)) {
        const auto playedCounts = CountRanks(candidate.cards);
        rules::Cards before = candidate.remainder;
        before.insert(before.end(), candidate.cards.begin(), candidate.cards.end());
        const auto beforeCounts = CountRanks(before);
        for (const auto& [rank, playedCount] : playedCounts) {
            const int beforeCount = beforeCounts.contains(rank) ? beforeCounts.at(rank) : 0;
            if (beforeCount >= 4 && playedCount < 4) {
                score -= 1700 + rules::RankValue(rank) * 24;
            } else if (beforeCount == 3 && playedCount < 3) {
                score -= 1180 + rules::RankValue(rank) * 24;
            } else if (beforeCount == 2 && playedCount == 1) {
                score -= 420 + rules::RankValue(rank) * 12;
            }
        }
    }

    return score;
}

} // namespace ai_internal

using namespace ai_internal;

AiMoveChoice StrongAiStrategy::ChooseMove(const rules::Cards& hand, const AiContext& context) {
    std::vector<Candidate> candidates = GenerateCandidates(hand, context);
    if (candidates.empty()) {
        return AiMoveChoice{true, {}, {}, context.leading ? "强 AI 没有可出的牌型" : "强 AI 压牌失败"};
    }

    for (Candidate& candidate : candidates) {
        candidate.score += StrongAdjustment(candidate, context);
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        if (lhs.cards.size() != rhs.cards.size()) {
            return lhs.cards.size() > rhs.cards.size();
        }
        if (lhs.pattern.type != rhs.pattern.type) {
            return PatternBaseScore(lhs.pattern.type) > PatternBaseScore(rhs.pattern.type);
        }
        return rules::RankValue(lhs.pattern.mainRank) < rules::RankValue(rhs.pattern.mainRank);
    });
    if (context.leading && context.currentPlayerIndex == context.roundLeaderIndex) {
        DeduplicateCandidates(candidates);
    }

    const int planLimit = std::min(context.leading ? 24 : 18, static_cast<int>(candidates.size()));
    for (int i = 0; i < planLimit; ++i) {
        Candidate& candidate = candidates[static_cast<std::size_t>(i)];
        if (!context.leading || candidate.remainder.size() <= 12) {
            const int planBonus = LeadCountPlanBonus(candidate.remainder);
            const int leadPlanWeight = context.currentPlayerIndex == context.roundLeaderIndex ? 3 : 5;
            candidate.score += context.leading ? planBonus * leadPlanWeight : planBonus;
        }
        if (!context.leading && context.currentTrickPassCount > 0 && candidate.remainder.size() <= 10) {
            candidate.score += SampledControlBonus(candidate, context, hand);
        }
        if (!context.leading && candidate.remainder.size() <= 8) {
            candidate.score += RemainderFinishSafetyBonus(candidate, context) / 2;
        }
    }
    std::sort(candidates.begin(), candidates.begin() + planLimit, [](const Candidate& lhs, const Candidate& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        if (lhs.cards.size() != rhs.cards.size()) {
            return lhs.cards.size() > rhs.cards.size();
        }
        if (lhs.pattern.type != rhs.pattern.type) {
            return PatternBaseScore(lhs.pattern.type) > PatternBaseScore(rhs.pattern.type);
        }
        return rules::RankValue(lhs.pattern.mainRank) < rules::RankValue(rhs.pattern.mainRank);
    });

    const int leaderRolloutLimit = 8;
    const int followRolloutLimit = 4;
    const int rolloutLimit = context.leading
        ? std::min(leaderRolloutLimit, static_cast<int>(candidates.size()))
        : std::min(followRolloutLimit, static_cast<int>(candidates.size()));
    for (int i = 0; i < rolloutLimit; ++i) {
        Candidate& candidate = candidates[static_cast<std::size_t>(i)];
        if (candidate.remainder.empty()) {
            continue;
        }
        const bool useFollowRollout = !context.leading &&
            context.currentTrickPassCount > 0 &&
            candidate.remainder.size() <= 8;
        if (context.leading ? ShouldUseRollout(candidate, context) : useFollowRollout) {
            const int rolloutBonus = RolloutBonus(candidate, context, hand);
            candidate.score = rolloutBonus * 24 + candidate.score / 120;
        }
    }
    std::sort(candidates.begin(), candidates.begin() + rolloutLimit, [](const Candidate& lhs, const Candidate& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        if (lhs.cards.size() != rhs.cards.size()) {
            return lhs.cards.size() > rhs.cards.size();
        }
        if (lhs.pattern.type != rhs.pattern.type) {
            return PatternBaseScore(lhs.pattern.type) > PatternBaseScore(rhs.pattern.type);
        }
        return rules::RankValue(lhs.pattern.mainRank) < rules::RankValue(rhs.pattern.mainRank);
    });

    const Candidate& best = candidates.front();
    return AiMoveChoice{
        false,
        best.cards,
        best.pattern,
        "强 AI 推荐 " + rules::PatternName(best.pattern.type),
        best.disruptionPenalty
    };
}

} // namespace pdk::game
