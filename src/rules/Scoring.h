#pragma once

#include <array>
#include <string>
#include <vector>

namespace pdk::rules {

enum class PlayerId {
    Player = 0,
    Ai1 = 1,
    Ai2 = 2
};

struct BombScoreEvent {
    PlayerId by{PlayerId::Player};
    int score{20};
};

struct SpringInfo {
    bool enabled{false};
    std::vector<PlayerId> losers;
};

struct RoundScoreInput {
    PlayerId winner{PlayerId::Player};
    std::array<int, 3> remainingCards{0, 0, 0};
    std::array<bool, 3> hasPlayedCards{false, false, false};
    std::vector<BombScoreEvent> bombs;
};

struct RoundScoreResult {
    std::array<int, 3> scores{0, 0, 0};
    SpringInfo spring;
};

int PlayerIndex(PlayerId player);
PlayerId PlayerFromIndex(int index);
std::string PlayerKey(PlayerId player);
RoundScoreResult CalculateRoundScore(const RoundScoreInput& input);

} // namespace pdk::rules
