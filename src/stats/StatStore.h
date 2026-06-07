#pragma once

#include "stats/DailyStat.h"

#include <filesystem>
#include <string>

namespace pdk::stats {

std::string TodayDateKey();
std::string NowTimeText();

class StatStore {
public:
    explicit StatStore(std::filesystem::path root = std::filesystem::current_path());

    DailyStat LoadDay(const std::string& date) const;
    bool SaveDay(const DailyStat& day) const;
    bool AppendRound(const std::string& date, const RoundRecord& round) const;
    StatSummary SummarizeDay(const std::string& date) const;
    StatSummary SummarizeMonth(const std::string& yyyymm) const;
    StatSummary SummarizeHistory() const;

    std::filesystem::path DayPath(const std::string& date) const;

private:
    std::filesystem::path root_;
};

} // namespace pdk::stats
