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

static void SetReason(PatternResult *result, const char *reason)
{
    if (reason == NULL) {
        result->reason[0] = '\0';
        return;
    }
    strncpy(result->reason, reason, PATTERN_REASON_CAP - 1);
    result->reason[PATTERN_REASON_CAP - 1] = '\0';
}

static PatternResult Invalid(const char *reason)
{
    PatternResult result;

    memset(&result, 0, sizeof(result));
    result.pattern.type = PATTERN_INVALID;
    SetReason(&result, reason);
    return result;
}

static PatternResult Valid(PatternType type, Rank rank, int count, bool lastShort, int groupCount)
{
    PatternResult result;

    memset(&result, 0, sizeof(result));
    result.pattern.type = type;
    result.pattern.mainRank = rank;
    result.pattern.cardCount = count;
    result.pattern.groupCount = groupCount;
    result.pattern.lastHandShort = lastShort;
    return result;
}

bool HandPattern_IsValid(const HandPattern *pattern)
{
    return pattern->type != PATTERN_INVALID;
}

/* Tries every consecutive run of at least two triple ranks and keeps the one with
 * the highest (groupCount, max rank) score, matching the original. */
static bool TryIdentifyPlane(const RankCounts *counts, int total,
                             int handSizeBeforePlay, bool allowShortFinal,
                             PatternResult *out)
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
                *out = Valid(PATTERN_PLANE, MaxRank(&tripleRanks[start], runCount),
                             total, lastShort, groupCount);
            }
        }
    }
    return bestScore != PDK_SCORE_MIN;
}

PatternResult IdentifyPattern(const Cards *cards, int handSizeBeforePlay, bool allowShortFinal)
{
    RankCounts counts;
    int total;

    if (cards->count == 0) {
        return Invalid("没有选择牌");
    }

    CountRanks(cards, &counts);
    total = cards->count;

    if (total == 1) {
        return Valid(PATTERN_SINGLE, cards->items[0].rank, total, false, 0);
    }

    if (total == 2 && counts.distinct == 1) {
        return Valid(PATTERN_PAIR, cards->items[0].rank, total, false, 0);
    }

    if (total == 3 && counts.distinct == 1) {
        if (allowShortFinal && handSizeBeforePlay == 3) {
            return Valid(PATTERN_TRIPLE_WITH_ONE, cards->items[0].rank, total, true, 0);
        }
        return Invalid("三张主体只能三带二，最后一手牌不足时除外");
    }

    if (total == 4) {
        if (counts.distinct == 1) {
            const Rank rank = cards->items[0].rank;
            if (rank == RANK_ACE || rank == RANK_TWO) {
                return Invalid("没有 A 或 2 炸弹");
            }
            return Valid(PATTERN_BOMB, rank, total, false, 0);
        }

        for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
            if (counts.count[rank] == 3) {
                if (allowShortFinal && handSizeBeforePlay == total) {
                    return Valid(PATTERN_TRIPLE_WITH_ONE, (Rank)rank, total, true, 0);
                }
                return Invalid("三张主体只能三带二，最后一手牌不足时除外");
            }
        }
    }

    if (total == 5) {
        for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
            if (counts.count[rank] == 3) {
                return Valid(PATTERN_TRIPLE_WITH_PAIR, (Rank)rank, total, false, 0);
            }
        }
    }

    {
        PatternResult plane;
        if (TryIdentifyPlane(&counts, total, handSizeBeforePlay, allowShortFinal, &plane)) {
            return plane;
        }
    }

    if (total >= 5 && counts.distinct == cards->count) {
        Rank ranks[RANK_TWO + 1];
        int rankCount = 0;
        for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
            if (counts.count[rank] > 0) {
                ranks[rankCount++] = (Rank)rank;
            }
        }
        if (IsConsecutive(ranks, rankCount)) {
            return Valid(PATTERN_STRAIGHT, MaxRank(ranks, rankCount), total, false, 0);
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
            return Valid(PATTERN_CONSECUTIVE_PAIRS, MaxRank(pairRanks, pairCount), total, false, 0);
        }
    }

    return Invalid("牌型不符合当前固定跑得快规则");
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
