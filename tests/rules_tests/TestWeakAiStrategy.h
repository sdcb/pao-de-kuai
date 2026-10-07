#pragma once

#include "game/AiStrategy.h"

#include <algorithm>
#include <cstdint>

namespace pdk::tests {

class TestWeakAiStrategy final : public game::AiStrategy {
public:
    game::AiMoveChoice ChooseMove(const rules::Cards& hand, const game::AiContext& context) override {
        const int n = static_cast<int>(hand.size());
        if (n <= 0 || n > 20) {
            return PassChoice(context);
        }

        std::vector<game::AiMoveChoice> choices;
        const std::uint64_t limit = 1ull << n;
        for (std::uint64_t mask = 1; mask < limit; ++mask) {
            rules::Cards cards;
            cards.reserve(static_cast<std::size_t>(n));
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

            choices.push_back(game::AiMoveChoice{
                false,
                cards,
                validation.pattern,
                "测试弱 AI 选择最低合法牌",
                0
            });
        }

        if (choices.empty()) {
            return PassChoice(context);
        }

        std::sort(choices.begin(), choices.end(), [](const game::AiMoveChoice& lhs, const game::AiMoveChoice& rhs) {
            const bool lhsBomb = lhs.pattern.type == PATTERN_BOMB;
            const bool rhsBomb = rhs.pattern.type == PATTERN_BOMB;
            if (lhsBomb != rhsBomb) {
                return !lhsBomb;
            }
            if (lhs.cards.size() != rhs.cards.size()) {
                return lhs.cards.size() < rhs.cards.size();
            }
            if (PatternOrder(lhs.pattern.type) != PatternOrder(rhs.pattern.type)) {
                return PatternOrder(lhs.pattern.type) < PatternOrder(rhs.pattern.type);
            }
            if (lhs.pattern.mainRank != rhs.pattern.mainRank) {
                return rules::RankValue(lhs.pattern.mainRank) < rules::RankValue(rhs.pattern.mainRank);
            }
            return lhs.pattern.groupCount < rhs.pattern.groupCount;
        });

        return choices.front();
    }

private:
    static int PatternOrder(rules::PatternType type) {
        switch (type) {
        case PATTERN_SINGLE: return 0;
        case PATTERN_PAIR: return 1;
        case PATTERN_TRIPLE_WITH_ONE: return 2;
        case PATTERN_TRIPLE_WITH_PAIR: return 3;
        case PATTERN_CONSECUTIVE_PAIRS: return 4;
        case PATTERN_STRAIGHT: return 5;
        case PATTERN_PLANE: return 6;
        case PATTERN_BOMB: return 7;
        case PATTERN_INVALID: return 8;
        }
        return 8;
    }

    static game::AiMoveChoice PassChoice(const game::AiContext& context) {
        return game::AiMoveChoice{true, {}, {}, context.leading ? "测试弱 AI 没有可出的牌型" : "测试弱 AI 压牌失败"};
    }
};

} // namespace pdk::tests
