#include "rules/HandPattern.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <optional>

namespace pdk::rules {
namespace {

std::map<Rank, int> CountRanks(const Cards& cards) {
    std::map<Rank, int> counts;
    for (Card card : cards) {
        counts[card.rank]++;
    }
    return counts;
}

bool IsConsecutive(const std::vector<Rank>& ranks) {
    if (ranks.empty()) {
        return false;
    }
    for (Rank rank : ranks) {
        if (rank == Rank::Two) {
            return false;
        }
    }
    for (std::size_t i = 1; i < ranks.size(); ++i) {
        if (RankValue(ranks[i]) != RankValue(ranks[i - 1]) + 1) {
            return false;
        }
    }
    return true;
}

Rank MaxRank(const std::vector<Rank>& ranks) {
    return *std::max_element(ranks.begin(), ranks.end(), [](Rank lhs, Rank rhs) {
        return RankValue(lhs) < RankValue(rhs);
    });
}

PatternResult Invalid(std::string reason) {
    return PatternResult{HandPattern{}, std::move(reason)};
}

PatternResult Valid(PatternType type, Rank rank, int count, bool lastShort = false, int groupCount = 0) {
    return PatternResult{HandPattern{type, rank, count, groupCount, lastShort}, {}};
}

std::optional<PatternResult> TryIdentifyPlane(
    const std::map<Rank, int>& counts,
    int total,
    int handSizeBeforePlay,
    bool allowShortFinal) {
    std::vector<Rank> tripleRanks;
    for (const auto& [rank, count] : counts) {
        if (rank != Rank::Two && count >= 3) {
            tripleRanks.push_back(rank);
        }
    }
    if (tripleRanks.size() < 2) {
        return std::nullopt;
    }

    std::optional<PatternResult> best;
    int bestScore = std::numeric_limits<int>::min();
    for (std::size_t start = 0; start < tripleRanks.size(); ++start) {
        for (std::size_t end = start + 1; end < tripleRanks.size(); ++end) {
            std::vector<Rank> run(tripleRanks.begin() + static_cast<std::ptrdiff_t>(start), tripleRanks.begin() + static_cast<std::ptrdiff_t>(end + 1));
            if (!IsConsecutive(run)) {
                break;
            }

            const int groupCount = static_cast<int>(run.size());
            const int kickerCount = total - groupCount * 3;
            if (kickerCount < 0 || kickerCount > groupCount * 2) {
                continue;
            }
            const bool lastShort = kickerCount < groupCount;
            if (lastShort && !(allowShortFinal && handSizeBeforePlay == total)) {
                continue;
            }

            const int score = groupCount * 100 + RankValue(MaxRank(run));
            if (score > bestScore) {
                bestScore = score;
                best = Valid(PatternType::Plane, MaxRank(run), total, lastShort, groupCount);
            }
        }
    }
    return best;
}

} // namespace

PatternResult IdentifyPattern(const Cards& cards, int handSizeBeforePlay, bool allowShortFinal) {
    if (cards.empty()) {
        return Invalid("没有选择牌");
    }

    const auto counts = CountRanks(cards);
    const int total = static_cast<int>(cards.size());

    if (total == 1) {
        return Valid(PatternType::Single, cards.front().rank, total);
    }

    if (total == 2 && counts.size() == 1) {
        return Valid(PatternType::Pair, cards.front().rank, total);
    }

    if (total == 3 && counts.size() == 1) {
        if (allowShortFinal && handSizeBeforePlay == 3) {
            return Valid(PatternType::TripleWithOne, cards.front().rank, total, true);
        }
        return Invalid("三张主体只能三带二，最后一手牌不足时除外");
    }

    if (total == 4) {
        if (counts.size() == 1) {
            const Rank rank = cards.front().rank;
            if (rank == Rank::Ace || rank == Rank::Two) {
                return Invalid("没有 A 或 2 炸弹");
            }
            return Valid(PatternType::Bomb, rank, total);
        }

        for (const auto& [rank, count] : counts) {
            if (count == 3) {
                if (allowShortFinal && handSizeBeforePlay == total) {
                    return Valid(PatternType::TripleWithOne, rank, total, true);
                }
                return Invalid("三张主体只能三带二，最后一手牌不足时除外");
            }
        }
    }

    if (total == 5) {
        for (const auto& [rank, count] : counts) {
            if (count == 3) {
                return Valid(PatternType::TripleWithPair, rank, total);
            }
        }
    }

    if (const auto plane = TryIdentifyPlane(counts, total, handSizeBeforePlay, allowShortFinal)) {
        return *plane;
    }

    if (total >= 5 && counts.size() == cards.size()) {
        std::vector<Rank> ranks;
        ranks.reserve(counts.size());
        for (const auto& [rank, count] : counts) {
            (void)count;
            ranks.push_back(rank);
        }
        if (IsConsecutive(ranks)) {
            return Valid(PatternType::Straight, MaxRank(ranks), total);
        }
    }

    if (total >= 4 && total % 2 == 0) {
        std::vector<Rank> pairRanks;
        for (const auto& [rank, count] : counts) {
            if (count != 2) {
                pairRanks.clear();
                break;
            }
            pairRanks.push_back(rank);
        }
        if (pairRanks.size() >= 2 && IsConsecutive(pairRanks)) {
            return Valid(PatternType::ConsecutivePairs, MaxRank(pairRanks), total);
        }
    }

    return Invalid("牌型不符合当前固定跑得快规则");
}

std::string PatternName(PatternType type) {
    switch (type) {
    case PatternType::Invalid: return "无效";
    case PatternType::Single: return "单张";
    case PatternType::Pair: return "对子";
    case PatternType::Straight: return "顺子";
    case PatternType::ConsecutivePairs: return "连对";
    case PatternType::TripleWithOne: return "三带一";
    case PatternType::TripleWithPair: return "三带二";
    case PatternType::Plane: return "飞机";
    case PatternType::Bomb: return "炸弹";
    }
    return "未知";
}

std::string PatternDescription(const HandPattern& pattern) {
    if (!pattern.IsValid()) {
        return PatternName(PatternType::Invalid);
    }
    std::string out = PatternName(pattern.type);
    out += ' ';
    out += RankName(pattern.mainRank);
    if (pattern.lastHandShort) {
        out += " (最后一手不足带牌)";
    }
    return out;
}

bool SameComparisonClass(const HandPattern& lhs, const HandPattern& rhs) {
    if (lhs.type != rhs.type) {
        return false;
    }
    if (lhs.type == PatternType::Plane) {
        return lhs.groupCount == rhs.groupCount;
    }
    if (lhs.type == PatternType::Straight || lhs.type == PatternType::ConsecutivePairs) {
        return lhs.cardCount == rhs.cardCount;
    }
    return true;
}

} // namespace pdk::rules
