#include "rules/HandPattern.h"

#include <string.h>

/*
 * Ported from the std::map<Rank,int> version without changing any rule.  The map
 * is replaced by a rank-indexed count array (3..15), and every table is walked in
 * ascending rank order, which is exactly the order std::map iterated in.
 */

/* Integer limits without <limits.h> noise; the plane search only needs a value
 * below every real score. */
#define PDK_SCORE_MIN (-2147483647 - 1)

typedef struct RankCounts {
    int count[RANK_TWO + 1]; /* index by rank value; 0..2 are unused */
    int distinct;
} RankCounts;

static void CountRanks(const Cards *cards, RankCounts *out)
{
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < cards->count; ++i) {
        const Rank rank = cards->items[i].rank;
        if (rank > RANK_TWO) {
            continue;
        }
        if (out->count[rank] == 0) {
            ++out->distinct;
        }
        ++out->count[rank];
    }
}

static bool IsConsecutive(const Rank *ranks, int count)
{
    if (count <= 0) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        if (ranks[i] == RANK_TWO) {
            return false;
        }
    }
    for (int i = 1; i < count; ++i) {
        if (RankValue(ranks[i]) != RankValue(ranks[i - 1]) + 1) {
            return false;
        }
    }
    return true;
}

static Rank MaxRank(const Rank *ranks, int count)
{
    Rank best = ranks[0];
    for (int i = 1; i < count; ++i) {
        if (RankValue(ranks[i]) > RankValue(best)) {
            best = ranks[i];
        }
    }
    return best;
}

/*
 * The identification core reports a reason as a pointer to a string literal; only the
 * public IdentifyPattern copies it into PatternResult.  The AI searches identify tens
 * of thousands of subsets per decision and almost all of them are invalid, so building
 * and zero-padding a 128-byte reason for each one was a real share of their cost.
 */
static bool Invalid(const char *reason, const char **outReason)
{
    *outReason = reason;
    return false;
}

static bool Valid(PatternType type, Rank rank, int count, bool lastShort, int groupCount,
                  HandPattern *out)
{
    memset(out, 0, sizeof(*out));
    out->type = type;
    out->mainRank = rank;
    out->cardCount = count;
    out->groupCount = groupCount;
    out->lastHandShort = lastShort;
    return true;
}

bool HandPattern_IsValid(const HandPattern *pattern)
{
    return pattern->type != PATTERN_INVALID;
}

/* Tries every consecutive run of at least two triple ranks and keeps the one with
 * the highest (groupCount, max rank) score, matching the original. */
static bool TryIdentifyPlane(const RankCounts *counts, int total,
                             int handSizeBeforePlay, bool allowShortFinal,
                             HandPattern *out)
{
    Rank tripleRanks[RANK_TWO + 1];
    int tripleCount = 0;
    int bestScore = PDK_SCORE_MIN;

    for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
        if (rank != RANK_TWO && counts->count[rank] >= 3) {
            tripleRanks[tripleCount++] = (Rank)rank;
        }
    }
    if (tripleCount < 2) {
        return false;
    }

    for (int start = 0; start < tripleCount; ++start) {
        for (int end = start + 1; end < tripleCount; ++end) {
            const int runCount = end - start + 1;
            int groupCount;
            int kickerCount;
            bool lastShort;
            int score;

            if (!IsConsecutive(&tripleRanks[start], runCount)) {
                break;
            }
            groupCount = runCount;
            kickerCount = total - groupCount * 3;
            if (kickerCount < 0 || kickerCount > groupCount * 2) {
                continue;
            }
            lastShort = kickerCount < groupCount;
            if (lastShort && !(allowShortFinal && handSizeBeforePlay == total)) {
                continue;
            }
            score = groupCount * 100 + RankValue(MaxRank(&tripleRanks[start], runCount));
            if (score > bestScore) {
                bestScore = score;
                Valid(PATTERN_PLANE, MaxRank(&tripleRanks[start], runCount),
                      total, lastShort, groupCount, out);
            }
        }
    }
    return bestScore != PDK_SCORE_MIN;
}

