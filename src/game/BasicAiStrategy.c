#include "game/AiStrategy.h"
#include "game/AiStrategyInternal.h"

#include "core/Str.h"

#include <stdlib.h>

int Ai_PatternBaseScore(PatternType type)
{
    switch (type) {
    case PATTERN_STRAIGHT: return 800;
    case PATTERN_CONSECUTIVE_PAIRS: return 760;
    case PATTERN_PLANE: return 720;
    case PATTERN_TRIPLE_WITH_PAIR: return 650;
    case PATTERN_TRIPLE_WITH_ONE: return 600;
    case PATTERN_PAIR: return 300;
    case PATTERN_SINGLE: return 150;
    case PATTERN_BOMB: return 80;
    case PATTERN_INVALID: return 0;
    default: return 0;
    }
}

static bool IsConsecutive(const Rank *ranks, int rankCount)
{
    if (rankCount <= 0) {
        return false;
    }
    for (int i = 0; i < rankCount; ++i) {
        if (ranks[i] == RANK_TWO) {
            return false;
        }
    }
    for (int i = 1; i < rankCount; ++i) {
        if (RankValue(ranks[i]) != RankValue(ranks[i - 1]) + 1) {
            return false;
        }
    }
    return true;
}

int Ai_BestConsecutiveRunScore(const Rank *ranks, int rankCount, int minLength, int perRankScore)
{
    int best = 0;

    if (rankCount < minLength) {
        return 0;
    }
    for (int start = 0; start < rankCount; ++start) {
        for (int end = start; end < rankCount; ++end) {
            const int length = end - start + 1;
            int candidate;

            if (!IsConsecutive(ranks + start, length)) {
                break;
            }
            if (length >= minLength) {
                candidate = length * perRankScore + (int)RankValue(ranks[end]) * 4;
                if (candidate > best) {
                    best = candidate;
                }
            }
        }
    }
    return best;
}

void Ai_CoreUsage(const Cards *cards, const HandPattern *pattern, AiRankCounts *out)
{
    AiRankCounts playedCounts;

    AiRankCounts_Build(cards, &playedCounts);
    AiRankCounts_Clear(out);

    switch (pattern->type) {
    case PATTERN_TRIPLE_WITH_ONE:
    case PATTERN_TRIPLE_WITH_PAIR:
        out->count[pattern->mainRank] = 3;
        break;
    case PATTERN_PLANE: {
        AiRankList tripleRanks;
        bool found = false;

        AiRankList_Clear(&tripleRanks);
        for (Rank rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
            if (rank != RANK_TWO && playedCounts.count[rank] >= 3) {
                AiRankList_Push(&tripleRanks, rank);
            }
        }
        for (int start = 0; start < tripleRanks.count && !found; ++start) {
            for (int end = start; end < tripleRanks.count; ++end) {
                const int length = end - start + 1;

                if (length > pattern->groupCount) {
                    break;
                }
                if (!IsConsecutive(tripleRanks.items + start, length)) {
                    break;
                }
                if (length == pattern->groupCount &&
                    tripleRanks.items[end] == pattern->mainRank) {
                    for (int i = start; i <= end; ++i) {
                        out->count[tripleRanks.items[i]] = 3;
                    }
                    found = true;
                    break;
                }
            }
        }
        break;
    }
    case PATTERN_INVALID:
        break;
    case PATTERN_SINGLE:
    case PATTERN_PAIR:
    case PATTERN_STRAIGHT:
    case PATTERN_CONSECUTIVE_PAIRS:
    case PATTERN_BOMB:
        *out = playedCounts;
        break;
    default:
        break;
    }
}

