#pragma once

#include "stats/DailyStat.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PDK_STAT_ROOT_CAP = 260 };
enum { PDK_STAT_PATH_CAP = 512 };

/*
 * Statistics reader/writer rooted at a directory.  The old type was a class
 * holding a std::string root; it is a fixed-buffer value type now, created with
 * StatStore_Init (pass NULL or "" to use the process current directory, which is
 * the project's documented convention: paths are relative to the working
 * directory, not the executable).
 */
typedef struct StatStore {
    char root[PDK_STAT_ROOT_CAP];
} StatStore;

void StatStore_Init(StatStore *store, const char *root);

/* `path` is the process working directory, so `root` may be relative. */
bool StatStore_LoadDay(const StatStore *store, const char *date, DailyStat *out);
bool StatStore_SaveDay(const StatStore *store, const DailyStat *day);
bool StatStore_AppendRound(const StatStore *store, const char *date,
                           const RoundRecord *round);
void StatStore_SummarizeDay(const StatStore *store, const char *date, StatSummary *out);
void StatStore_SummarizeMonth(const StatStore *store, const char *yyyymm, StatSummary *out);
void StatStore_SummarizeHistory(const StatStore *store, StatSummary *out);
void StatStore_DayPath(const StatStore *store, const char *date, char *out, int cap);

/* Both format the local system time, matching the old strftime("%Y%m%d") and
 * strftime("%H:%M:%S") results. */
void TodayDateKey(char *out, int cap);
void NowTimeText(char *out, int cap);

#ifdef __cplusplus
}
#endif