/* The rank of a selection that holds a single rank (distinct == 1). */
static Rank OnlyRank(const RankCounts *counts)
{
    for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
        if (counts->count[rank] > 0) {
            return (Rank)rank;
        }
    }
    return RANK_THREE;
}

/*
 * The rules work on rank counts only (suits never matter), so this is the whole of
 * pattern identification; Identify and IdentifyPatternCounts are thin front ends.
 */
static bool IdentifyCounts(const RankCounts *countsIn, int total, int handSizeBeforePlay,
                           bool allowShortFinal, HandPattern *out, const char **reason)
{
    const RankCounts counts = *countsIn;

    if (total == 0) {
        return Invalid("没有选择牌", reason);
    }

    if (total == 1) {
        return Valid(PATTERN_SINGLE, OnlyRank(&counts), total, false, 0, out);
    }

    if (total == 2 && counts.distinct == 1) {
        return Valid(PATTERN_PAIR, OnlyRank(&counts), total, false, 0, out);
    }

    if (total == 3 && counts.distinct == 1) {
        if (allowShortFinal && handSizeBeforePlay == 3) {
            return Valid(PATTERN_TRIPLE_WITH_ONE, OnlyRank(&counts), total, true, 0, out);
        }
        return Invalid("三张主体只能三带二，最后一手牌不足时除外", reason);
    }

    if (total == 4) {
        if (counts.distinct == 1) {
            const Rank rank = OnlyRank(&counts);
            if (rank == RANK_ACE || rank == RANK_TWO) {
                return Invalid("没有 A 或 2 炸弹", reason);
            }
            return Valid(PATTERN_BOMB, rank, total, false, 0, out);
        }

        for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
            if (counts.count[rank] == 3) {
                if (allowShortFinal && handSizeBeforePlay == total) {
                    return Valid(PATTERN_TRIPLE_WITH_ONE, (Rank)rank, total, true, 0, out);
                }
                return Invalid("三张主体只能三带二，最后一手牌不足时除外", reason);
            }
        }
    }

    if (total == 5) {
        for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
            if (counts.count[rank] == 3) {
                return Valid(PATTERN_TRIPLE_WITH_PAIR, (Rank)rank, total, false, 0, out);
            }
        }
    }

    if (TryIdentifyPlane(&counts, total, handSizeBeforePlay, allowShortFinal, out)) {
        return true;
    }

    if (total >= 5 && counts.distinct == total) {
        Rank ranks[RANK_TWO + 1];
        int rankCount = 0;
        for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
            if (counts.count[rank] > 0) {
                ranks[rankCount++] = (Rank)rank;
            }
        }
        if (IsConsecutive(ranks, rankCount)) {
            return Valid(PATTERN_STRAIGHT, MaxRank(ranks, rankCount), total, false, 0, out);
        }
    }

    if (total >= 4 && total % 2 == 0) {
        Rank pairRanks[RANK_TWO + 1];
        int pairCount = 0;
        bool allPairs = true;
        for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
            if (counts.count[rank] == 0) {
                continue;
            }
            if (counts.count[rank] != 2) {
                allPairs = false;
                break;
            }
            pairRanks[pairCount++] = (Rank)rank;
        }
        if (allPairs && pairCount >= 2 && IsConsecutive(pairRanks, pairCount)) {
            return Valid(PATTERN_CONSECUTIVE_PAIRS, MaxRank(pairRanks, pairCount), total, false, 0, out);
        }
    }

    return Invalid("牌型不符合当前固定跑得快规则", reason);
}

static bool Identify(const Cards *cards, int handSizeBeforePlay, bool allowShortFinal,
                     HandPattern *out, const char **reason)
{
    RankCounts counts;

    CountRanks(cards, &counts);
    return IdentifyCounts(&counts, cards->count, handSizeBeforePlay, allowShortFinal, out, reason);
}

