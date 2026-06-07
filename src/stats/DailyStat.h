#pragma once

#include "rules/Scoring.h"

#include <array>
#include <string>
#include <vector>

namespace pdk::stats {

struct RoundRecord {
    std::string startedAt;
    std::string endedAt;
    rules::PlayerId winner{rules::PlayerId::Player};
    std::string playerName;
    std::array<int, 3> scores{0, 0, 0};
    std::array<int, 3> remainingCards{0, 0, 0};
    std::vector<rules::BombScoreEvent> bombs;
    rules::SpringInfo spring;
};

struct DailyStat {
    std::string date;
    std::vector<RoundRecord> rounds;
};

struct StatSummary {
    int rounds{0};
    std::array<int, 3> scores{0, 0, 0};
    int bombs{0};
    int springLosers{0};
    int bestSingleRoundPlayerScore{0};
};

} // namespace pdk::stats
