#include "game/AiStrategy.h"
#include "game/AiStrategyInternal.h"

#include "rules/CppCompat.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
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
    case PATTERN_STRAIGHT:
    case PATTERN_CONSECUTIVE_PAIRS:
    case PATTERN_PLANE:
        return lhs.cardCount == rhs.cardCount && lhs.groupCount == rhs.groupCount;
    case PATTERN_SINGLE:
    case PATTERN_PAIR:
    case PATTERN_TRIPLE_WITH_ONE:
    case PATTERN_TRIPLE_WITH_PAIR:
    case PATTERN_BOMB:
        return lhs.cardCount == rhs.cardCount;
    case PATTERN_INVALID:
        return false;
    }
    return false;
}

bool ObservationProvesCannotBeat(const Candidate& candidate, const PassObservation& observation) {
    if (observation.remainingCards <= 0) {
        return false;
    }
    if (candidate.pattern.type == PATTERN_BOMB &&
        observation.pattern.type != PATTERN_BOMB) {
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
        const int rankOffset = rules::RankValue(card.rank) - rules::RankValue(RANK_THREE);
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
    static std::mutex cacheMutex;
    const FollowKey key{CardSetMask(hand), PatternKey(previous), handSizeBeforePlay};
    {
        const std::lock_guard<std::mutex> lock(cacheMutex);
        const auto cached = cache.find(key);
        if (cached != cache.end()) {
            return cached->second;
        }
    }
    const bool result = rules::HasAnyFollowMove(hand, previous, handSizeBeforePlay);
    {
        const std::lock_guard<std::mutex> lock(cacheMutex);
        cache[key] = result;
    }
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
    static std::mutex cacheMutex;
    const std::uint64_t cacheKey = CardSetMask(cards);
    {
        const std::lock_guard<std::mutex> lock(cacheMutex);
        const auto cached = cache.find(cacheKey);
        if (cached != cache.end()) {
            return cached->second;
        }
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

    std::vector<std::vector<int>> legalByCard(static_cast<std::size_t>(n));
    for (int legal : legalMasks) {
        for (int i = 0; i < n; ++i) {
            if ((legal & (1 << i)) != 0) {
                legalByCard[static_cast<std::size_t>(i)].push_back(legal);
            }
        }
    }

    constexpr int inf = 99;
    std::vector<int> dp(static_cast<std::size_t>(fullMask + 1), inf);
    dp[0] = 0;
    for (int remaining = 1; remaining <= fullMask; ++remaining) {
        int first = 0;
        while ((remaining & (1 << first)) == 0) {
            ++first;
        }
        int best = inf;
        for (int legal : legalByCard[static_cast<std::size_t>(first)]) {
            if ((legal & remaining) == legal) {
                best = std::min(best, 1 + dp[static_cast<std::size_t>(remaining ^ legal)]);
            }
        }
        dp[static_cast<std::size_t>(remaining)] = best;
    }
    const int result = dp[static_cast<std::size_t>(fullMask)];
    {
        const std::lock_guard<std::mutex> lock(cacheMutex);
        cache[cacheKey] = result;
    }
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
        if (rank == RANK_TWO || rank == RANK_ACE) {
            controls += count;
        } else if (rank == RANK_KING && count >= 1) {
            controls += 1;
        }
        if (count >= 4 && rank >= RANK_THREE && rank <= RANK_KING) {
            controls += 2;
        }
    }
    return controls;
}

int UnknownBombRankCount(const Candidate& candidate, const AiContext& context) {
    int bombs = 0;
    for (int value = rules::RankValue(RANK_THREE); value <= rules::RankValue(RANK_KING); ++value) {
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
    case PATTERN_SINGLE:
        for (int value = rules::RankValue(pattern.mainRank) + 1; value <= rules::RankValue(RANK_TWO); ++value) {
            pressure += UnknownRankCount(candidate, context, static_cast<rules::Rank>(value));
        }
        break;
    case PATTERN_PAIR:
        for (int value = rules::RankValue(pattern.mainRank) + 1; value <= rules::RankValue(RANK_ACE); ++value) {
            if (UnknownRankCount(candidate, context, static_cast<rules::Rank>(value)) >= 2) {
                pressure += 2;
            }
        }
        break;
    case PATTERN_STRAIGHT:
    case PATTERN_CONSECUTIVE_PAIRS:
    case PATTERN_PLANE:
    case PATTERN_TRIPLE_WITH_ONE:
    case PATTERN_TRIPLE_WITH_PAIR:
        for (int value = rules::RankValue(pattern.mainRank) + 1; value <= rules::RankValue(RANK_ACE); ++value) {
            if (UnknownRankCount(candidate, context, static_cast<rules::Rank>(value)) >= 3) {
                pressure += 1;
            }
        }
        break;
    case PATTERN_BOMB:
        for (int value = rules::RankValue(pattern.mainRank) + 1; value <= rules::RankValue(RANK_KING); ++value) {
            if (UnknownRankCount(candidate, context, static_cast<rules::Rank>(value)) >= 4) {
                pressure += 4;
            }
        }
        break;
    case PATTERN_INVALID:
        break;
    }
    if (pattern.type != PATTERN_BOMB) {
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

int CachedUnknownSampledControlBonus(
    const Candidate& candidate,
    const AiContext& context,
    const rules::Cards& unknown) {
    const int sampleCount = context.currentPlayerIndex == context.roundLeaderIndex ? 7 : 5;
    const int nextIndex = (context.currentPlayerIndex + 2) % 3;
    const int otherIndex = (context.currentPlayerIndex + 1) % 3;
    const int nextCards = context.remainingCards[static_cast<std::size_t>(nextIndex)];
    const int otherCards = context.remainingCards[static_cast<std::size_t>(otherIndex)];
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
        AiMoveChoice choice;
        if (state.currentIndex == strongIndex) {
            choice = ChooseRolloutMove(hand, RolloutContext(state), true);
        } else {
            choice = basic.ChooseMove(hand, RolloutContext(state));
        }
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
    int nextCards,
    bool parallelEvaluation) {
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

    const auto scoreMask = [&](int mask, int& total, int& count) {
        if (CountMaskBits(mask) != nextCards) {
            return;
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
            return;
        }

        total += ScoreRolloutDistribution(
            candidate, context, self, next, other,
            std::move(nextHand), std::move(otherHand));
        count++;
    };

    int total = 0;
    int count = 0;
    if (parallelEvaluation) {
        std::atomic<int> nextMask{0};
        std::atomic<int> parallelTotal{0};
        std::atomic<int> parallelCount{0};
        const auto worker = [&]() {
            int localTotal = 0;
            int localCount = 0;
            for (;;) {
                const int mask = nextMask.fetch_add(1);
                if (mask >= limit) {
                    break;
                }
                scoreMask(mask, localTotal, localCount);
            }
            parallelTotal.fetch_add(localTotal);
            parallelCount.fetch_add(localCount);
        };
        std::array<std::thread, 8> workers{
            std::thread(worker), std::thread(worker), std::thread(worker), std::thread(worker),
            std::thread(worker), std::thread(worker), std::thread(worker), std::thread(worker)
        };
        worker();
        for (std::thread& thread : workers) {
            thread.join();
        }
        total = parallelTotal.load();
        count = parallelCount.load();
    } else {
        for (int mask = 0; mask < limit; ++mask) {
            scoreMask(mask, total, count);
        }
    }

    if (count == 0) {
        return std::nullopt;
    }
    return total / count;
}

bool ShouldUseExactRolloutEnumeration(int unknownCount, int nextCards) {
    if (unknownCount > 12 || nextCards < 0 || nextCards > unknownCount) {
        return false;
    }

    int combinations = 0;
    const int limit = 1 << unknownCount;
    for (int mask = 0; mask < limit; ++mask) {
        if (CountMaskBits(mask) == nextCards) {
            combinations++;
            if (combinations > 1200) {
                return false;
            }
        }
    }
    return true;
}

int FastRolloutBonus(
    const Candidate& candidate,
    const AiContext& context,
    const rules::Cards& unknown,
    int sampleCount = 5) {
    if (candidate.remainder.size() > 10 && context.minOpponentRemainingCards > 8) {
        return 0;
    }

    const int self = context.currentPlayerIndex;
    const int next = NextIndex(self);
    const int other = NextIndex(next);
    const int nextCards = context.remainingCards[static_cast<std::size_t>(next)];
    const int otherCards = context.remainingCards[static_cast<std::size_t>(other)];
    if (static_cast<int>(unknown.size()) != nextCards + otherCards) {
        return 0;
    }

    if (ShouldUseExactRolloutEnumeration(static_cast<int>(unknown.size()), nextCards)) {
        if (std::optional<int> enumerated = EnumeratedRolloutBonus(
                candidate, context, unknown, self, next, other, nextCards,
                sampleCount > 5)) {
            return *enumerated;
        }
    }

    const int attemptCount = sampleCount * 4;
    std::vector<int> sampleScores(static_cast<std::size_t>(attemptCount));
    std::vector<unsigned char> sampleValid(static_cast<std::size_t>(attemptCount));
    const std::uint32_t seed = CandidateSeed(candidate, context);
    std::vector<int> validAttemptIndices;
    validAttemptIndices.reserve(static_cast<std::size_t>(sampleCount));
    for (int sample = 0; sample < attemptCount && static_cast<int>(validAttemptIndices.size()) < sampleCount; ++sample) {
        rules::Cards shuffled = unknown;
        ShuffleSample(shuffled, MixSeed(seed, static_cast<std::uint32_t>(sample + 17)));
        rules::Cards nextHand(shuffled.begin(), shuffled.begin() + nextCards);
        rules::Cards otherHand(shuffled.begin() + nextCards, shuffled.end());
        if (IsConsistentWithPassHistory(nextHand, next, context) &&
            IsConsistentWithPassHistory(otherHand, other, context)) {
            validAttemptIndices.push_back(sample);
        }
    }
    const auto runSample = [&](int sample) {
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
            return;
        }
        sampleValid[static_cast<std::size_t>(sample)] = 1;

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
            sampleScores[static_cast<std::size_t>(sample)] = selfWinScore;
        } else if (winner >= 0) {
            sampleScores[static_cast<std::size_t>(sample)] = winner == other ? -700 : -2300;
        }
    };

    if (sampleCount > 5) {
        std::atomic<int> nextTask{0};
        const auto worker = [&]() {
            for (;;) {
                const int task = nextTask.fetch_add(1);
                if (task >= static_cast<int>(validAttemptIndices.size())) {
                    return;
                }
                runSample(validAttemptIndices[static_cast<std::size_t>(task)]);
            }
        };
        std::array<std::thread, 8> workers{
            std::thread(worker), std::thread(worker), std::thread(worker), std::thread(worker),
            std::thread(worker), std::thread(worker), std::thread(worker), std::thread(worker)
        };
        worker();
        for (std::thread& thread : workers) {
            thread.join();
        }
    } else {
        for (int sample : validAttemptIndices) {
            runSample(sample);
        }
    }

    int score = 0;
    int validSamples = 0;
    for (int sample = 0; sample < attemptCount && validSamples < sampleCount; ++sample) {
        if (sampleValid[static_cast<std::size_t>(sample)] == 0) {
            continue;
        }
        score += sampleScores[static_cast<std::size_t>(sample)];
        validSamples++;
    }
    return validSamples > 0 ? score / validSamples : 0;
}

bool ShouldUseRollout(const Candidate& candidate, const AiContext& context) {
    return candidate.remainder.size() <= 10 || context.minOpponentRemainingCards <= 8;
}

int StrongPostRolloutAdjustment(const Candidate& candidate, const AiContext& context) {
    int score = -KickerControlPenalty(candidate) * 10;
    const bool anyOpponentSingle = context.minOpponentRemainingCards == 1;
    const bool singleBlocker = candidate.pattern.type == PATTERN_SINGLE &&
        (context.leading || (!context.leading && context.previous.type == PATTERN_SINGLE));
    if (anyOpponentSingle && singleBlocker && !candidate.remainder.empty()) {
        score += rules::RankValue(candidate.pattern.mainRank) * 180;
        if (candidate.pattern.mainRank >= RANK_KING) {
            score += 1100;
        } else if (candidate.pattern.mainRank <= RANK_TEN) {
            score -= 900;
        }
    }
    return score;
}

bool IsPreferredEarlyLead(rules::PatternType type) {
    return type == PATTERN_STRAIGHT ||
        type == PATTERN_CONSECUTIVE_PAIRS ||
        type == PATTERN_TRIPLE_WITH_PAIR ||
        type == PATTERN_PLANE;
}

int StrongAdjustment(const Candidate& candidate, const AiContext& context) {
    int score = 0;
    if (context.leading && candidate.pattern.type == PATTERN_BOMB &&
        !candidate.remainder.empty() && candidate.remainder.size() <= 8) {
        score += 1600 + LeadCountPlanBonus(candidate.remainder);
    }

    if (context.leading &&
        context.currentPlayerIndex == context.roundLeaderIndex &&
        context.ownRemainingCards >= 13 &&
        candidate.pattern.cardCount >= 4 && IsPreferredEarlyLead(candidate.pattern.type) &&
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
        context.previous.type == PATTERN_SINGLE &&
        candidate.pattern.type == PATTERN_SINGLE &&
        context.ownRemainingCards > 10 &&
        context.minOpponentRemainingCards > 5 &&
        rules::RankValue(context.previous.mainRank) <= rules::RankValue(RANK_SEVEN) &&
        candidate.pattern.mainRank >= RANK_KING) {
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
            score -= candidate.pattern.mainRank == RANK_TWO ? 1900 : 1150;
        }
    }

    if (!context.leading && !candidate.remainder.empty() &&
        context.minOpponentRemainingCards > 2 &&
        (candidate.pattern.type == PATTERN_SINGLE ||
         candidate.pattern.type == PATTERN_PAIR)) {
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

bool StrongCandidateBetter(const Candidate& lhs, const Candidate& rhs) {
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
}

int PartialBombCardsUsed(const Candidate& candidate, const rules::Cards& hand) {
    const auto handCounts = CountRanks(hand);
    const auto playedCounts = CountRanks(candidate.cards);
    int used = 0;
    for (const auto& [rank, count] : handCounts) {
        if (count != 4) {
            continue;
        }
        const auto it = playedCounts.find(rank);
        if (it != playedCounts.end() && it->second > 0 && it->second < 4) {
            used = std::max(used, it->second);
        }
    }
    return used;
}

void FilterStrongBombSplits(std::vector<Candidate>& candidates, const rules::Cards& hand, const AiContext& context) {
    if (!context.leading || hand.size() > 7) {
        return;
    }
    const auto handCounts = CountRanks(hand);
    const bool hasBomb = std::any_of(handCounts.begin(), handCounts.end(), [](const auto& entry) {
        return entry.second == 4;
    });
    if (!hasBomb) {
        return;
    }

    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const Candidate& candidate) {
        const int used = PartialBombCardsUsed(candidate, hand);
        if (used == 0 || candidate.remainder.empty()) {
            return false;
        }
        return used >= 3;
    }), candidates.end());
}