int Ai_GroupDisruptionPenalty(const AiRankCounts *beforeCounts, const Cards *played,
                              const HandPattern *pattern)
{
    AiRankCounts playedCounts;
    AiRankCounts coreUsage;
    int penalty = 0;

    AiRankCounts_Build(played, &playedCounts);
    Ai_CoreUsage(played, pattern, &coreUsage);

    /* Only ranks the hand already held can lose structure, and the old std::map loop
     * visited exactly those in ascending rank order. */
    for (Rank rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
        const int beforeCount = beforeCounts->count[rank];
        const int used = playedCounts.count[rank];
        int kickerUsed;

        if (beforeCount == 0 || used == 0) {
            continue;
        }
        kickerUsed = used - coreUsage.count[rank];
        if (kickerUsed < 0) {
            kickerUsed = 0;
        }
        if (kickerUsed > 0) {
            if (beforeCount >= 4) {
                penalty += 900;
            } else if (beforeCount == 3) {
                penalty += 620;
            } else if (beforeCount == 2) {
                penalty += 360;
            }
            penalty += (int)RankValue(rank) * 8;
        }

        if (beforeCount >= 4 && used > 0 && used < 4) {
            penalty += 900;
        } else if (beforeCount == 3 && used > 0 && used < 3) {
            penalty += 520;
        } else if (beforeCount == 2 && used == 1) {
            penalty += 320;
        }
    }
    return penalty;
}

int Ai_KickerControlPenalty(const AiCandidate *candidate)
{
    AiRankCounts playedCounts;
    AiRankCounts coreUsage;
    int penalty = 0;

    if (candidate->remainder.count == 0) {
        return 0;
    }
    if (candidate->pattern.type != PATTERN_TRIPLE_WITH_ONE &&
        candidate->pattern.type != PATTERN_TRIPLE_WITH_PAIR &&
        candidate->pattern.type != PATTERN_PLANE) {
        return 0;
    }

    AiRankCounts_Build(&candidate->cards, &playedCounts);
    Ai_CoreUsage(&candidate->cards, &candidate->pattern, &coreUsage);
    for (Rank rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
        const int used = playedCounts.count[rank];
        int kickerUsed;

        if (used == 0) {
            continue;
        }
        kickerUsed = used - coreUsage.count[rank];
        if (kickerUsed <= 0) {
            continue;
        }
        if (rank == RANK_TWO) {
            penalty += 1800 * kickerUsed;
        } else if (rank == RANK_ACE) {
            penalty += 1300 * kickerUsed;
        } else if (rank == RANK_KING) {
            penalty += 850 * kickerUsed;
        } else if (rank == RANK_QUEEN) {
            penalty += 360 * kickerUsed;
        }
    }
    return penalty;
}

int Ai_EvaluateRemainingHand(const Cards *cards)
{
    AiRankCounts counts;
    AiRankList straightRanks;
    AiRankList pairRanks;
    AiRankList tripleRanks;
    int score;
    int singleCount = 0;

    if (cards->count == 0) {
        return 6000;
    }

    AiRankCounts_Build(cards, &counts);
    AiRankList_Clear(&straightRanks);
    AiRankList_Clear(&pairRanks);
    AiRankList_Clear(&tripleRanks);
    score = -cards->count * 10;

    for (Rank rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
        const int count = counts.count[rank];

        if (count == 0) {
            continue;
        }
        if (count == 1) {
            singleCount++;
            score -= 130 - (int)RankValue(rank) * 4;
        } else if (count == 2) {
            score += 220 + (int)RankValue(rank) * 5;
        } else if (count == 3) {
            score += 430 + (int)RankValue(rank) * 7;
        } else if (count >= 4) {
            score += 900 + (int)RankValue(rank) * 10;
        }

        if (rank != RANK_TWO) {
            AiRankList_Push(&straightRanks, rank);
        }
        if (count >= 2 && rank != RANK_TWO) {
            AiRankList_Push(&pairRanks, rank);
        }
        if (count >= 3 && rank != RANK_TWO) {
            AiRankList_Push(&tripleRanks, rank);
        }
    }

    score -= singleCount * singleCount * 35;
    score += Ai_BestConsecutiveRunScore(straightRanks.items, straightRanks.count, 5, 95);
    score += Ai_BestConsecutiveRunScore(pairRanks.items, pairRanks.count, 2, 115);
    score += Ai_BestConsecutiveRunScore(tripleRanks.items, tripleRanks.count, 2, 190);
    if (cards->count <= 2) {
        score += 320;
    }
    return score;
}

