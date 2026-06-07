#include "game/AiStrategy.h"

#include <algorithm>
#include <limits>
#include <map>
#include <vector>

namespace pdk::game {
namespace {

struct Candidate {
    rules::Cards cards;
    rules::HandPattern pattern;
    rules::Cards remainder;
    int disruptionPenalty{};
    int score{};
};

int PatternBaseScore(rules::PatternType type) {
    using rules::PatternType;
    switch (type) {
    case PatternType::Straight: return 800;
    case PatternType::ConsecutivePairs: return 760;
    case PatternType::Plane: return 720;
    case PatternType::TripleWithPair: return 650;
    case PatternType::TripleWithOne: return 600;
    case PatternType::Pair: return 300;
    case PatternType::Single: return 150;
    case PatternType::Bomb: return 80;
    case PatternType::Invalid: return 0;
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

bool IsConsecutive(const std::vector<rules::Rank>& ranks) {
    if (ranks.empty()) {
        return false;
    }
    for (rules::Rank rank : ranks) {
        if (rank == rules::Rank::Two) {
            return false;
        }
    }
    for (std::size_t i = 1; i < ranks.size(); ++i) {
        if (rules::RankValue(ranks[i]) != rules::RankValue(ranks[i - 1]) + 1) {
            return false;
        }
    }
    return true;
}

int BestConsecutiveRunScore(std::vector<rules::Rank> ranks, int minLength, int perRankScore) {
    if (static_cast<int>(ranks.size()) < minLength) {
        return 0;
    }
    int best = 0;
    for (std::size_t start = 0; start < ranks.size(); ++start) {
        for (std::size_t end = start; end < ranks.size(); ++end) {
            std::vector<rules::Rank> run(ranks.begin() + static_cast<std::ptrdiff_t>(start),
                ranks.begin() + static_cast<std::ptrdiff_t>(end + 1));
            if (!IsConsecutive(run)) {
                break;
            }
            const int length = static_cast<int>(run.size());
            if (length >= minLength) {
                best = std::max(best, length * perRankScore + rules::RankValue(run.back()) * 4);
            }
        }
    }
    return best;
}

std::map<rules::Rank, int> CoreUsage(const rules::Cards& cards, const rules::HandPattern& pattern) {
    std::map<rules::Rank, int> core;
    const auto playedCounts = CountRanks(cards);
    switch (pattern.type) {
    case rules::PatternType::TripleWithOne:
    case rules::PatternType::TripleWithPair:
        core[pattern.mainRank] = 3;
        break;
    case rules::PatternType::Plane: {
        std::vector<rules::Rank> tripleRanks;
        for (const auto& [rank, count] : playedCounts) {
            if (rank != rules::Rank::Two && count >= 3) {
                tripleRanks.push_back(rank);
            }
        }
        bool found = false;
        for (std::size_t start = 0; start < tripleRanks.size() && !found; ++start) {
            for (std::size_t end = start; end < tripleRanks.size(); ++end) {
                const int length = static_cast<int>(end - start + 1);
                if (length > pattern.groupCount) {
                    break;
                }
                std::vector<rules::Rank> run(tripleRanks.begin() + static_cast<std::ptrdiff_t>(start),
                    tripleRanks.begin() + static_cast<std::ptrdiff_t>(end + 1));
                if (!IsConsecutive(run)) {
                    break;
                }
                if (length == pattern.groupCount && run.back() == pattern.mainRank) {
                    for (rules::Rank rank : run) {
                        core[rank] = 3;
                    }
                    found = true;
                    break;
                }
            }
        }
        break;
    }
    case rules::PatternType::Invalid:
        break;
    case rules::PatternType::Single:
    case rules::PatternType::Pair:
    case rules::PatternType::Straight:
    case rules::PatternType::ConsecutivePairs:
    case rules::PatternType::Bomb:
        return playedCounts;
    }
    return core;
}

int GroupDisruptionPenalty(
    const std::map<rules::Rank, int>& beforeCounts,
    const rules::Cards& played,
    const rules::HandPattern& pattern) {
    const auto playedCounts = CountRanks(played);
    const auto coreUsage = CoreUsage(played, pattern);
    int penalty = 0;

    for (const auto& [rank, beforeCount] : beforeCounts) {
        const int used = playedCounts.contains(rank) ? playedCounts.at(rank) : 0;
        if (used == 0) {
            continue;
        }

        const int coreUsed = coreUsage.contains(rank) ? coreUsage.at(rank) : 0;
        const int kickerUsed = std::max(0, used - coreUsed);
        if (kickerUsed > 0) {
            if (beforeCount >= 4) {
                penalty += 900;
            } else if (beforeCount == 3) {
                penalty += 620;
            } else if (beforeCount == 2) {
                penalty += 360;
            }
            penalty += rules::RankValue(rank) * 8;
        }

        if (beforeCount >= 4 && used > 0 && used < 4) {
            penalty += 900;
        } else if (beforeCount == 3 && used > 0 && used < 3) {
            penalty += 520;
        } else if (beforeCount == 2 && used == 1) {
            penalty += 320;
        }
    }

    return penalty;
}

int EvaluateRemainingHand(const rules::Cards& cards) {
    if (cards.empty()) {
        return 6000;
    }

    const auto counts = CountRanks(cards);
    int score = -static_cast<int>(cards.size()) * 10;
    int singleCount = 0;
    std::vector<rules::Rank> straightRanks;
    std::vector<rules::Rank> pairRanks;
    std::vector<rules::Rank> tripleRanks;

    for (const auto& [rank, count] : counts) {
        if (count == 1) {
            singleCount++;
            score -= 130 - rules::RankValue(rank) * 4;
        } else if (count == 2) {
            score += 220 + rules::RankValue(rank) * 5;
        } else if (count == 3) {
            score += 430 + rules::RankValue(rank) * 7;
        } else if (count >= 4) {
            score += 900 + rules::RankValue(rank) * 10;
        }

        if (rank != rules::Rank::Two) {
            straightRanks.push_back(rank);
        }
        if (count >= 2 && rank != rules::Rank::Two) {
            pairRanks.push_back(rank);
        }
        if (count >= 3 && rank != rules::Rank::Two) {
            tripleRanks.push_back(rank);
        }
    }

    score -= singleCount * singleCount * 35;
    score += BestConsecutiveRunScore(straightRanks, 5, 95);
    score += BestConsecutiveRunScore(pairRanks, 2, 115);
    score += BestConsecutiveRunScore(tripleRanks, 2, 190);
    if (cards.size() <= 2) {
        score += 320;
    }
    return score;
}

int TotalCardsOfRank(rules::Rank rank) {
    if (rank == rules::Rank::Two) {
        return 1;
    }
    if (rank == rules::Rank::Ace) {
        return 3;
    }
    return 4;
}

int CountRank(const rules::Cards& cards, rules::Rank rank) {
    int count = 0;
    for (rules::Card card : cards) {
        if (card.rank == rank) {
            count++;
        }
    }
    return count;
}

int UnknownRankCount(const Candidate& candidate, const AiContext& context, rules::Rank rank) {
    const int known = CountRank(context.playedCards, rank)
        + CountRank(candidate.cards, rank)
        + CountRank(candidate.remainder, rank);
    return std::max(0, TotalCardsOfRank(rank) - known);
}

int UnknownHigherControlCount(const Candidate& candidate, const AiContext& context, rules::Rank rank) {
    int count = 0;
    if (rank < rules::Rank::King) {
        count += UnknownRankCount(candidate, context, rules::Rank::King);
    }
    if (rank < rules::Rank::Ace) {
        count += UnknownRankCount(candidate, context, rules::Rank::Ace);
    }
    if (rank < rules::Rank::Two) {
        count += UnknownRankCount(candidate, context, rules::Rank::Two);
    }
    return count;
}

int ProvenSingleControlBonus(const Candidate& candidate, const AiContext& context) {
    if (candidate.pattern.type != rules::PatternType::Single ||
        candidate.pattern.mainRank >= rules::Rank::Ace) {
        return 0;
    }

    int best = 0;
    const int candidateRank = rules::RankValue(candidate.pattern.mainRank);
    for (int i = 0; i < static_cast<int>(context.passObservations.size()); ++i) {
        if (i == context.currentPlayerIndex || context.remainingCards[i] <= 0) {
            continue;
        }
        const std::optional<PassObservation>& observation = context.passObservations[static_cast<std::size_t>(i)];
        if (!observation || observation->pattern.type != rules::PatternType::Single) {
            continue;
        }

        const int failedRank = rules::RankValue(observation->pattern.mainRank);
        if (candidateRank >= failedRank) {
            // Consume only same-pattern information: failing to beat Q single
            // proves Q/K below A can be useful leads, but says nothing about
            // pairs, straights, or wing choices.
            best = std::max(best, 760 - (candidateRank - failedRank) * 35);
        }
    }
    return best;
}

int TacticalAdjustment(const Candidate& candidate, const AiContext& context) {
    const int leaves = static_cast<int>(candidate.remainder.size());
    int score = 0;
    const bool urgentDefense = context.nextPlayerRemainingCards == 1 ||
        (context.minOpponentRemainingCards > 0 && context.minOpponentRemainingCards <= 2);

    if (context.leading && context.nextPlayerRemainingCards == 1) {
        if (candidate.pattern.type == rules::PatternType::Single) {
            score -= 900;
            score += rules::RankValue(candidate.pattern.mainRank) * 55;
        } else {
            score += 420 + candidate.pattern.cardCount * 25;
        }
    }

    if (context.minOpponentRemainingCards > 0 && context.minOpponentRemainingCards <= 2) {
        score += candidate.pattern.cardCount * 35;
    }

    if (context.leading && !urgentDefense && leaves > 3) {
        const int rankValue = rules::RankValue(candidate.pattern.mainRank);
        if (candidate.pattern.type == rules::PatternType::Single) {
            // Normal lead turns should usually burn low loose cards first. Without
            // this guard the remainder evaluator can prefer throwing away A/2 just
            // because the leftover low cards still form a pretty-looking structure.
            score -= rankValue * 42;
            if (rankValue >= rules::RankValue(rules::Rank::Ace)) {
                score -= 520;
            }
            if (candidate.pattern.mainRank == rules::Rank::King || candidate.pattern.mainRank == rules::Rank::Ace) {
                const int unknownHigher = UnknownHigherControlCount(candidate, context, candidate.pattern.mainRank);
                if (unknownHigher == 0) {
                    score += 260;
                } else if (unknownHigher == 1) {
                    score += 120;
                }
            }
        } else if (candidate.pattern.type == rules::PatternType::Pair) {
            score -= rankValue * 18;
            if (rankValue >= rules::RankValue(rules::Rank::Ace)) {
                score -= 360;
            }
        } else {
            score -= rankValue * 3;
        }
    }

    if (context.leading && !urgentDefense) {
        score += ProvenSingleControlBonus(candidate, context);
    }

    if (candidate.pattern.type == rules::PatternType::Bomb) {
        const bool critical = leaves == 0 || leaves <= 2 ||
            (context.minOpponentRemainingCards > 0 && context.minOpponentRemainingCards <= 2);
        score += 180;
        if (critical) {
            score += 850;
        } else {
            score -= context.leading ? 250 : 500;
        }
        if (!context.leading && context.previous.type != rules::PatternType::Bomb && !critical) {
            score -= 550;
        }
    }

    return score;
}

rules::Cards RemainingAfterPlay(const rules::Cards& hand, std::uint64_t mask) {
    rules::Cards remaining;
    for (int i = 0; i < static_cast<int>(hand.size()); ++i) {
        if ((mask & (1ull << i)) == 0) {
            remaining.push_back(hand[static_cast<std::size_t>(i)]);
        }
    }
    return remaining;
}

std::vector<Candidate> GenerateCandidates(const rules::Cards& hand, const AiContext& context) {
    std::vector<Candidate> candidates;
    const int n = static_cast<int>(hand.size());
    if (n == 0 || n > 20) {
        return candidates;
    }

    const auto handCounts = CountRanks(hand);
    const std::uint64_t limit = 1ull << n;
    for (std::uint64_t mask = 1; mask < limit; ++mask) {
        rules::Cards cards;
        cards.reserve(n);
        for (int i = 0; i < n; ++i) {
            if ((mask & (1ull << i)) != 0) {
                cards.push_back(hand[static_cast<std::size_t>(i)]);
            }
        }

        const auto validation = context.leading
            ? rules::ValidateLead(cards, n)
            : rules::ValidateFollow(cards, context.previous, n);
        if (!validation.ok) {
            continue;
        }

        rules::Cards remainder = RemainingAfterPlay(hand, mask);
        const int leaves = static_cast<int>(remainder.size());
        const int disruption = GroupDisruptionPenalty(handCounts, cards, validation.pattern);
        int score = PatternBaseScore(validation.pattern.type);
        score += static_cast<int>(cards.size()) * (context.leading ? 28 : 12);
        score -= rules::RankValue(validation.pattern.mainRank) * (context.leading ? 2 : 10);
        score += EvaluateRemainingHand(remainder);
        score -= disruption * (context.leading ? 2 : 3);
        if (leaves == 0) {
            score += 100000;
        } else if (leaves <= 2) {
            score += 450;
        }
        if (!context.leading) {
            score += EvaluateRemainingHand(remainder);
        }
        Candidate candidate{cards, validation.pattern, remainder, disruption, score};
        candidate.score += TacticalAdjustment(candidate, context);
        candidates.push_back(std::move(candidate));
    }

    return candidates;
}

} // namespace

AiMoveChoice BasicAiStrategy::ChooseMove(const rules::Cards& hand, const AiContext& context) {
    std::vector<Candidate> candidates = GenerateCandidates(hand, context);
    if (candidates.empty()) {
        return AiMoveChoice{true, {}, {}, context.leading ? "没有可出的牌型" : "压不过，选择不要"};
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

    const Candidate& chosen = candidates.front();
    return AiMoveChoice{false, chosen.cards, chosen.pattern, "基础 AI 推荐 " + rules::PatternName(chosen.pattern.type), chosen.disruptionPenalty};
}

} // namespace pdk::game
