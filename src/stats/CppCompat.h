#pragma once

/*
 * TEMPORARY TRANSITION HEADER -- DELETE WHEN THE PORT IS DONE.
 *
 * `src/stats/` is pure C now (plan.md S3): AppSettings and RoundRecord use fixed
 * character buffers, DailyStat owns a growable array behind Init/Free, and
 * StatStore is a value type with free functions.
 *
 * The C++ translation units that are not converted yet expect the old shapes --
 * `settings.playerName` as a std::string, `round.bombs` as a std::vector,
 * `stats::StatStore().SummarizeDay(...)` as a method on a temporary.  This header
 * gives them exactly that inside `namespace pdk::stats` and converts at the C
 * boundary, so no call site has to change twice.
 *
 * It must not grow new features, and it disappears together with the last C++
 * file under src/.
 */

#include "rules/CppCompat.h"

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "stats/AppSettings.h"
#include "stats/DailyStat.h"
#include "stats/StatStore.h"

namespace pdk::stats {

/* Copies a std::string into a fixed C buffer, truncating and always terminating.
 * Deliberately not snprintf: the C side of this project stays off the CRT
 * formatter for size reasons. */
inline void CopyToBuffer(char *dst, int cap, const std::string& src)
{
    const std::size_t limit = static_cast<std::size_t>(cap > 0 ? cap - 1 : 0);
    const std::size_t count = src.size() < limit ? src.size() : limit;
    if (count > 0) {
        std::memcpy(dst, src.data(), count);
    }
    if (cap > 0) {
        dst[count] = '\0';
    }
}

/* ---- value types the C++ side still expects -------------------------- */

struct AppSettings {
    std::string playerName{"\xE6\x9D\x8E\xE5\xA7\x90"};
    float masterVolume{0.8f};
    int windowWidth{1280};
    int windowHeight{720};
    /* Local AI strategy per seat: "basic" or "strong". */
    std::string ai1{"basic"};
    std::string ai2{"basic"};
    bool roundTraceEnabled{false};
};

struct RoundRecord {
    std::string startedAt;
    std::string endedAt;
    rules::PlayerId winner{PLAYER_HUMAN};
    std::string playerName;
    std::array<int, 3> scores{0, 0, 0};
    std::array<int, 3> remainingCards{0, 0, 0};
    std::vector<rules::BombScoreEvent> bombs;
    rules::SpringInfo spring{};
};

struct DailyStat {
    std::string date;
    std::vector<RoundRecord> rounds;
};

/* StatSummary is all scalars, but `scores` stays a std::array so that callers can
 * assign it to another std::array (C arrays cannot be assigned). */
struct StatSummary {
    int rounds{0};
    std::array<int, 3> scores{0, 0, 0};
    int bombs{0};
    int springLosers{0};
    int bestSingleRoundPlayerScore{0};
};

/* ---- conversions ----------------------------------------------------- */

inline ::RoundRecord ToCRound(const RoundRecord& in)
{
    ::RoundRecord out;
    RoundRecord_Init(&out);
    CopyToBuffer(out.startedAt, PDK_TIME_TEXT_CAP, in.startedAt);
    CopyToBuffer(out.endedAt, PDK_TIME_TEXT_CAP, in.endedAt);
    out.winner = in.winner;
    CopyToBuffer(out.playerName, PDK_PLAYER_NAME_CAP, in.playerName);
    out.scores[0] = in.scores[0];
    out.scores[1] = in.scores[1];
    out.scores[2] = in.scores[2];
    out.remainingCards[0] = in.remainingCards[0];
    out.remainingCards[1] = in.remainingCards[1];
    out.remainingCards[2] = in.remainingCards[2];
    for (const rules::BombScoreEvent& bomb : in.bombs) {
        RoundRecord_AddBomb(&out, bomb);
    }
    out.spring = in.spring;
    return out;
}

inline RoundRecord FromCRound(const ::RoundRecord& in)
{
    RoundRecord out;
    out.startedAt = in.startedAt;
    out.endedAt = in.endedAt;
    out.winner = in.winner;
    out.playerName = in.playerName;
    out.scores = {in.scores[0], in.scores[1], in.scores[2]};
    out.remainingCards = {in.remainingCards[0], in.remainingCards[1], in.remainingCards[2]};
    out.bombs.assign(in.bombs, in.bombs + in.bombCount);
    out.spring = in.spring;
    return out;
}

inline ::DailyStat ToCDay(const DailyStat& in)
{
    ::DailyStat out;
    DailyStat_Init(&out);
    CopyToBuffer(out.date, PDK_DATE_KEY_CAP, in.date);
    for (const RoundRecord& round : in.rounds) {
        const ::RoundRecord converted = ToCRound(round);
        DailyStat_Append(&out, &converted);
    }
    return out;
}

inline DailyStat FromCDay(const ::DailyStat& in)
{
    DailyStat out;
    out.date = in.date;
    out.rounds.reserve(static_cast<std::size_t>(in.roundCount));
    for (int i = 0; i < in.roundCount; ++i) {
        out.rounds.push_back(FromCRound(in.rounds[i]));
    }
    return out;
}

inline StatSummary FromCSummary(const ::StatSummary& in)
{
    StatSummary out;
    out.rounds = in.rounds;
    out.scores = {in.scores[0], in.scores[1], in.scores[2]};
    out.bombs = in.bombs;
    out.springLosers = in.springLosers;
    out.bestSingleRoundPlayerScore = in.bestSingleRoundPlayerScore;
    return out;
}

/* ---- settings -------------------------------------------------------- */

inline ::AppSettings ToCSettings(const AppSettings& in)
{
    ::AppSettings out;
    AppSettings_Default(&out);
    CopyToBuffer(out.playerName, PDK_PLAYER_NAME_CAP, in.playerName);
    out.masterVolume = in.masterVolume;
    out.windowWidth = in.windowWidth;
    out.windowHeight = in.windowHeight;
    CopyToBuffer(out.ai1, PDK_AI_NAME_CAP, in.ai1);
    CopyToBuffer(out.ai2, PDK_AI_NAME_CAP, in.ai2);
    out.roundTraceEnabled = in.roundTraceEnabled;
    return out;
}

inline AppSettings FromCSettings(const ::AppSettings& in)
{
    AppSettings out;
    out.playerName = in.playerName;
    out.masterVolume = in.masterVolume;
    out.windowWidth = in.windowWidth;
    out.windowHeight = in.windowHeight;
    out.ai1 = in.ai1;
    out.ai2 = in.ai2;
    out.roundTraceEnabled = in.roundTraceEnabled;
    return out;
}

inline std::string NormalizeAiSelection(const std::string& value)
{
    char out[PDK_AI_NAME_CAP];
    ::NormalizeAiSelection(value.c_str(), out, PDK_AI_NAME_CAP);
    return std::string(out);
}

inline AppSettings LoadAppSettings(const std::string& path = "appsettings.json")
{
    ::AppSettings raw;
    ::LoadAppSettings(path.c_str(), &raw);
    return FromCSettings(raw);
}

inline bool SaveAppSettings(const AppSettings& settings, const std::string& path = "appsettings.json")
{
    const ::AppSettings raw = ToCSettings(settings);
    return ::SaveAppSettings(&raw, path.c_str());
}

/* ---- time keys ------------------------------------------------------- */

inline std::string TodayDateKey()
{
    char out[PDK_DATE_KEY_CAP];
    ::TodayDateKey(out, PDK_DATE_KEY_CAP);
    return std::string(out);
}

inline std::string NowTimeText()
{
    char out[PDK_TIME_TEXT_CAP];
    ::NowTimeText(out, PDK_TIME_TEXT_CAP);
    return std::string(out);
}

/* ---- the store ------------------------------------------------------- */

class StatStore {
public:
    explicit StatStore(std::string root = {}) { ::StatStore_Init(&data_, root.c_str()); }