bool IdentifyPatternCounts(const uint8_t count[RANK_TWO + 1], int handSizeBeforePlay,
                           bool allowShortFinal, HandPattern *out)
{
    RankCounts counts;
    const char *reason = NULL;
    int total = 0;

    memset(&counts, 0, sizeof(counts));
    for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
        counts.count[rank] = count[rank];
        total += count[rank];
        if (count[rank] > 0) {
            ++counts.distinct;
        }
    }
    if (!IdentifyCounts(&counts, total, handSizeBeforePlay, allowShortFinal, out, &reason)) {
        memset(out, 0, sizeof(*out));
        out->type = PATTERN_INVALID;
        return false;
    }
    return true;
}

PatternResult IdentifyPattern(const Cards *cards, int handSizeBeforePlay, bool allowShortFinal)
{
    PatternResult result;
    const char *reason = NULL;

    memset(&result, 0, sizeof(result));
    if (!Identify(cards, handSizeBeforePlay, allowShortFinal, &result.pattern, &reason)) {
        /* Same bytes as before: the invalid pattern is all zero and the reason is
         * NUL-padded, which the memset already provides. */
        memset(&result.pattern, 0, sizeof(result.pattern));
        result.pattern.type = PATTERN_INVALID;
        for (int i = 0; reason != NULL && reason[i] != '\0' && i + 1 < PATTERN_REASON_CAP; ++i) {
            result.reason[i] = reason[i];
        }
    }
    return result;
}

bool IdentifyPatternOnly(const Cards *cards, int handSizeBeforePlay, bool allowShortFinal,
                         HandPattern *out, const char **reason)
{
    const char *why = NULL;

    if (!Identify(cards, handSizeBeforePlay, allowShortFinal, out, &why)) {
        memset(out, 0, sizeof(*out));
        out->type = PATTERN_INVALID;
        if (reason != NULL) {
            *reason = why;
        }
        return false;
    }
    if (reason != NULL) {
        *reason = NULL;
    }
    return true;
}

const char *PatternName(PatternType type)
{
    switch (type) {
    case PATTERN_INVALID: return "无效";
    case PATTERN_SINGLE: return "单张";
    case PATTERN_PAIR: return "对子";
    case PATTERN_STRAIGHT: return "顺子";
    case PATTERN_CONSECUTIVE_PAIRS: return "连对";
    case PATTERN_TRIPLE_WITH_ONE: return "三带一";
    case PATTERN_TRIPLE_WITH_PAIR: return "三带二";
    case PATTERN_PLANE: return "飞机";
    case PATTERN_BOMB: return "炸弹";
    }
    return "未知";
}

void PatternDescription(const HandPattern *pattern, char *out, int cap)
{
    int used = 0;

    if (out == NULL || cap <= 0) {
        return;
    }
    out[0] = '\0';
    if (!HandPattern_IsValid(pattern)) {
        strncpy(out, PatternName(PATTERN_INVALID), (size_t)cap - 1);
        out[cap - 1] = '\0';
        return;
    }
    /* Pattern names and rank names are short ASCII literals, so a plain copy loop
     * keeps the buffer terminated without pulling in a formatter. */
    for (const char *p = PatternName(pattern->type); *p != '\0' && used + 1 < cap; ++p) {
        out[used++] = *p;
    }
    if (used + 1 < cap) {
        out[used++] = ' ';
    }
    for (const char *p = RankName(pattern->mainRank); *p != '\0' && used + 1 < cap; ++p) {
        out[used++] = *p;
    }
    if (pattern->lastHandShort) {
        static const char suffix[] = " (最后一手不足带牌)";
        for (const char *p = suffix; *p != '\0' && used + 1 < cap; ++p) {
            out[used++] = *p;
        }
    }
    out[used] = '\0';
}

bool SameComparisonClass(const HandPattern *lhs, const HandPattern *rhs)
{
    if (lhs->type != rhs->type) {
        return false;
    }
    if (lhs->type == PATTERN_PLANE) {
        return lhs->groupCount == rhs->groupCount;
    }
    if (lhs->type == PATTERN_STRAIGHT || lhs->type == PATTERN_CONSECUTIVE_PAIRS) {
        return lhs->cardCount == rhs->cardCount;
    }
    return true;
}
