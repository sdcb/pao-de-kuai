#pragma once

#include "stats/DailyStat.h"

#include <string>

namespace pdk::stats {

std::string TodayDateKey();
std::string NowTimeText();

class StatStore {
public:
    explicit StatStore(std::string root = {});

    DailyStat LoadDay(const std::string& date) const;
    bool SaveDay(const DailyStat& day) const;
    bool AppendRound(const std::string& date, const RoundRecord& round) const;
    StatSummary SummarizeDay(const std::string& date) const;
    StatSummary SummarizeMonth(const std::string& yyyymm) const;
    StatSummary SummarizeHistory() const;

    std::string DayPath(const std::string& date) const;

private:
    std::string root_;
};

} // namespace pdk::stats
