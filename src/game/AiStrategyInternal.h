#pragma once

/*
 * Shared internals of the two built-in AI strategies (plan.md S4).
 *
 * Pure C.  Every STL container the C++ version used here became either a fixed array or
 * a small growable list:
 *   - `std::map<Rank,int> CountRanks` -> AiRankCounts, a plain `int[16]` indexed by rank.
 *     The only thing the map provided was ascending iteration, which a rank loop
 *     reproduces exactly, and the array is both smaller and faster on the search's hot path.
 *   - `std::vector<Rank>` -> AiRankList (a hand has at most 13 distinct ranks).
 *   - `std::vector<Candidate>` -> AiCandidateList (growable; a 16-card lead produces
 *     several hundred).
 *   - `std::vector<uint64_t> masks` -> AiMaskList (growable: C(16,8) alone is 12870, so a
 *     fixed array would be ~100 KB of stack).
 *   - `std::set<std::string> seen` -> AiKeySet, a hashed key set, because the keys are
 *     short strings and rescanning a list would be quadratic in the candidate count.
 */

#include "game/AiStrategy.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Rank constants run RANK_THREE..RANK_TWO, so 16 slots cover every legal index. */
enum { PDK_RANK_SLOTS = 16 };
enum { PDK_AI_RANKS_MAX = 16 };
enum { PDK_AI_KEY_CAP = 96 };

typedef struct AiRankCounts {
    int count[PDK_RANK_SLOTS];
} AiRankCounts;

typedef struct AiRankList {
    Rank items[PDK_AI_RANKS_MAX];
    int count;
} AiRankList;

typedef struct AiCandidate {
    Cards cards;
    HandPattern pattern;
    Cards remainder;
    int disruptionPenalty;
    int score;
} AiCandidate;

typedef struct AiCandidateList {
    AiCandidate *items;
    int count;
    int capacity;
} AiCandidateList;

typedef struct AiMaskList {
    uint64_t *items;
    int count;
    int capacity;
} AiMaskList;

typedef struct AiKeySet {
    char (*keys)[PDK_AI_KEY_CAP];
    unsigned *hashes;
    int count;
    int capacity;
} AiKeySet;

static inline void AiRankCounts_Clear(AiRankCounts *counts)
{
    for (int i = 0; i < PDK_RANK_SLOTS; ++i) {
        counts->count[i] = 0;
    }
}

static inline int AiRankCounts_Get(const AiRankCounts *counts, Rank rank)
{
    return (rank < PDK_RANK_SLOTS) ? counts->count[rank] : 0;
}

static inline bool AiRankCounts_Has(const AiRankCounts *counts, Rank rank)
{
    return AiRankCounts_Get(counts, rank) > 0;
}

static inline void AiRankList_Clear(AiRankList *list)
{
    list->count = 0;
}

static inline bool AiRankList_Push(AiRankList *list, Rank rank)
{
    if (list->count >= PDK_AI_RANKS_MAX) {
        return false;
    }
    list->items[list->count++] = rank;
    return true;
}

void AiRankCounts_Build(const Cards *cards, AiRankCounts *out);

void AiCandidateList_Init(AiCandidateList *list);
void AiCandidateList_Free(AiCandidateList *list);
bool AiCandidateList_Push(AiCandidateList *list, const AiCandidate *candidate);

void AiMaskList_Init(AiMaskList *list);
void AiMaskList_Free(AiMaskList *list);
bool AiMaskList_Push(AiMaskList *list, uint64_t mask);

/* Returns false when the set could not be allocated; callers then skip de-duplication
 * rather than failing, which is the closest a game AI can sensibly come to the old
 * bad_alloc behaviour. */
bool AiKeySet_Init(AiKeySet *set, int capacity);
void AiKeySet_Free(AiKeySet *set);
/* False when the key was already present. */
bool AiKeySet_Add(AiKeySet *set, const char *key);

int Ai_PatternBaseScore(PatternType type);
int Ai_BestConsecutiveRunScore(const Rank *ranks, int rankCount, int minLength, int perRankScore);
void Ai_CoreUsage(const Cards *cards, const HandPattern *pattern, AiRankCounts *out);
int Ai_GroupDisruptionPenalty(const AiRankCounts *beforeCounts, const Cards *played,
                              const HandPattern *pattern);
int Ai_KickerControlPenalty(const AiCandidate *candidate);
int Ai_EvaluateRemainingHand(const Cards *cards);
int Ai_UnknownRankCount(const AiCandidate *candidate, const AiContext *context, Rank rank);
int Ai_UnknownHigherControlCount(const AiCandidate *candidate, const AiContext *context,
                                 Rank rank);
int Ai_TacticalAdjustment(const AiCandidate *candidate, const AiContext *context);
void Ai_CandidateKey(const AiCandidate *candidate, char *out, int cap);
Cards Ai_RemainingAfterPlay(const Cards *hand, uint64_t mask);
void Ai_GenerateCandidates(const Cards *hand, const AiContext *context, AiCandidateList *out);
void Ai_DeduplicateCandidates(AiCandidateList *candidates);

#ifdef __cplusplus
}
#endif
