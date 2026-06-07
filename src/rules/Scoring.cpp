#include "rules/Scoring.h"

#include <algorithm>

namespace pdk::rules {

int PlayerIndex(PlayerId player) {
    return static_cast<int>(player);
}

PlayerId PlayerFromIndex(int index) {
    switch (index) {
    case 0: return PlayerId::Player;
    case 1: return PlayerId::Ai1;
    default: return PlayerId::Ai2;
    }
}

std::string PlayerKey(PlayerId player) {
    switch (player) {
    case PlayerId::Player: return "player";
    case PlayerId::Ai1: return "ai1";
    case PlayerId::Ai2: return "ai2";
    }
    return "unknown";
}

RoundScoreResult CalculateRoundScore(const RoundScoreInput& input) {
    RoundScoreResult result;

    for (const BombScoreEvent& bomb : input.bombs) {
        const int bombPlayer = PlayerIndex(bomb.by);
        result.scores[bombPlayer] += bomb.score;
        for (int i = 0; i < 3; ++i) {
            if (i != bombPlayer) {
                result.scores[i] -= bomb.score / 2;
            }
        }
    }

    const int winner = PlayerIndex(input.winner);
    for (int i = 0; i < 3; ++i) {
        if (i == winner) {
            continue;
        }

        if (!input.hasPlayedCards[i]) {
            result.spring.enabled = true;
            result.spring.losers.push_back(PlayerFromIndex(i));
            result.scores[i] -= 32;
            result.scores[winner] += 32;
            continue;
        }

        const int remaining = input.remainingCards[i] == 1 ? 0 : input.remainingCards[i];
        result.scores[i] -= remaining;
        result.scores[winner] += remaining;
    }

    return result;
}

} // namespace pdk::rules