static int TotalCardsOfRank(Rank rank)
{
    if (rank == RANK_TWO) {
        return 1;
    }
    if (rank == RANK_ACE) {
        return 3;
    }
    return 4;
}

static int CountRankIn(const Cards *cards, Rank rank)
{
    int count = 0;

    for (int i = 0; i < cards->count; ++i) {
        if (cards->items[i].rank == rank) {
            count++;
        }
    }
    return count;
}

int Ai_UnknownRankCount(const AiCandidate *candidate, const AiContext *context, Rank rank)
{
    const int known = CountRankIn(&context->playedCards, rank) +
                      CountRankIn(&candidate->cards, rank) +
                      CountRankIn(&candidate->remainder, rank);
    const int unknown = TotalCardsOfRank(rank) - known;

    return unknown > 0 ? unknown : 0;
}

int Ai_UnknownHigherControlCount(const AiCandidate *candidate, const AiContext *context,
                                 Rank rank)
{
    int count = 0;

    if (rank < RANK_KING) {
        count += Ai_UnknownRankCount(candidate, context, RANK_KING);
    }
    if (rank < RANK_ACE) {
        count += Ai_UnknownRankCount(candidate, context, RANK_ACE);
    }
    if (rank < RANK_TWO) {
        count += Ai_UnknownRankCount(candidate, context, RANK_TWO);
    }
    return count;
}

static int ProvenSingleControlBonus(const AiCandidate *candidate, const AiContext *context)
{
    int best = 0;
    int candidateRank;

    if (candidate->pattern.type != PATTERN_SINGLE ||
        candidate->pattern.mainRank >= RANK_ACE) {
        return 0;
    }

    candidateRank = (int)RankValue(candidate->pattern.mainRank);
    for (int i = 0; i < PDK_AI_SEATS; ++i) {
        const OptionalPassObservation *observation;
        int failedRank;

        if (i == context->currentPlayerIndex || context->remainingCards[i] <= 0) {
            continue;
        }
        observation = &context->passObservations[i];
        if (!observation->has || observation->value.pattern.type != PATTERN_SINGLE) {
            continue;
        }

        failedRank = (int)RankValue(observation->value.pattern.mainRank);
        if (candidateRank >= failedRank) {
            /* Consume only same-pattern information: failing to beat Q single proves
             * Q/K below A can be useful leads, but says nothing about pairs, straights,
             * or wing choices. */
            const int bonus = 760 - (candidateRank - failedRank) * 35;

            if (bonus > best) {
                best = bonus;
            }
        }
    }
    return best;
}


static void AppendMasksWithCardCount(int n, int count, AiMaskList *masks)
{
    uint64_t mask;
    uint64_t limit;

    if (count <= 0 || count > n || n >= 63) {
        return;
    }

    mask = (1ull << count) - 1ull;
    limit = 1ull << n;
    while (mask < limit) {
        const uint64_t smallest = mask & (~mask + 1ull);
        const uint64_t ripple = mask + smallest;

        AiMaskList_Push(masks, mask);
        if (ripple == 0) {
            break;
        }
        mask = (((mask ^ ripple) >> 2) / smallest) | ripple;
    }
}

static int CompareMasks(const void *lhs, const void *rhs)
{
    const uint64_t left = *(const uint64_t *)lhs;
    const uint64_t right = *(const uint64_t *)rhs;

    if (left < right) {
        return -1;
    }
    return left > right ? 1 : 0;
}

