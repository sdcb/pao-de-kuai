#pragma once

#include "stats/StatStore.h"

namespace pdk::game {

class RoundRecorder {
public:
    explicit RoundRecorder(stats::StatStore store = stats::StatStore()) : store_(std::move(store)) {}

    bool AppendToday(const stats::RoundRecord& record) {
        return store_.AppendRound(stats::TodayDateKey(), record);
    }

private:
    stats::StatStore store_;
};

} // namespace pdk::game