void FilterStrongMidgameSingleSplits(
    std::vector<Candidate>& candidates,
    const rules::Cards& hand,
    const AiContext& context) {
    if (!context.leading || hand.size() < 5 || hand.size() > 8) {
        return;
    }
    const auto handCounts = CountRanks(hand);
    const bool hasSingleton = std::any_of(handCounts.begin(), handCounts.end(), [](const auto& entry) {
        return entry.second == 1;
    });
    if (!hasSingleton) {
        return;
    }
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const Candidate& candidate) {
        if (candidate.pattern.type != PATTERN_SINGLE || candidate.cards.empty()) {
            return false;
        }
        const auto found = handCounts.find(candidate.cards.front().rank);
        return found != handCounts.end() && found->second >= 2;
    }), candidates.end());
}

void PromotePreferredLeadTypes(std::vector<Candidate>& candidates, int limit) {
    if (limit <= 0 || static_cast<int>(candidates.size()) <= limit) {
        return;
    }
    const std::array<rules::PatternType, 4> preferred{
        PATTERN_STRAIGHT,
        PATTERN_CONSECUTIVE_PAIRS,
        PATTERN_TRIPLE_WITH_PAIR,
        PATTERN_PLANE
    };
    int replace = limit - 1;
    for (rules::PatternType type : preferred) {
        const bool alreadyPresent = std::any_of(candidates.begin(), candidates.begin() + limit, [type](const Candidate& candidate) {
            return candidate.pattern.type == type;
        });
        if (alreadyPresent) {
            continue;
        }
        const auto found = std::find_if(candidates.begin() + limit, candidates.end(), [type](const Candidate& candidate) {
            return candidate.pattern.type == type;
        });
        if (found != candidates.end() && replace >= 0) {
            std::iter_swap(candidates.begin() + replace, found);
            --replace;
        }
    }
}

} // namespace ai_internal