static void FollowCandidateMasks(int n, const HandPattern *previous, AiMaskList *out)
{
    /* At most the 4-card bomb set plus the plane's 3 or 4 distinct sizes. */
    int counts[8];
    int countCount = 1;

    counts[0] = 4;
    if (previous->type == PATTERN_PLANE) {
        const int minCards = previous->groupCount * 4;
        const int maxCards = previous->groupCount * 5;

        for (int count = minCards; count <= maxCards && countCount < 8; ++count) {
            bool seen = false;

            for (int i = 0; i < countCount; ++i) {
                if (counts[i] == count) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                counts[countCount++] = count;
            }
        }
    } else {
        bool seen = false;

        for (int i = 0; i < countCount; ++i) {
            if (counts[i] == previous->cardCount) {
                seen = true;
                break;
            }
        }
        if (!seen && countCount < 8) {
            counts[countCount++] = previous->cardCount;
        }
    }

    for (int i = 0; i < countCount; ++i) {
        AppendMasksWithCardCount(n, counts[i], out);
    }
    /* Every mask is distinct, so the order is total and qsort cannot introduce any
     * toolchain-dependent tie ordering. */
    if (out->count > 1) {
        qsort(out->items, (size_t)out->count, sizeof(uint64_t), CompareMasks);
    }
}

int Ai_TacticalAdjustment(const AiCandidate *candidate, const AiContext *context)
{
    const int leaves = candidate->remainder.count;
    const bool anyOpponentSingle = context->minOpponentRemainingCards == 1;
    const bool urgentDefense = context->nextPlayerRemainingCards == 1 ||
        (context->minOpponentRemainingCards > 0 && context->minOpponentRemainingCards <= 2);
    int score = 0;

    if (context->leading && anyOpponentSingle) {
        if (candidate->pattern.type == PATTERN_SINGLE) {
            score -= 900;
            score += (int)RankValue(candidate->pattern.mainRank) * 85;
            if (candidate->pattern.mainRank >= RANK_KING) {
                score += 360;
            }
        } else {
            score += 420 + candidate->pattern.cardCount * 25;
        }
    }
    if (!context->leading && anyOpponentSingle &&
        context->previous.type == PATTERN_SINGLE &&
        candidate->pattern.type == PATTERN_SINGLE) {
        /* When an opponent has reported single, following with the minimum card often
         * hands them the turn, so a single-card follow favors the largest available
         * blocker over preserving a high singleton. */
        score += (int)RankValue(candidate->pattern.mainRank) * 115;
        if (candidate->pattern.mainRank >= RANK_KING) {
            score += 420;
        }
    }

    if (context->minOpponentRemainingCards > 0 && context->minOpponentRemainingCards <= 2) {
        score += candidate->pattern.cardCount * 35;
    }

    if (context->leading && !urgentDefense && leaves > 3) {
        const int rankValue = (int)RankValue(candidate->pattern.mainRank);

        if (candidate->pattern.type == PATTERN_SINGLE) {
            /* Normal lead turns should burn low loose cards first.  Without this guard
             * the remainder evaluator can prefer throwing away A/2 just because the
             * leftover low cards still form a pretty-looking structure. */
            score -= rankValue * 42;
            if (rankValue >= (int)RankValue(RANK_ACE)) {
                score -= 520;
            }
            if (candidate->pattern.mainRank == RANK_KING ||
                candidate->pattern.mainRank == RANK_ACE) {
                const int unknownHigher =
                    Ai_UnknownHigherControlCount(candidate, context, candidate->pattern.mainRank);

                if (unknownHigher == 0) {
                    score += 260;
                } else if (unknownHigher == 1) {
                    score += 120;
                }
            }
        } else if (candidate->pattern.type == PATTERN_PAIR) {
            score -= rankValue * 18;
            if (rankValue >= (int)RankValue(RANK_ACE)) {
                score -= 360;
            }
        } else {
            score -= rankValue * 3;
        }
    }

    if (context->leading && !urgentDefense) {
        score += ProvenSingleControlBonus(candidate, context);
    }

    if (candidate->pattern.type == PATTERN_BOMB) {
        const bool critical = leaves == 0 || leaves <= 2 ||
            (context->minOpponentRemainingCards > 0 &&
             context->minOpponentRemainingCards <= 4);

        score += 180;
        if (critical) {
            score += 850;
        } else {
            score -= context->leading ? 250 : 500;
        }
        if (!context->leading && context->previous.type != PATTERN_BOMB && !critical) {
            score -= 550;
        }
    }

    return score;
}

