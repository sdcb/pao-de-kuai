#include "game/AiStrategy.h"

#include <algorithm>
#include <limits>

namespace pdk::game {
namespace {

struct Candidate {
    rules::Cards cards;
    rules::HandPattern pattern;
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

std::vector<Candidate> GenerateCandidates(const rules::Cards& hand, const AiContext& context) {
    std::vector<Candidate> candidates;
    const int n = static_cast<int>(hand.size());
    if (n == 0 || n > 20) {
        return candidates;
    }

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

        const int leaves = n - static_cast<int>(cards.size());
        int score = PatternBaseScore(validation.pattern.type);
        score += static_cast<int>(cards.size()) * 20;
        score -= rules::RankValue(validation.pattern.mainRank);
        if (leaves == 0) {
            score += 3000;
        } else if (leaves <= 2) {
            score += 250;
        }
        if (!context.leading && validation.pattern.type == rules::PatternType::Bomb &&
            context.previous.type != rules::PatternType::Bomb && leaves > 0) {
            score -= 550;
        }
        if (!context.leading) {
            score -= rules::RankValue(validation.pattern.mainRank) * 2;
        }
        candidates.push_back(Candidate{cards, validation.pattern, score});
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
    return AiMoveChoice{false, chosen.cards, chosen.pattern, "基础 AI 推荐 " + rules::PatternName(chosen.pattern.type)};
}

} // namespace pdk::game
