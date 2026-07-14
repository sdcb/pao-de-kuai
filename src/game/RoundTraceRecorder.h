#pragma once

#include "game/Player.h"
#include "game/TurnRecord.h"
#include "stats/DailyStat.h"

#include <array>
#include <string>
#include <vector>

namespace pdk::game {

struct RoundTrace {
    unsigned seed{0};
    std::string playerName;
    std::string startedAt;
    rules::PlayerId roundLeader{rules::PlayerId::Player};
    std::array<PlayerState, 3> initialPlayers;
    std::vector<TurnRecord> turns;
    stats::RoundRecord result;
};

class RoundTraceRecorder {
public:
    explicit RoundTraceRecorder(std::string root = {});

    bool WriteRound(const RoundTrace& trace, std::string* writtenPath = nullptr) const;

private:
    std::string root_;
};

} // namespace pdk::game