void Ai_CandidateKey(const AiCandidate *candidate, char *out, int cap)
{
    AiRankCounts counts;
    Str text;

    Str_Init(&text);
    Str_Append(&text, PatternName(candidate->pattern.type));
    Str_Append(&text, ":");
    Str_Append(&text, RankName(candidate->pattern.mainRank));
    Str_Append(&text, ":");
    Str_AppendNumber(&text, candidate->pattern.cardCount);
    Str_Append(&text, ":");
    Str_AppendNumber(&text, candidate->pattern.groupCount);
    Str_Append(&text, ":");
    AiRankCounts_Build(&candidate->cards, &counts);
    for (Rank rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
        if (counts.count[rank] > 0) {
            Str_Append(&text, RankName(rank));
            Str_AppendNumber(&text, counts.count[rank]);
            Str_Append(&text, ",");
        }
    }
    Str_CopyTo(out, cap, Str_CStr(&text));
    Str_Free(&text);
}

Cards Ai_RemainingAfterPlay(const Cards *hand, uint64_t mask)
{
    Cards remaining;

    Cards_Clear(&remaining);
    for (int i = 0; i < hand->count; ++i) {
        if ((mask & (1ull << i)) == 0) {
            Cards_Push(&remaining, hand->items[i]);
        }
    }
    return remaining;
}

typedef struct AiCandidateBuild {
    const Cards *hand;
    const AiContext *context;
    const AiRankCounts *handCounts;
    int n;
    AiCandidateList *out;
} AiCandidateBuild;

static void AddCandidate(AiCandidateBuild *build, uint64_t mask)
{
    Cards cards;
    Cards remainder;
    MoveValidation validation;
    AiCandidate candidate;
    int score;
    int leaves;

    Cards_Clear(&cards);
    for (int i = 0; i < build->n; ++i) {
        if ((mask & (1ull << i)) != 0) {
            Cards_Push(&cards, build->hand->items[i]);
        }
    }

    validation = build->context->leading ? ValidateLead(&cards, build->n)
                                         : ValidateFollow(&cards, &build->context->previous,
                                                          build->n);
    if (!validation.ok) {
        return;
    }

    remainder = Ai_RemainingAfterPlay(build->hand, mask);
    leaves = remainder.count;
    candidate.cards = cards;
    candidate.pattern = validation.pattern;
    candidate.remainder = remainder;
    candidate.disruptionPenalty =
        Ai_GroupDisruptionPenalty(build->handCounts, &cards, &validation.pattern);
    candidate.score = 0;

    score = Ai_PatternBaseScore(validation.pattern.type);
    score += cards.count * (build->context->leading ? 92 : 12);
    score -= (int)RankValue(validation.pattern.mainRank) * (build->context->leading ? 2 : 10);
    score += Ai_EvaluateRemainingHand(&remainder);
    score -= candidate.disruptionPenalty * (build->context->leading ? 2 : 3);
    if (leaves == 0) {
        score += 100000;
    } else if (leaves <= 2) {
        score += 450;
    }
    if (!build->context->leading) {
        score += Ai_EvaluateRemainingHand(&remainder);
    }
    candidate.score = score;
    candidate.score -= Ai_KickerControlPenalty(&candidate) * (build->context->leading ? 2 : 3);
    candidate.score += Ai_TacticalAdjustment(&candidate, build->context);
    AiCandidateList_Push(build->out, &candidate);
}

void Ai_GenerateCandidates(const Cards *hand, const AiContext *context, AiCandidateList *out)
{
    AiCandidateBuild build;
    AiRankCounts handCounts;
    const int n = hand->count;

    if (n == 0 || n > 20) {
        return;
    }

    AiRankCounts_Build(hand, &handCounts);
    build.hand = hand;
    build.context = context;
    build.handCounts = &handCounts;
    build.n = n;
    build.out = out;

    if (context->leading) {
        const uint64_t limit = 1ull << n;

        for (uint64_t mask = 1; mask < limit; ++mask) {
            AddCandidate(&build, mask);
        }
    } else {
        AiMaskList masks;

        AiMaskList_Init(&masks);
        FollowCandidateMasks(n, &context->previous, &masks);
        for (int i = 0; i < masks.count; ++i) {
            AddCandidate(&build, masks.items[i]);
        }
        AiMaskList_Free(&masks);
    }
}