    DailyStat LoadDay(const std::string& date) const
    {
        ::DailyStat raw;
        ::StatStore_LoadDay(&data_, date.c_str(), &raw);
        DailyStat out = FromCDay(raw);
        DailyStat_Free(&raw);
        return out;
    }

    bool SaveDay(const DailyStat& day) const
    {
        ::DailyStat raw = ToCDay(day);
        const bool ok = ::StatStore_SaveDay(&data_, &raw);
        DailyStat_Free(&raw);
        return ok;
    }

    bool AppendRound(const std::string& date, const RoundRecord& round) const
    {
        const ::RoundRecord raw = ToCRound(round);
        return ::StatStore_AppendRound(&data_, date.c_str(), &raw);
    }

    StatSummary SummarizeDay(const std::string& date) const
    {
        ::StatSummary raw;
        ::StatStore_SummarizeDay(&data_, date.c_str(), &raw);
        return FromCSummary(raw);
    }

    StatSummary SummarizeMonth(const std::string& yyyymm) const
    {
        ::StatSummary raw;
        ::StatStore_SummarizeMonth(&data_, yyyymm.c_str(), &raw);
        return FromCSummary(raw);
    }

    StatSummary SummarizeHistory() const
    {
        ::StatSummary raw;
        ::StatStore_SummarizeHistory(&data_, &raw);
        return FromCSummary(raw);
    }

    std::string DayPath(const std::string& date) const
    {
        char out[PDK_STAT_PATH_CAP];
        ::StatStore_DayPath(&data_, date.c_str(), out, PDK_STAT_PATH_CAP);
        return std::string(out);
    }

private:
    ::StatStore data_;
};

} // namespace pdk::stats