using namespace ai_internal;

namespace {

AiMoveChoice ChooseStrongMove(const rules::Cards& hand, const AiContext& context) {
    std::vector<Candidate> candidates = GenerateCandidates(hand, context);
    if (candidates.empty()) {
        return AiMoveChoice{true, {}, {}, context.leading ? "强 AI 没有可出的牌型" : "强 AI 压牌失败"};
    }

    for (Candidate& candidate : candidates) {
        candidate.score += StrongAdjustment(candidate, context);
    }

    std::sort(candidates.begin(), candidates.end(), StrongCandidateBetter);
    if (context.leading && context.currentPlayerIndex == context.roundLeaderIndex) {
        DeduplicateCandidates(candidates);
    }
    FilterStrongBombSplits(candidates, hand, context);
    FilterStrongMidgameSingleSplits(candidates, hand, context);
    if (candidates.empty()) {
        return AiMoveChoice{true, {}, {}, context.leading ? "强 AI 没有可出的牌型" : "强 AI 压牌失败"};
    }

    std::optional<rules::Cards> unknown;
    const auto getUnknown = [&]() -> const rules::Cards& {
        if (!unknown) {
            unknown = UnknownOpponentCards(hand, context);
        }
        return *unknown;
    };

    const int planLimit = std::min(context.leading ? 24 : 18, static_cast<int>(candidates.size()));
    if (context.leading && context.ownRemainingCards >= 13) {
        PromotePreferredLeadTypes(candidates, planLimit);
    }
    for (int i = 0; i < planLimit; ++i) {
        Candidate& candidate = candidates[static_cast<std::size_t>(i)];
        if (!context.leading || candidate.remainder.size() <= 12) {
            const int planBonus = LeadCountPlanBonus(candidate.remainder);
            const bool strongEarlyLead = context.currentPlayerIndex == context.roundLeaderIndex &&
                context.ownRemainingCards >= 13;
            const int leadPlanWeight = strongEarlyLead
                ? 5
                : (context.currentPlayerIndex == context.roundLeaderIndex ? 3 : 5);
            candidate.score += context.leading ? planBonus * leadPlanWeight : planBonus;
        }
        if (!context.leading && context.currentTrickPassCount > 0 && candidate.remainder.size() <= 10) {
            candidate.score += CachedUnknownSampledControlBonus(candidate, context, getUnknown());
        }
        if (!context.leading && candidate.remainder.size() <= 8) {
            candidate.score += RemainderFinishSafetyBonus(candidate, context) / 2;
        }
    }
    std::sort(candidates.begin(), candidates.begin() + planLimit, StrongCandidateBetter);

    const bool useStrongLeadPlanner = context.leading && context.ownRemainingCards >= 9;
    const int leaderRolloutLimit = 8;
    const int followRolloutLimit = 4;
    const int rolloutLimit = context.leading
        ? std::min(leaderRolloutLimit, static_cast<int>(candidates.size()))
        : std::min(followRolloutLimit, static_cast<int>(candidates.size()));
    int strongPlannerBestPlan = 99;
    if (useStrongLeadPlanner) {
        for (int i = 0; i < rolloutLimit; ++i) {
            const Candidate& candidate = candidates[static_cast<std::size_t>(i)];
            const int plan = candidate.remainder.size() <= 12
                ? MinimumLeadCount(candidate.remainder)
                : 99;
            if (plan < strongPlannerBestPlan) {
                strongPlannerBestPlan = plan;
            }
        }
    }
    for (int i = 0; i < rolloutLimit; ++i) {
        Candidate& candidate = candidates[static_cast<std::size_t>(i)];
        if (candidate.remainder.empty()) {
            continue;
        }
        const bool useFollowRollout = !context.leading &&
            context.currentTrickPassCount > 0 &&
            candidate.remainder.size() <= 8;
        bool replacedByRollout = false;
        const int plannerCandidatePlan = useStrongLeadPlanner && candidate.remainder.size() <= 12
            ? MinimumLeadCount(candidate.remainder)
            : 99;
        const bool canWinStrongPlanner = !useStrongLeadPlanner ||
            plannerCandidatePlan == strongPlannerBestPlan;
        if (canWinStrongPlanner &&
            (context.leading ? ShouldUseRollout(candidate, context) : useFollowRollout)) {
            const int rolloutBonus = FastRolloutBonus(
                candidate, context, getUnknown(),
                useStrongLeadPlanner ? 9 : 5);
            candidate.score = rolloutBonus * 24 + candidate.score / 120;
            replacedByRollout = true;
        }
        candidate.score += StrongPostRolloutAdjustment(candidate, context);
        if (replacedByRollout && hand.size() <= 7 &&
            PartialBombCardsUsed(candidate, hand) > 0) {
            candidate.score -= candidate.disruptionPenalty * 2;
        }
        if (replacedByRollout &&
            context.leading &&
                context.currentPlayerIndex == context.roundLeaderIndex &&
                context.ownRemainingCards >= 13) {
            candidate.score += LeadCountPlanBonus(candidate.remainder) * 3;
        }
        if (!context.leading && !candidate.remainder.empty() &&
            context.ownRemainingCards <= 4 &&
            context.minOpponentRemainingCards > 4 &&
            (candidate.pattern.type == PATTERN_SINGLE ||
             candidate.pattern.type == PATTERN_PAIR)) {
            candidate.score -= rules::RankValue(candidate.pattern.mainRank) * 30;
        }
    }
    std::sort(candidates.begin(), candidates.begin() + rolloutLimit, StrongCandidateBetter);

    const Candidate* best = &candidates.front();
    if (context.leading &&
        context.ownRemainingCards >= 9) {
        const int plannerLimit = std::min(8, static_cast<int>(candidates.size()));
        long long bestSelectionScore = -0x7fffffffffffffffLL;
        for (int i = 0; i < plannerLimit; ++i) {
            const Candidate& candidate = candidates[static_cast<std::size_t>(i)];
            const int candidatePlan = candidate.remainder.size() <= 12
                ? MinimumLeadCount(candidate.remainder)
                : 99;
            if (candidatePlan != strongPlannerBestPlan) {
                continue;
            }
            const long long selectionScore = static_cast<long long>(candidate.score) -
                static_cast<long long>(candidatePlan) * 20000LL +
                static_cast<long long>(candidate.cards.size()) * 1000LL;
            if (selectionScore > bestSelectionScore) {
                best = &candidate;
                bestSelectionScore = selectionScore;
            }
        }
    }
    return AiMoveChoice{
        false,
        best->cards,
        best->pattern,
        "强 AI 推荐 " + rules::PatternName(best->pattern.type),
        best->disruptionPenalty
    };
}

} // namespace

AiMoveChoice StrongAiStrategy::ChooseMove(const rules::Cards& hand, const AiContext& context) {
    return ChooseStrongMove(hand, context);
}

} // namespace pdk::game