void Ai_DeduplicateCandidates(AiCandidateList *candidates)
{
    AiKeySet seen;
    int write = 0;

    if (candidates->count <= 1) {
        return;
    }
    if (!AiKeySet_Init(&seen, candidates->count)) {
        return;
    }
    for (int i = 0; i < candidates->count; ++i) {
        char key[PDK_AI_KEY_CAP];

        Ai_CandidateKey(&candidates->items[i], key, PDK_AI_KEY_CAP);
        if (AiKeySet_Add(&seen, key)) {
            candidates->items[write++] = candidates->items[i];
        }
    }
    candidates->count = write;
    AiKeySet_Free(&seen);
}

/* ---- the basic strategy ---------------------------------------------- */

/* Exactly the old std::sort comparator.  Applied as a stable insertion sort so the
 * result cannot depend on the standard library's sort implementation -- std::sort is
 * not stable, and the two toolchains use different algorithms. */
static bool CandidateLess(const AiCandidate *lhs, const AiCandidate *rhs)
{
    if (lhs->score != rhs->score) {
        return lhs->score > rhs->score;
    }
    if (lhs->cards.count != rhs->cards.count) {
        return lhs->cards.count > rhs->cards.count;
    }
    return RankValue(lhs->pattern.mainRank) < RankValue(rhs->pattern.mainRank);
}

static void SortCandidates(AiCandidate *items, int count)
{
    for (int i = 1; i < count; ++i) {
        const AiCandidate held = items[i];
        int j = i - 1;

        while (j >= 0 && CandidateLess(&held, &items[j])) {
            items[j + 1] = items[j];
            --j;
        }
        items[j + 1] = held;
    }
}

static int RecommendBasicMoves(const Cards *hand, const AiContext *context, int limit,
                               AiMoveChoice *out, int outCapacity)
{
    AiCandidateList candidates;
    AiKeySet seen;
    int wanted;
    int written = 0;

    AiCandidateList_Init(&candidates);
    Ai_GenerateCandidates(hand, context, &candidates);
    if (candidates.count == 0) {
        AiCandidateList_Free(&candidates);
        return 0;
    }

    SortCandidates(candidates.items, candidates.count);
    wanted = limit < candidates.count ? limit : candidates.count;
    if (wanted > outCapacity) {
        wanted = outCapacity;
    }
    if (wanted < 0) {
        wanted = 0;
    }
    if (!AiKeySet_Init(&seen, candidates.count)) {
        AiCandidateList_Free(&candidates);
        return 0;
    }

    for (int i = 0; i < candidates.count && written < wanted; ++i) {
        char key[PDK_AI_KEY_CAP];
        Str reason;

        Ai_CandidateKey(&candidates.items[i], key, PDK_AI_KEY_CAP);
        if (!AiKeySet_Add(&seen, key)) {
            continue;
        }
        Str_Init(&reason);
        Str_Append(&reason, "基础 AI 推荐 ");
        Str_Append(&reason, PatternName(candidates.items[i].pattern.type));
        out[written++] = AiMoveChoice_MakePlay(&candidates.items[i].cards,
                                              &candidates.items[i].pattern, Str_CStr(&reason),
                                              candidates.items[i].disruptionPenalty);
        Str_Free(&reason);
    }

    AiKeySet_Free(&seen);
    AiCandidateList_Free(&candidates);
    return written;
}

AiMoveChoice BasicAiStrategy_ChooseMove(const Cards *hand, const AiContext *context)
{
    AiMoveChoice one[1];

    if (RecommendBasicMoves(hand, context, 1, one, 1) > 0) {
        return one[0];
    }
    return AiMoveChoice_MakePass(context->leading ? "没有可出的牌型" : "压不过，选择不要");
}
