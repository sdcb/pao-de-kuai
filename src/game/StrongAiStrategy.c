#include "game/AiStrategy.h"
#include "game/AiStrategyInternal.h"

#include "core/Str.h"
#include "rules/Deck.h"

#include <windows.h>

#include <process.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * The strong strategy: the basic evaluator plus pass-history inference, lead planning
 * and a parallel rollout search.
 *
 * Pure C (plan.md S8).  What replaced what:
 *
 *   - the two `static std::mutex` around the memo tables became SRWLOCK with
 *     SRWLOCK_INIT.  This is where losing RAII actually bites, so the rule followed
 *     here is that no function ever holds the lock across a possible exit: every
 *     AcquireSRWLockExclusive sits in a tiny helper whose only way out is the matching
 *     ReleaseSRWLockExclusive (FollowCache_Lookup/_Store, LeadCountCache_Lookup/_Store),
 *     and the callers' early returns all happen after the helper returned, i.e. after
 *     the release.  There is no path that can return between Acquire and Release.
 *   - the four `std::atomic<int>` work counters became `volatile LONG` driven by
 *     InterlockedIncrement / InterlockedExchangeAdd.  InterlockedIncrement returns the
 *     *new* value, so an `x.fetch_add(1)` is written `(int)InterlockedIncrement(&x) - 1`.
 *   - the two `std::array<std::thread, 8>` pools became _beginthreadex plus
 *     WaitForMultipleObjects, with the calling thread running the same work loop it ran
 *     before (`worker();`).  StartWorkers/JoinWorkers own the handle lifetime so the
 *     CloseHandle calls happen at exactly one point per pool, including when a thread
 *     could not be created.
 *   - `std::vector<Candidate>` -> the growable AiCandidateList, `std::map<Rank,int>` ->
 *     AiRankCounts, `std::optional` -> value + bool, `std::sort` -> a stable insertion
 *     sort (std::sort is unstable and the two toolchains order equal elements
 *     differently), `std::min`/`std::max` -> ternaries, `std::move` -> copy,
 *     `std::string` -> Str.
 *
 * Four helpers of the C++ file have no call site in either version: ProvenControlBonus,
 * FinishPlanBonus, LooseSingleCount and EstimatedControlCount (grep each name in
 * StrongAiStrategy.cpp -- one occurrence, the definition).  They are translated verbatim
 * and keep the external linkage they had at namespace scope: a file-local translation
 * would turn them into unused `static` functions, which is a -Wunused-function warning on
 * the -Wall -Wextra build.  Their own helpers stay `static`, because being reachable from
 * these four is enough to keep the compiler quiet.  They are kept rather than deleted so
 * that this file stays a 1:1 port; the strategy simply does not call them today.
 */

enum { PDK_STRONG_WORKERS = 8 };
/* The parallel sample pool is only entered with sampleCount 9 or 5; the cap below is
 * defensive so the fixed buffers cannot be overrun if a third call site appears. */
enum { PDK_STRONG_SAMPLE_CAP = 64 };

/* ---- the two memo tables ---------------------------------------------- */

/*
 * `CachedHasAnyFollowMove` and `MinimumLeadCount` used `std::map` plus `std::mutex`.
 * Both memoise a pure function of their key, so the shape of the table is unobservable:
 * a miss recomputes exactly the same answer.  They are open addressed with linear
 * probing, grow by doubling on insert and never remove anything.
 */

typedef struct FollowKey {
    uint64_t handMask;
    uint64_t pattern;
    int handSize;
} FollowKey;

typedef struct FollowEntry {
    FollowKey key;
    bool value;
    bool used;
} FollowEntry;

typedef struct FollowCache {
    FollowEntry *items;
    int capacity; /* power of two; 0 until the first insert */
    int count;
} FollowCache;

static FollowCache g_followCache = { NULL, 0, 0 };
static SRWLOCK g_followCacheLock = SRWLOCK_INIT;

static unsigned FollowKey_Hash(const FollowKey *key)
{
    uint64_t mixed = key->handMask;

    mixed ^= key->pattern + 0x9e3779b97f4a7c15ull + (mixed << 6) + (mixed >> 2);
    mixed ^= (uint64_t)(unsigned)key->handSize + 0x9e3779b97f4a7c15ull +
             (mixed << 6) + (mixed >> 2);
    return (unsigned)(mixed ^ (mixed >> 32));
}

static bool FollowKey_Equal(const FollowKey *lhs, const FollowKey *rhs)
{
    return lhs->handMask == rhs->handMask && lhs->pattern == rhs->pattern &&
           lhs->handSize == rhs->handSize;
}

static bool FollowCache_Find(const FollowCache *cache, const FollowKey *key, bool *value)
{
    int index;
    const int mask = cache->capacity - 1;

    if (cache->capacity == 0) {
        return false;
    }
    index = (int)(FollowKey_Hash(key) & (unsigned)mask);
    for (int probe = 0; probe < cache->capacity; ++probe) {
        const FollowEntry *entry = &cache->items[index];

        /* Nothing is ever removed, so an unused slot ends the probe run. */
        if (!entry->used) {
            return false;
        }
        if (FollowKey_Equal(&entry->key, key)) {
            *value = entry->value;
            return true;
        }
        index = (index + 1) & mask;
    }
    return false;
}

/* Rehashes into a table twice the size.  False on allocation failure, in which case the
 * old table is left untouched. */
static bool FollowCache_Grow(FollowCache *cache)
{
    const int oldCapacity = cache->capacity;
    FollowEntry *oldItems = cache->items;
    const int next = oldCapacity > 0 ? oldCapacity * 2 : 256;
    const int mask = next - 1;
    FollowEntry *items = (FollowEntry *)calloc((size_t)next, sizeof(FollowEntry));

    if (items == NULL) {
        return false;
    }
    for (int i = 0; i < oldCapacity; ++i) {
        if (oldItems[i].used) {
            int index = (int)(FollowKey_Hash(&oldItems[i].key) & (unsigned)mask);

            while (items[index].used) {
                index = (index + 1) & mask;
            }
            items[index] = oldItems[i];
        }
    }
    free(oldItems);
    cache->items = items;
    cache->capacity = next;
    return true;
}

static void FollowCache_Insert(FollowCache *cache, const FollowKey *key, bool value)
{
    int index;

    if (cache->capacity == 0 || cache->count * 4 >= cache->capacity * 3) {
        if (!FollowCache_Grow(cache)) {
            return; /* out of memory: the entry is simply not cached */
        }
    }
    index = (int)(FollowKey_Hash(key) & (unsigned)(cache->capacity - 1));
    for (int probe = 0; probe < cache->capacity; ++probe) {
        FollowEntry *entry = &cache->items[index];

        if (!entry->used) {
            entry->key = *key;
            entry->value = value;
            entry->used = true;
            cache->count++;
            return;
        }
        if (FollowKey_Equal(&entry->key, key)) {
            /* Two workers can miss on the same key at once; the function is pure, so
             * the value they computed is the same. */
            entry->value = value;
            return;
        }
        index = (index + 1) & (cache->capacity - 1);
    }
}

/* The only two entry points, so the lock cannot be leaked by a caller's early return. */
static bool FollowCache_Lookup(const FollowKey *key, bool *value)
{
    bool found;

    AcquireSRWLockExclusive(&g_followCacheLock);
    found = FollowCache_Find(&g_followCache, key, value);
    ReleaseSRWLockExclusive(&g_followCacheLock);
    return found;
}

static void FollowCache_Store(const FollowKey *key, bool value)
{
    AcquireSRWLockExclusive(&g_followCacheLock);
    FollowCache_Insert(&g_followCache, key, value);
    ReleaseSRWLockExclusive(&g_followCacheLock);
}

typedef struct LeadCountEntry {
    uint64_t key;
    int value;
    bool used;
} LeadCountEntry;

typedef struct LeadCountCache {
    LeadCountEntry *items;
    int capacity;
    int count;
} LeadCountCache;

static LeadCountCache g_leadCountCache = { NULL, 0, 0 };
static SRWLOCK g_leadCountCacheLock = SRWLOCK_INIT;

static unsigned LeadCountKey_Hash(uint64_t key)
{
    const uint64_t mixed = key * 0x9e3779b97f4a7c15ull;

    return (unsigned)(mixed ^ (mixed >> 32));
}

static bool LeadCountCache_Find(const LeadCountCache *cache, uint64_t key, int *value)
{
    int index;
    const int mask = cache->capacity - 1;

    if (cache->capacity == 0) {
        return false;
    }
    index = (int)(LeadCountKey_Hash(key) & (unsigned)mask);
    for (int probe = 0; probe < cache->capacity; ++probe) {
        const LeadCountEntry *entry = &cache->items[index];

        if (!entry->used) {
            return false;
        }
        if (entry->key == key) {
            *value = entry->value;
            return true;
        }
        index = (index + 1) & mask;
    }
    return false;
}

static bool LeadCountCache_Grow(LeadCountCache *cache)
{
    const int oldCapacity = cache->capacity;
    LeadCountEntry *oldItems = cache->items;
    const int next = oldCapacity > 0 ? oldCapacity * 2 : 256;
    const int mask = next - 1;
    LeadCountEntry *items = (LeadCountEntry *)calloc((size_t)next, sizeof(LeadCountEntry));

    if (items == NULL) {
        return false;
    }
    for (int i = 0; i < oldCapacity; ++i) {
        if (oldItems[i].used) {
            int index = (int)(LeadCountKey_Hash(oldItems[i].key) & (unsigned)mask);

            while (items[index].used) {
                index = (index + 1) & mask;
            }
            items[index] = oldItems[i];
        }
    }
    free(oldItems);
    cache->items = items;
    cache->capacity = next;
    return true;
}

static void LeadCountCache_Insert(LeadCountCache *cache, uint64_t key, int value)
{
    int index;

    if (cache->capacity == 0 || cache->count * 4 >= cache->capacity * 3) {
        if (!LeadCountCache_Grow(cache)) {
            return;
        }
    }
    index = (int)(LeadCountKey_Hash(key) & (unsigned)(cache->capacity - 1));
    for (int probe = 0; probe < cache->capacity; ++probe) {
        LeadCountEntry *entry = &cache->items[index];

        if (!entry->used) {
            entry->key = key;
            entry->value = value;
            entry->used = true;
            cache->count++;
            return;
        }
        if (entry->key == key) {
            entry->value = value;
            return;
        }
        index = (index + 1) & (cache->capacity - 1);
    }
}

static bool LeadCountCache_Lookup(uint64_t key, int *value)
{
    bool found;

    AcquireSRWLockExclusive(&g_leadCountCacheLock);
    found = LeadCountCache_Find(&g_leadCountCache, key, value);
    ReleaseSRWLockExclusive(&g_leadCountCacheLock);
    return found;
}

static void LeadCountCache_Store(uint64_t key, int value)
{
    AcquireSRWLockExclusive(&g_leadCountCacheLock);
    LeadCountCache_Insert(&g_leadCountCache, key, value);
    ReleaseSRWLockExclusive(&g_leadCountCacheLock);
}

/* ---- worker pools ------------------------------------------------------ */

/*
 * Both pools are "N threads plus the calling thread", pull work from a shared counter
 * and are joined before returning.  The handle array is owned here, so a failed
 * _beginthreadex cannot leak a handle and the CloseHandle loop runs exactly once.
 */
static int StartWorkers(HANDLE *handles, int maxWorkers,
                        unsigned(__stdcall *start)(void *), void *parameter)
{
    int created = 0;

    for (int i = 0; i < maxWorkers; ++i) {
        const uintptr_t raw = _beginthreadex(NULL, 0, start, parameter, 0, NULL);

        if (raw == 0) {
            break;
        }
        handles[created++] = (HANDLE)raw;
    }
    return created;
}

static void JoinWorkers(HANDLE *handles, int count)
{
    if (count > 0) {
        WaitForMultipleObjects((DWORD)count, handles, TRUE, INFINITE);
    }
    for (int i = 0; i < count; ++i) {
        CloseHandle(handles[i]);
    }
}

/* ---- deterministic pseudo random sampling ------------------------------ */

static uint32_t MixSeed(uint32_t seed, uint32_t value)
{
    seed ^= value + 0x9e3779b9u + (seed << 6) + (seed >> 2);
    return seed;
}

static uint32_t CandidateSeed(const AiCandidate *candidate, const AiContext *context)
{
    uint32_t seed = 2166136261u;

    seed = MixSeed(seed, (uint32_t)context->currentPlayerIndex);
    seed = MixSeed(seed, (uint32_t)context->lastMovePlayerIndex);
    seed = MixSeed(seed, (uint32_t)candidate->pattern.cardCount);
    seed = MixSeed(seed, (uint32_t)candidate->pattern.groupCount);
    seed = MixSeed(seed, (uint32_t)RankValue(candidate->pattern.mainRank));
    for (int i = 0; i < candidate->cards.count; ++i) {
        const Card card = candidate->cards.items[i];

        seed = MixSeed(seed, (uint32_t)(RankValue(card.rank) * 5 + (int)card.suit));
    }
    for (int i = 0; i < context->playedCards.count; ++i) {
        const Card card = context->playedCards.items[i];

        seed = MixSeed(seed, (uint32_t)(RankValue(card.rank) * 5 + (int)card.suit));
    }
    return seed;
}

static uint32_t NextRandom(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void ShuffleSample(Cards *cards, uint32_t seed)
{
    if (cards->count <= 1) {
        return;
    }
    /* The C++ version walked std::size_t indices; every count here is at most 48, so an
     * int loop with the same 32 bit modulo produces the identical permutation. */
    for (int i = cards->count; i > 1; --i) {
        const int j = (int)(NextRandom(&seed) % (uint32_t)i);
        const Card held = cards->items[i - 1];

        cards->items[i - 1] = cards->items[j];
        cards->items[j] = held;
    }
}

/* ---- pass observation inference --------------------------------------- */

static bool SameObservationClass(const HandPattern *lhs, const HandPattern *rhs)
{
    if (lhs->type != rhs->type) {
        return false;
    }
    switch (lhs->type) {
    case PATTERN_STRAIGHT:
    case PATTERN_CONSECUTIVE_PAIRS:
    case PATTERN_PLANE:
        return lhs->cardCount == rhs->cardCount && lhs->groupCount == rhs->groupCount;
    case PATTERN_SINGLE:
    case PATTERN_PAIR:
    case PATTERN_TRIPLE_WITH_ONE:
    case PATTERN_TRIPLE_WITH_PAIR:
    case PATTERN_BOMB:
        return lhs->cardCount == rhs->cardCount;
    case PATTERN_INVALID:
    default:
        return false;
    }
}

static bool ObservationProvesCannotBeat(const AiCandidate *candidate,
                                        const PassObservation *observation)
{
    if (observation->remainingCards <= 0) {
        return false;
    }
    if (candidate->pattern.type == PATTERN_BOMB &&
        observation->pattern.type != PATTERN_BOMB) {
        /* Passing on any non-bomb follow proves the player had no bomb then. */
        return true;
    }
    if (!SameObservationClass(&candidate->pattern, &observation->pattern)) {
        return false;
    }
    return RankValue(candidate->pattern.mainRank) >=
           RankValue(observation->pattern.mainRank);
}

int ProvenControlBonus(const AiCandidate *candidate, const AiContext *context)
{
    int provenOpponents = 0;

    if (!context->leading) {
        return 0;
    }

    for (int i = 0; i < PDK_AI_SEATS; ++i) {
        const PassHistory *history;
        const OptionalPassObservation *observation;
        bool proven = false;

        if (i == context->currentPlayerIndex || context->remainingCards[i] <= 0) {
            continue;
        }
        history = &context->passHistory[i];
        for (int h = 0; h < history->count; ++h) {
            if (ObservationProvesCannotBeat(candidate, &history->items[h])) {
                proven = true;
                break;
            }
        }
        observation = &context->passObservations[i];
        if (!proven && observation->has &&
            ObservationProvesCannotBeat(candidate, &observation->value)) {
            proven = true;
        }
        if (proven) {
            provenOpponents++;
        }
    }

    if (provenOpponents == 0) {
        return 0;
    }
    return provenOpponents == 1 ? 520 + candidate->pattern.cardCount * 35
                                : 1450 + candidate->pattern.cardCount * 70;
}

/* ---- finish planning --------------------------------------------------- */

static bool IsWholeHandLead(const Cards *cards)
{
    return cards->count > 0 && ValidateLead(cards, cards->count).ok;
}

static bool CanFinishWithinTwoLeads(const Cards *cards)
{
    const int n = cards->count;

    if (n <= 0) {
        return true;
    }
    if (IsWholeHandLead(cards)) {
        return true;
    }
    if (n > 16) {
        return false;
    }

    {
        const uint64_t limit = 1ull << n;

        for (uint64_t mask = 1; mask < limit - 1; ++mask) {
            Cards first;
            Cards second;

            Cards_Clear(&first);
            Cards_Clear(&second);
            for (int i = 0; i < n; ++i) {
                if ((mask & (1ull << i)) != 0) {
                    Cards_Push(&first, cards->items[i]);
                } else {
                    Cards_Push(&second, cards->items[i]);
                }
            }
            if (ValidateLead(&first, n).ok && ValidateLead(&second, second.count).ok) {
                return true;
            }
        }
    }
    return false;
}

int FinishPlanBonus(const Cards *remainder)
{
    if (remainder->count == 0) {
        return 0;
    }
    if (IsWholeHandLead(remainder)) {
        return 3600 + remainder->count * 80;
    }
    if (CanFinishWithinTwoLeads(remainder)) {
        return 1700 + remainder->count * 35;
    }
    return 0;
}

/* ---- cache keys -------------------------------------------------------- */

static uint64_t CardSetMask(const Cards *cards)
{
    uint64_t mask = 0;

    for (int i = 0; i < cards->count; ++i) {
        const Card card = cards->items[i];
        const int rankOffset = RankValue(card.rank) - RankValue(RANK_THREE);
        const int bit = rankOffset * 4 + (int)card.suit;

        mask |= 1ull << bit;
    }
    return mask;
}

static uint64_t PatternKey(const HandPattern *pattern)
{
    uint64_t key = (uint64_t)pattern->type;

    key = (key << 8) | (uint64_t)RankValue(pattern->mainRank);
    key = (key << 8) | (uint64_t)pattern->cardCount;
    key = (key << 8) | (uint64_t)pattern->groupCount;
    return key;
}

static bool CachedHasAnyFollowMove(const Cards *hand, const HandPattern *previous,
                                   int handSizeBeforePlay)
{
    FollowKey key;
    bool value = false;

    key.handMask = CardSetMask(hand);
    key.pattern = PatternKey(previous);
    key.handSize = handSizeBeforePlay;

    /* The early "already cached" return sits *after* the lookup, which has already
     * released the lock -- the std::lock_guard used to do that on this path. */
    if (FollowCache_Lookup(&key, &value)) {
        return value;
    }

    value = HasAnyFollowMove(hand, previous, handSizeBeforePlay);

    FollowCache_Store(&key, value);
    return value;
}

/* ---- lead planning ----------------------------------------------------- */

/*
 * Minimum number of leads that empty `cards`, by set-cover DP over the subsets of the
 * hand.  Split out of MinimumLeadCount so both allocations have one release point.
 * Returns false when the scratch buffers cannot be allocated.
 */
static bool ComputeMinimumLeadCount(const Cards *cards, int *out)
{
    const int n = cards->count;
    const int fullMask = (1 << n) - 1;
    const int inf = 99;
    AiMaskList legalMasks;
    int *dp = NULL;
    int result = 8;
    bool ok = false;

    AiMaskList_Init(&legalMasks);
    dp = (int *)malloc(sizeof(int) * (size_t)(fullMask + 1));
    if (dp == NULL) {
        goto cleanup;
    }

    /*
     * Every subset that is a legal lead, in ascending mask order.  The C++ version then
     * built legalByCard[card] lists and scanned one of them per DP step; scanning this
     * array for membership of the lowest remaining card reproduces exactly that list,
     * in the same order, without the vector of vectors.
     */
    for (int mask = 1; mask <= fullMask; ++mask) {
        Cards play;

        Cards_Clear(&play);
        for (int i = 0; i < n; ++i) {
            if ((mask & (1 << i)) != 0) {
                Cards_Push(&play, cards->items[i]);
            }
        }
        if (ValidateLead(&play, n).ok) {
            if (!AiMaskList_Push(&legalMasks, (uint64_t)mask)) {
                goto cleanup;
            }
        }
    }

    for (int i = 0; i <= fullMask; ++i) {
        dp[i] = inf;
    }
    dp[0] = 0;
    for (int remaining = 1; remaining <= fullMask; ++remaining) {
        int first = 0;
        int best = inf;

        while ((remaining & (1 << first)) == 0) {
            ++first;
        }
        for (int i = 0; i < legalMasks.count; ++i) {
            const int legal = (int)legalMasks.items[i];

            if ((legal & (1 << first)) == 0) {
                continue;
            }
            if ((legal & remaining) == legal) {
                const int candidate = 1 + dp[remaining ^ legal];

                if (candidate < best) {
                    best = candidate;
                }
            }
        }
        dp[remaining] = best;
    }

    result = dp[fullMask];
    ok = true;
cleanup:
    free(dp);
    AiMaskList_Free(&legalMasks);
    if (ok) {
        *out = result;
    }
    return ok;
}

static int MinimumLeadCount(const Cards *cards)
{
    const int n = cards->count;
    uint64_t cacheKey;
    int result;

    if (n == 0) {
        return 0;
    }
    if (n > 16) {
        return 8;
    }

    cacheKey = CardSetMask(cards);
    if (LeadCountCache_Lookup(cacheKey, &result)) {
        return result;
    }

    if (!ComputeMinimumLeadCount(cards, &result)) {
        /* Allocation failure only; the search falls back to the same pessimistic count
         * the oversized-hand case uses and does not cache it. */
        return 8;
    }
    LeadCountCache_Store(cacheKey, result);
    return result;
}

static int LeadCountPlanBonus(const Cards *remainder)
{
    const int count = MinimumLeadCount(remainder);
    int score;

    if (count == 0) {
        return 0;
    }
    score = -count * 650;
    if (count == 1) {
        score += 4000;
    } else if (count == 2) {
        score += 2200;
    } else if (count == 3) {
        score += 600;
    }
    return score;
}

/* ---- unknown card pressure -------------------------------------------- */

static int UnknownPatternBeaterPressureForPattern(const AiCandidate *candidate,
                                                  const HandPattern *pattern,
                                                  const AiContext *context);

static int RemainderFinishSafetyBonus(const AiCandidate *candidate, const AiContext *context)
{
    MoveValidation result;
    int pressure;

    if (candidate->remainder.count == 0) {
        return 0;
    }
    result = ValidateLead(&candidate->remainder, candidate->remainder.count);
    if (!result.ok) {
        return 0;
    }
    pressure = UnknownPatternBeaterPressureForPattern(candidate, &result.pattern, context);
    if (pressure == 0) {
        return 2600 + result.pattern.cardCount * 110;
    }
    if (pressure <= 2) {
        return 900 + result.pattern.cardCount * 60;
    }
    return 0;
}

int LooseSingleCount(const Cards *cards)
{
    AiRankCounts counts;
    int singles = 0;

    AiRankCounts_Build(cards, &counts);
    for (Rank rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
        if (counts.count[rank] == 1) {
            singles++;
        }
    }
    return singles;
}

int EstimatedControlCount(const Cards *cards)
{
    AiRankCounts counts;
    int controls = 0;

    AiRankCounts_Build(cards, &counts);
    for (Rank rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
        const int count = counts.count[rank];

        if (count == 0) {
            continue;
        }
        if (rank == RANK_TWO || rank == RANK_ACE) {
            controls += count;
        } else if (rank == RANK_KING) {
            controls += 1;
        }
        if (count >= 4 && rank >= RANK_THREE && rank <= RANK_KING) {
            controls += 2;
        }
    }
    return controls;
}

static int UnknownBombRankCount(const AiCandidate *candidate, const AiContext *context)
{
    int bombs = 0;

    for (int value = RankValue(RANK_THREE); value <= RankValue(RANK_KING); ++value) {
        if (Ai_UnknownRankCount(candidate, context, (Rank)value) >= 4) {
            bombs++;
        }
    }
    return bombs;
}

static int UnknownPatternBeaterPressureForPattern(const AiCandidate *candidate,
                                                  const HandPattern *pattern,
                                                  const AiContext *context)
{
    int pressure = 0;

    switch (pattern->type) {
    case PATTERN_SINGLE:
        for (int value = RankValue(pattern->mainRank) + 1; value <= RankValue(RANK_TWO); ++value) {
            pressure += Ai_UnknownRankCount(candidate, context, (Rank)value);
        }
        break;
    case PATTERN_PAIR:
        for (int value = RankValue(pattern->mainRank) + 1; value <= RankValue(RANK_ACE); ++value) {
            if (Ai_UnknownRankCount(candidate, context, (Rank)value) >= 2) {
                pressure += 2;
            }
        }
        break;
    case PATTERN_STRAIGHT:
    case PATTERN_CONSECUTIVE_PAIRS:
    case PATTERN_PLANE:
    case PATTERN_TRIPLE_WITH_ONE:
    case PATTERN_TRIPLE_WITH_PAIR:
        for (int value = RankValue(pattern->mainRank) + 1; value <= RankValue(RANK_ACE); ++value) {
            if (Ai_UnknownRankCount(candidate, context, (Rank)value) >= 3) {
                pressure += 1;
            }
        }
        break;
    case PATTERN_BOMB:
        for (int value = RankValue(pattern->mainRank) + 1; value <= RankValue(RANK_KING); ++value) {
            if (Ai_UnknownRankCount(candidate, context, (Rank)value) >= 4) {
                pressure += 4;
            }
        }
        break;
    case PATTERN_INVALID:
    default:
        break;
    }
    if (pattern->type != PATTERN_BOMB) {
        pressure += UnknownBombRankCount(candidate, context) * 3;
    }
    return pressure;
}

static int UnknownPatternBeaterPressure(const AiCandidate *candidate, const AiContext *context)
{
    return UnknownPatternBeaterPressureForPattern(candidate, &candidate->pattern, context);
}

/* ---- unknown hand sampling -------------------------------------------- */

static bool ContainsExactCard(const Cards *cards, Card target)
{
    for (int i = 0; i < cards->count; ++i) {
        if (cards->items[i].rank == target.rank && cards->items[i].suit == target.suit) {
            return true;
        }
    }
    return false;
}

static Cards UnknownOpponentCards(const Cards *hand, const AiContext *context)
{
    Cards unknown;
    const Cards deck = CreatePaoDeKuaiDeck();

    Cards_Clear(&unknown);
    for (int i = 0; i < deck.count; ++i) {
        if (!ContainsExactCard(hand, deck.items[i]) &&
            !ContainsExactCard(&context->playedCards, deck.items[i])) {
            Cards_Push(&unknown, deck.items[i]);
        }
    }
    return unknown;
}

static bool IsConsistentWithPassHistory(const Cards *hand, int playerIndex,
                                        const AiContext *context)
{
    const PassHistory *history;
    const OptionalPassObservation *latest;

    if (playerIndex < 0 || playerIndex >= PDK_AI_SEATS) {
        return true;
    }
    history = &context->passHistory[playerIndex];
    for (int h = 0; h < history->count; ++h) {
        if (CachedHasAnyFollowMove(hand, &history->items[h].pattern,
                                   history->items[h].remainingCards)) {
            return false;
        }
    }
    latest = &context->passObservations[playerIndex];
    if (latest->has &&
        CachedHasAnyFollowMove(hand, &latest->value.pattern, latest->value.remainingCards)) {
        return false;
    }
    return true;
}

/*
 * The body shared by SampledControlBonus and CachedUnknownSampledControlBonus.  The C++
 * file kept two copies that differed only in where `unknown` came from; the caller now
 * supplies it.
 */
static int SampleControlBonus(const AiCandidate *candidate, const AiContext *context,
                              const Cards *unknown, int nextIndex, int otherIndex)
{
    const int sampleCount = context->currentPlayerIndex == context->roundLeaderIndex ? 7 : 5;
    const int nextCards = context->remainingCards[nextIndex];
    const int otherCards = context->remainingCards[otherIndex];
    int score = 0;
    uint32_t seed;

    if (unknown->count != nextCards + otherCards) {
        return 0;
    }

    seed = CandidateSeed(candidate, context);
    for (int sample = 0; sample < sampleCount; ++sample) {
        Cards shuffled = *unknown;
        Cards nextHand;
        Cards otherHand;
        bool nextCanBeat;
        bool otherCanBeat;

        ShuffleSample(&shuffled, MixSeed(seed, (uint32_t)(sample + 1)));
        Cards_Clear(&nextHand);
        Cards_Clear(&otherHand);
        for (int i = 0; i < nextCards; ++i) {
            Cards_Push(&nextHand, shuffled.items[i]);
        }
        for (int i = 0; i < otherCards; ++i) {
            Cards_Push(&otherHand, shuffled.items[nextCards + i]);
        }

        if (!IsConsistentWithPassHistory(&nextHand, nextIndex, context) ||
            !IsConsistentWithPassHistory(&otherHand, otherIndex, context)) {
            continue;
        }

        nextCanBeat = CachedHasAnyFollowMove(&nextHand, &candidate->pattern, nextCards);
        otherCanBeat = CachedHasAnyFollowMove(&otherHand, &candidate->pattern, otherCards);
        if (!nextCanBeat && !otherCanBeat) {
            score += context->leading ? 620 : 520;
        } else {
            if (nextCanBeat) {
                score -= context->nextPlayerRemainingCards <= 3 ? 520 : 170;
                if (ValidateFollow(&nextHand, &candidate->pattern, nextCards).ok) {
                    score -= 900;
                }
            }
            if (otherCanBeat) {
                score -= otherCards <= 3 ? 460 : 130;
                if (ValidateFollow(&otherHand, &candidate->pattern, otherCards).ok) {
                    score -= 760;
                }
            }
        }
    }
    return score / sampleCount;
}

static int SampledControlBonus(const AiCandidate *candidate, const AiContext *context,
                               const Cards *hand)
{
    const int nextIndex = (context->currentPlayerIndex + 2) % 3;
    const int otherIndex = (context->currentPlayerIndex + 1) % 3;
    Cards unknown = UnknownOpponentCards(hand, context);

    return SampleControlBonus(candidate, context, &unknown, nextIndex, otherIndex);
}

static int CachedUnknownSampledControlBonus(const AiCandidate *candidate,
                                            const AiContext *context,
                                            const Cards *unknown)
{
    const int nextIndex = (context->currentPlayerIndex + 2) % 3;
    const int otherIndex = (context->currentPlayerIndex + 1) % 3;

    return SampleControlBonus(candidate, context, unknown, nextIndex, otherIndex);
}

/* ---- candidate ordering ------------------------------------------------ */

typedef bool (*CandidateLessFn)(const AiCandidate *lhs, const AiCandidate *rhs);

/*
 * Both comparators are the C++ lambdas verbatim.  They are applied as a stable insertion
 * sort: std::sort is not stable, and MSVC's and libstdc++'s implementations order equal
 * candidates differently, so a stable sort is the only portable way to keep one
 * deterministic answer (BasicAiStrategy.c makes the same choice).
 */
static bool RolloutCandidateLess(const AiCandidate *lhs, const AiCandidate *rhs)
{
    if (lhs->score != rhs->score) {
        return lhs->score > rhs->score;
    }
    if (lhs->cards.count != rhs->cards.count) {
        return lhs->cards.count > rhs->cards.count;
    }
    return RankValue(lhs->pattern.mainRank) < RankValue(rhs->pattern.mainRank);
}

static bool StrongCandidateLess(const AiCandidate *lhs, const AiCandidate *rhs)
{
    if (lhs->score != rhs->score) {
        return lhs->score > rhs->score;
    }
    if (lhs->cards.count != rhs->cards.count) {
        return lhs->cards.count > rhs->cards.count;
    }
    if (lhs->pattern.type != rhs->pattern.type) {
        return Ai_PatternBaseScore(lhs->pattern.type) > Ai_PatternBaseScore(rhs->pattern.type);
    }
    return RankValue(lhs->pattern.mainRank) < RankValue(rhs->pattern.mainRank);
}

static void SortCandidates(AiCandidate *items, int count, CandidateLessFn less)
{
    for (int i = 1; i < count; ++i) {
        const AiCandidate held = items[i];
        int j = i - 1;

        while (j >= 0 && less(&held, &items[j])) {
            items[j + 1] = items[j];
            --j;
        }
        items[j + 1] = held;
    }
}

/* ---- rollout ----------------------------------------------------------- */

static int NextIndex(int index)
{
    return (index + 2) % 3;
}

typedef struct RolloutState {
    Cards hands[3];
    int currentIndex;
    int lastMoveIndex;
    int trickLeaderIndex;
    int roundLeaderIndex;
    HandPattern lastPattern;
    bool hasLastPattern;
    Cards playedCards;
    OptionalPassObservation passObservations[PDK_AI_SEATS];
    PassHistory passHistory[PDK_AI_SEATS];
    int passCount;
} RolloutState;

static void RemoveRolloutCards(Cards *hand, const Cards *cards)
{
    for (int c = 0; c < cards->count; ++c) {
        for (int i = 0; i < hand->count; ++i) {
            if (hand->items[i].rank == cards->items[c].rank &&
                hand->items[i].suit == cards->items[c].suit) {
                Cards_RemoveAt(hand, i);
                break;
            }
        }
    }
}

static void RecordRolloutPass(RolloutState *state, int playerIndex,
                              const HandPattern *pattern)
{
    PassObservation observation;

    observation.pattern = *pattern;
    observation.remainingCards = state->hands[playerIndex].count;
    PassHistory_Add(&state->passHistory[playerIndex], &observation);
    OptionalPassObservation_Set(&state->passObservations[playerIndex], &observation);
}

/*
 * Fills `context` instead of returning it by value.  The memset is the C++ AiContext
 * default constructor (which zeroed the struct and set `leading`), and every field the
 * constructor's callers relied on is written below.
 */
static void RolloutContext(const RolloutState *state, AiContext *context)
{
    memset(context, 0, sizeof(*context));
    context->leading = !state->hasLastPattern;
    if (state->hasLastPattern) {
        context->previous = state->lastPattern;
    }
    context->currentPlayerIndex = state->currentIndex;
    context->lastMovePlayerIndex = state->lastMoveIndex;
    context->trickLeaderIndex =
        state->hasLastPattern ? state->trickLeaderIndex : state->currentIndex;
    context->roundLeaderIndex = state->roundLeaderIndex;
    context->currentTrickPassCount = state->passCount;
    context->ownRemainingCards = state->hands[state->currentIndex].count;
    for (int i = 0; i < 3; ++i) {
        context->remainingCards[i] = state->hands[i].count;
    }
    context->nextPlayerRemainingCards =
        state->hands[NextIndex(state->currentIndex)].count;
    context->minOpponentRemainingCards = 100;
    for (int i = 0; i < 3; ++i) {
        if (i != state->currentIndex &&
            context->remainingCards[i] < context->minOpponentRemainingCards) {
            context->minOpponentRemainingCards = context->remainingCards[i];
        }
    }
    context->playedCards = state->playedCards;
    PassObservations_Copy(context->passObservations, state->passObservations, PDK_AI_SEATS);
    PassHistories_Copy(context->passHistory, state->passHistory, PDK_AI_SEATS);
}

static int StrongAdjustment(const AiCandidate *candidate, const AiContext *context);

static AiMoveChoice ChooseRolloutMove(const Cards *hand, const AiContext *context,
                                      bool strongSelf)
{
    AiCandidateList candidates;
    AiMoveChoice choice;
    int planLimit;

    if (!strongSelf) {
        return BasicAiStrategy_ChooseMove(hand, context);
    }

    AiCandidateList_Init(&candidates);
    Ai_GenerateCandidates(hand, context, &candidates);
    if (candidates.count == 0) {
        AiCandidateList_Free(&candidates);
        return AiMoveChoice_MakePass(context->leading ? "rollout strong no lead"
                                                      : "rollout strong pass");
    }
    for (int i = 0; i < candidates.count; ++i) {
        candidates.items[i].score += StrongAdjustment(&candidates.items[i], context);
    }
    SortCandidates(candidates.items, candidates.count, RolloutCandidateLess);
    if (context->leading && context->currentPlayerIndex == context->roundLeaderIndex) {
        Ai_DeduplicateCandidates(&candidates);
    }
    planLimit = context->leading ? 10 : 8;
    if (planLimit > candidates.count) {
        planLimit = candidates.count;
    }
    for (int i = 0; i < planLimit; ++i) {
        AiCandidate *candidate = &candidates.items[i];

        if (!context->leading || candidate->remainder.count <= 12) {
            const int planBonus = LeadCountPlanBonus(&candidate->remainder);
            const int leadPlanWeight =
                context->currentPlayerIndex == context->roundLeaderIndex ? 3 : 5;

            candidate->score += context->leading ? planBonus * leadPlanWeight : planBonus;
        }
        if (!context->leading && context->currentTrickPassCount > 0) {
            candidate->score += SampledControlBonus(candidate, context, hand);
        }
    }
    SortCandidates(candidates.items, planLimit, RolloutCandidateLess);

    choice = AiMoveChoice_MakePlay(&candidates.items[0].cards, &candidates.items[0].pattern,
                                   "rollout strong",
                                   candidates.items[0].disruptionPenalty);
    AiCandidateList_Free(&candidates);
    return choice;
}

/*
 * `state` used to be taken by value and std::moved in; every caller discards its own copy
 * right afterwards, so mutating it in place is the same run of the rollout.
 */
static int RolloutWinner(RolloutState *state, int strongIndex)
{
    for (int turn = 0; turn < 240; ++turn) {
        Cards *hand = &state->hands[state->currentIndex];
        AiMoveChoice choice;
        AiContext rolloutContext;
        MoveValidation validation;
        int handSizeBefore;

        memset(&choice, 0, sizeof(choice));
        if (state->currentIndex == strongIndex) {
            RolloutContext(state, &rolloutContext);
            choice = ChooseRolloutMove(hand, &rolloutContext, true);
        } else {
            RolloutContext(state, &rolloutContext);
            choice = BasicAiStrategy_ChooseMove(hand, &rolloutContext);
        }
        if (choice.pass) {
            if (!state->hasLastPattern) {
                return -1;
            }
            RecordRolloutPass(state, state->currentIndex, &state->lastPattern);
            state->passCount++;
            if (state->passCount >= 2) {
                state->currentIndex = state->lastMoveIndex;
                state->hasLastPattern = false;
                state->trickLeaderIndex = state->currentIndex;
                state->passCount = 0;
            } else {
                state->currentIndex = NextIndex(state->currentIndex);
            }
            continue;
        }

        handSizeBefore = hand->count;
        validation = state->hasLastPattern
            ? ValidateFollow(&choice.cards, &state->lastPattern, handSizeBefore)
            : ValidateLead(&choice.cards, handSizeBefore);
        if (!validation.ok) {
            return -1;
        }
        if (!state->hasLastPattern) {
            state->trickLeaderIndex = state->currentIndex;
        }
        RemoveRolloutCards(hand, &choice.cards);
        Cards_Append(&state->playedCards, &choice.cards);
        state->lastPattern = validation.pattern;
        state->hasLastPattern = true;
        state->lastMoveIndex = state->currentIndex;
        state->passCount = 0;
        if (hand->count == 0) {
            return state->currentIndex;
        }
        state->currentIndex = NextIndex(state->currentIndex);
    }
    return -1;
}

/* ---- rollout enumeration ----------------------------------------------- */

static int CountMaskBits(int mask)
{
    int count = 0;

    while (mask != 0) {
        count += mask & 1;
        mask >>= 1;
    }
    return count;
}

typedef struct RolloutScoreContext {
    const AiCandidate *candidate;
    const AiContext *context;
    const Cards *unknown;
    int self;
    int next;
    int other;
    int nextCards;
    int n;
} RolloutScoreContext;

static int ScoreRolloutDistribution(const AiCandidate *candidate,
                                    const AiContext *context,
                                    int self, int next, int other,
                                    const Cards *nextHand, const Cards *otherHand)
{
    RolloutState state;
    int winner;
    int selfWinScore;

    memset(&state, 0, sizeof(state));
    state.hands[self] = candidate->remainder;
    state.hands[next] = *nextHand;
    state.hands[other] = *otherHand;
    state.currentIndex = next;
    state.lastMoveIndex = self;
    state.trickLeaderIndex = self;
    state.roundLeaderIndex = context->roundLeaderIndex;
    state.lastPattern = candidate->pattern;
    state.hasLastPattern = true;
    state.playedCards = context->playedCards;
    Cards_Append(&state.playedCards, &candidate->cards);
    PassObservations_Copy(state.passObservations, context->passObservations, PDK_AI_SEATS);
    PassHistories_Copy(state.passHistory, context->passHistory, PDK_AI_SEATS);

    winner = RolloutWinner(&state, self);
    selfWinScore = self == context->roundLeaderIndex ? 3800 : 2600;
    if (winner == self) {
        return selfWinScore;
    }
    if (winner >= 0) {
        return winner == other ? -700 : -2300;
    }
    return 0;
}

/* The C++ `scoreMask` lambda; it only ever writes through its two out parameters. */
static void ScoreRolloutMask(const RolloutScoreContext *scoreContext, int mask,
                             int *total, int *count)
{
    Cards nextHand;
    Cards otherHand;

    if (CountMaskBits(mask) != scoreContext->nextCards) {
        return;
    }

    Cards_Clear(&nextHand);
    Cards_Clear(&otherHand);
    for (int i = 0; i < scoreContext->n; ++i) {
        if ((mask & (1 << i)) != 0) {
            Cards_Push(&nextHand, scoreContext->unknown->items[i]);
        } else {
            Cards_Push(&otherHand, scoreContext->unknown->items[i]);
        }
    }

    if (!IsConsistentWithPassHistory(&nextHand, scoreContext->next, scoreContext->context) ||
        !IsConsistentWithPassHistory(&otherHand, scoreContext->other, scoreContext->context)) {
        return;
    }

    *total += ScoreRolloutDistribution(scoreContext->candidate, scoreContext->context,
                                       scoreContext->self, scoreContext->next,
                                       scoreContext->other, &nextHand, &otherHand);
    (*count)++;
}

typedef struct RolloutMaskTask {
    const RolloutScoreContext *scoreContext;
    volatile LONG *nextMask;
    volatile LONG *total;
    volatile LONG *count;
    int limit;
} RolloutMaskTask;

static unsigned __stdcall RolloutMaskWorker(void *parameter)
{
    RolloutMaskTask *task = (RolloutMaskTask *)parameter;
    int localTotal = 0;
    int localCount = 0;

    for (;;) {
        /* InterlockedIncrement returns the new value, so the old one is `- 1`; that is
         * exactly the old fetch_add(1). */
        const int mask = (int)InterlockedIncrement(task->nextMask) - 1;

        if (mask >= task->limit) {
            break;
        }
        ScoreRolloutMask(task->scoreContext, mask, &localTotal, &localCount);
    }
    InterlockedExchangeAdd(task->total, (LONG)localTotal);
    InterlockedExchangeAdd(task->count, (LONG)localCount);
    return 0;
}

/* False stands for the old std::nullopt. */
static bool EnumeratedRolloutBonus(const AiCandidate *candidate, const AiContext *context,
                                   const Cards *unknown, int self, int next, int other,
                                   int nextCards, bool parallelEvaluation, int *outScore)
{
    const int n = unknown->count;
    RolloutScoreContext scoreContext;
    int possible = 0;
    int limit;
    int total = 0;
    int count = 0;

    if (n > 14 || nextCards < 0 || nextCards > n) {
        return false;
    }

    limit = 1 << n;
    for (int mask = 0; mask < limit; ++mask) {
        if (CountMaskBits(mask) == nextCards) {
            possible++;
            if (possible > 4000) {
                return false;
            }
        }
    }

    scoreContext.candidate = candidate;
    scoreContext.context = context;
    scoreContext.unknown = unknown;
    scoreContext.self = self;
    scoreContext.next = next;
    scoreContext.other = other;
    scoreContext.nextCards = nextCards;
    scoreContext.n = n;

    if (parallelEvaluation) {
        volatile LONG nextMask = 0;
        volatile LONG parallelTotal = 0;
        volatile LONG parallelCount = 0;
        RolloutMaskTask task;
        HANDLE handles[PDK_STRONG_WORKERS];
        int handleCount;

        task.scoreContext = &scoreContext;
        task.nextMask = &nextMask;
        task.total = &parallelTotal;
        task.count = &parallelCount;
        task.limit = limit;

        handleCount = StartWorkers(handles, PDK_STRONG_WORKERS, RolloutMaskWorker, &task);
        RolloutMaskWorker(&task); /* the calling thread works too, like worker() did */
        JoinWorkers(handles, handleCount);
        total = (int)parallelTotal;
        count = (int)parallelCount;
    } else {
        for (int mask = 0; mask < limit; ++mask) {
            ScoreRolloutMask(&scoreContext, mask, &total, &count);
        }
    }

    if (count == 0) {
        return false;
    }
    *outScore = total / count;
    return true;
}

static bool ShouldUseExactRolloutEnumeration(int unknownCount, int nextCards)
{
    int combinations = 0;
    int limit;

    if (unknownCount > 12 || nextCards < 0 || nextCards > unknownCount) {
        return false;
    }

    limit = 1 << unknownCount;
    for (int mask = 0; mask < limit; ++mask) {
        if (CountMaskBits(mask) == nextCards) {
            combinations++;
            if (combinations > 1200) {
                return false;
            }
        }
    }
    return true;
}

typedef struct FastSampleContext {
    const AiCandidate *candidate;
    const AiContext *context;
    const Cards *unknown;
    const int *validAttemptIndices;
    int validCount;
    int nextCards;
    int otherCards;
    uint32_t seed;
    int self;
    int next;
    int other;
    int *sampleScores;
    unsigned char *sampleValid;
} FastSampleContext;

/* The C++ `runSample` lambda.  Each task index is unique, so the two array writes below
 * never touch the same element as another worker. */
static void RunFastSample(const FastSampleContext *sampleContext, int sample)
{
    Cards shuffled = *sampleContext->unknown;
    Cards nextHand;
    Cards otherHand;
    RolloutState state;
    int winner;
    int selfWinScore;

    ShuffleSample(&shuffled, MixSeed(sampleContext->seed, (uint32_t)(sample + 17)));
    Cards_Clear(&nextHand);
    Cards_Clear(&otherHand);
    for (int i = 0; i < sampleContext->nextCards; ++i) {
        Cards_Push(&nextHand, shuffled.items[i]);
    }
    for (int i = 0; i < sampleContext->otherCards; ++i) {
        Cards_Push(&otherHand, shuffled.items[sampleContext->nextCards + i]);
    }
    if (!IsConsistentWithPassHistory(&nextHand, sampleContext->next, sampleContext->context) ||
        !IsConsistentWithPassHistory(&otherHand, sampleContext->other, sampleContext->context)) {
        return;
    }
    sampleContext->sampleValid[sample] = 1;

    memset(&state, 0, sizeof(state));
    state.hands[sampleContext->self] = sampleContext->candidate->remainder;
    state.hands[sampleContext->next] = nextHand;
    state.hands[sampleContext->other] = otherHand;
    state.currentIndex = sampleContext->next;
    state.lastMoveIndex = sampleContext->self;
    state.trickLeaderIndex = sampleContext->self;
    state.roundLeaderIndex = sampleContext->context->roundLeaderIndex;
    state.lastPattern = sampleContext->candidate->pattern;
    state.hasLastPattern = true;
    state.playedCards = sampleContext->context->playedCards;
    Cards_Append(&state.playedCards, &sampleContext->candidate->cards);
    PassObservations_Copy(state.passObservations, sampleContext->context->passObservations,
                          PDK_AI_SEATS);
    PassHistories_Copy(state.passHistory, sampleContext->context->passHistory, PDK_AI_SEATS);

    winner = RolloutWinner(&state, sampleContext->self);
    selfWinScore = sampleContext->self == sampleContext->context->roundLeaderIndex ? 3800 : 2600;
    if (winner == sampleContext->self) {
        sampleContext->sampleScores[sample] = selfWinScore;
    } else if (winner >= 0) {
        sampleContext->sampleScores[sample] = winner == sampleContext->other ? -700 : -2300;
    }
}

typedef struct FastSampleTask {
    const FastSampleContext *sampleContext;
    volatile LONG *nextTask;
} FastSampleTask;

static unsigned __stdcall FastSampleWorker(void *parameter)
{
    FastSampleTask *task = (FastSampleTask *)parameter;

    for (;;) {
        const int index = (int)InterlockedIncrement(task->nextTask) - 1;

        if (index >= task->sampleContext->validCount) {
            return 0;
        }
        RunFastSample(task->sampleContext,
                      task->sampleContext->validAttemptIndices[index]);
    }
}

static int FastRolloutBonus(const AiCandidate *candidate, const AiContext *context,
                            const Cards *unknown, int sampleCount)
{
    FastSampleContext sampleContext;
    int validAttemptIndices[PDK_STRONG_SAMPLE_CAP];
    int sampleScores[PDK_STRONG_SAMPLE_CAP * 4];
    unsigned char sampleValid[PDK_STRONG_SAMPLE_CAP * 4];
    int attemptCount;
    int nextCards;
    int otherCards;
    int score = 0;
    int validSamples = 0;

    if (candidate->remainder.count > 10 && context->minOpponentRemainingCards > 8) {
        return 0;
    }
    /* The only two call sites pass 9 and 5; clamping keeps the fixed buffers safe if a
     * third one ever appears. */
    if (sampleCount < 0) {
        sampleCount = 0;
    }
    if (sampleCount > PDK_STRONG_SAMPLE_CAP) {
        sampleCount = PDK_STRONG_SAMPLE_CAP;
    }

    sampleContext.self = context->currentPlayerIndex;
    sampleContext.next = NextIndex(sampleContext.self);
    sampleContext.other = NextIndex(sampleContext.next);
    nextCards = context->remainingCards[sampleContext.next];
    otherCards = context->remainingCards[sampleContext.other];
    if (unknown->count != nextCards + otherCards) {
        return 0;
    }

    if (ShouldUseExactRolloutEnumeration(unknown->count, nextCards)) {
        int enumerated = 0;

        if (EnumeratedRolloutBonus(candidate, context, unknown, sampleContext.self,
                                   sampleContext.next, sampleContext.other, nextCards,
                                   sampleCount > 5, &enumerated)) {
            return enumerated;
        }
    }

    attemptCount = sampleCount * 4;
    memset(sampleScores, 0, sizeof(sampleScores));
    memset(sampleValid, 0, sizeof(sampleValid));

    sampleContext.candidate = candidate;
    sampleContext.context = context;
    sampleContext.unknown = unknown;
    sampleContext.validAttemptIndices = validAttemptIndices;
    sampleContext.validCount = 0;
    sampleContext.nextCards = nextCards;
    sampleContext.otherCards = otherCards;
    sampleContext.seed = CandidateSeed(candidate, context);
    sampleContext.sampleScores = sampleScores;
    sampleContext.sampleValid = sampleValid;

    for (int sample = 0;
         sample < attemptCount && sampleContext.validCount < sampleCount;
         ++sample) {
        Cards shuffled = *unknown;
        Cards nextHand;
        Cards otherHand;

        ShuffleSample(&shuffled, MixSeed(sampleContext.seed, (uint32_t)(sample + 17)));
        Cards_Clear(&nextHand);
        Cards_Clear(&otherHand);
        for (int i = 0; i < nextCards; ++i) {
            Cards_Push(&nextHand, shuffled.items[i]);
        }
        for (int i = 0; i < otherCards; ++i) {
            Cards_Push(&otherHand, shuffled.items[nextCards + i]);
        }
        if (IsConsistentWithPassHistory(&nextHand, sampleContext.next, context) &&
            IsConsistentWithPassHistory(&otherHand, sampleContext.other, context)) {
            validAttemptIndices[sampleContext.validCount++] = sample;
        }
    }

    if (sampleCount > 5) {
        volatile LONG nextTask = 0;
        FastSampleTask task;
        HANDLE handles[PDK_STRONG_WORKERS];
        int handleCount;

        task.sampleContext = &sampleContext;
        task.nextTask = &nextTask;

        handleCount = StartWorkers(handles, PDK_STRONG_WORKERS, FastSampleWorker, &task);
        FastSampleWorker(&task); /* the calling thread works too, like worker() did */
        JoinWorkers(handles, handleCount);
    } else {
        for (int i = 0; i < sampleContext.validCount; ++i) {
            RunFastSample(&sampleContext, validAttemptIndices[i]);
        }
    }

    for (int sample = 0; sample < attemptCount && validSamples < sampleCount; ++sample) {
        if (sampleValid[sample] == 0) {
            continue;
        }
        score += sampleScores[sample];
        validSamples++;
    }
    return validSamples > 0 ? score / validSamples : 0;
}

/* ---- strong adjustments ------------------------------------------------ */

static bool ShouldUseRollout(const AiCandidate *candidate, const AiContext *context)
{
    return candidate->remainder.count <= 10 || context->minOpponentRemainingCards <= 8;
}

static int StrongPostRolloutAdjustment(const AiCandidate *candidate,
                                      const AiContext *context)
{
    int score = -Ai_KickerControlPenalty(candidate) * 10;
    const bool anyOpponentSingle = context->minOpponentRemainingCards == 1;
    const bool singleBlocker = candidate->pattern.type == PATTERN_SINGLE &&
        (context->leading ||
         (!context->leading && context->previous.type == PATTERN_SINGLE));

    if (anyOpponentSingle && singleBlocker && candidate->remainder.count > 0) {
        score += RankValue(candidate->pattern.mainRank) * 180;
        if (candidate->pattern.mainRank >= RANK_KING) {
            score += 1100;
        } else if (candidate->pattern.mainRank <= RANK_TEN) {
            score -= 900;
        }
    }
    return score;
}

static bool IsPreferredEarlyLead(PatternType type)
{
    return type == PATTERN_STRAIGHT ||
           type == PATTERN_CONSECUTIVE_PAIRS ||
           type == PATTERN_TRIPLE_WITH_PAIR ||
           type == PATTERN_PLANE;
}

static int StrongAdjustment(const AiCandidate *candidate, const AiContext *context)
{
    int score = 0;

    if (context->leading && candidate->pattern.type == PATTERN_BOMB &&
        candidate->remainder.count > 0 && candidate->remainder.count <= 8) {
        score += 1600 + LeadCountPlanBonus(&candidate->remainder);
    }

    if (context->leading &&
        context->currentPlayerIndex == context->roundLeaderIndex &&
        context->ownRemainingCards >= 13 &&
        candidate->pattern.cardCount >= 4 &&
        IsPreferredEarlyLead(candidate->pattern.type) &&
        candidate->remainder.count <= 11) {
        score += 950 + candidate->pattern.cardCount * 85;
    }

    if (!context->leading &&
        context->currentTrickPassCount > 0 &&
        candidate->remainder.count > 0) {
        const int pressure = UnknownPatternBeaterPressure(candidate, context);

        if (pressure == 0) {
            score += 950 + candidate->pattern.cardCount * 70;
        } else if (pressure <= 2 && candidate->remainder.count <= 8) {
            score += 280 + candidate->pattern.cardCount * 35;
        }
    }

    if (!context->leading &&
        context->previous.type == PATTERN_SINGLE &&
        candidate->pattern.type == PATTERN_SINGLE &&
        context->ownRemainingCards > 10 &&
        context->minOpponentRemainingCards > 5 &&
        RankValue(context->previous.mainRank) <= RankValue(RANK_SEVEN) &&
        candidate->pattern.mainRank >= RANK_KING) {
        const int previousRank = RankValue(context->previous.mainRank);
        const int candidateRank = RankValue(candidate->pattern.mainRank);
        bool lowerBeaterExists = false;

        for (int i = 0; i < candidate->remainder.count; ++i) {
            const int rankValue = RankValue(candidate->remainder.items[i].rank);

            if (rankValue > previousRank && rankValue < candidateRank) {
                lowerBeaterExists = true;
                break;
            }
        }
        if (lowerBeaterExists) {
            score -= candidate->pattern.mainRank == RANK_TWO ? 1900 : 1150;
        }
    }

    if (!context->leading && candidate->remainder.count > 0 &&
        context->minOpponentRemainingCards > 2 &&
        (candidate->pattern.type == PATTERN_SINGLE ||
         candidate->pattern.type == PATTERN_PAIR)) {
        AiRankCounts playedCounts;
        AiRankCounts beforeCounts;
        Cards before;

        AiRankCounts_Build(&candidate->cards, &playedCounts);
        before = candidate->remainder;
        Cards_Append(&before, &candidate->cards);
        AiRankCounts_Build(&before, &beforeCounts);
        /* Ascending rank order over the ranks actually played, exactly like the old
         * std::map iteration. */
        for (Rank rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
            const int playedCount = playedCounts.count[rank];
            int beforeCount;

            if (playedCount == 0) {
                continue;
            }
            beforeCount = beforeCounts.count[rank];
            if (beforeCount >= 4 && playedCount < 4) {
                score -= 1700 + RankValue(rank) * 24;
            } else if (beforeCount == 3 && playedCount < 3) {
                score -= 1180 + RankValue(rank) * 24;
            } else if (beforeCount == 2 && playedCount == 1) {
                score -= 420 + RankValue(rank) * 12;
            }
        }
    }

    return score;
}

/* ---- candidate filtering ---------------------------------------------- */

static int PartialBombCardsUsed(const AiCandidate *candidate, const Cards *hand)
{
    AiRankCounts handCounts;
    AiRankCounts playedCounts;
    int used = 0;

    AiRankCounts_Build(hand, &handCounts);
    AiRankCounts_Build(&candidate->cards, &playedCounts);
    for (Rank rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
        const int played = playedCounts.count[rank];

        if (handCounts.count[rank] != 4 || played <= 0 || played >= 4) {
            continue;
        }
        if (played > used) {
            used = played;
        }
    }
    return used;
}

static void FilterStrongBombSplits(AiCandidateList *candidates, const Cards *hand,
                                   const AiContext *context)
{
    AiRankCounts handCounts;
    bool hasBomb = false;
    int write = 0;

    if (!context->leading || hand->count > 7) {
        return;
    }
    AiRankCounts_Build(hand, &handCounts);
    for (Rank rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
        if (handCounts.count[rank] == 4) {
            hasBomb = true;
            break;
        }
    }
    if (!hasBomb) {
        return;
    }

    /* std::remove_if + erase, written as a compaction pass. */
    for (int i = 0; i < candidates->count; ++i) {
        const int used = PartialBombCardsUsed(&candidates->items[i], hand);
        bool remove = false;

        if (used != 0 && candidates->items[i].remainder.count > 0) {
            remove = used >= 3;
        }
        if (!remove) {
            candidates->items[write++] = candidates->items[i];
        }
    }
    candidates->count = write;
}

static void FilterStrongMidgameSingleSplits(AiCandidateList *candidates, const Cards *hand,
                                            const AiContext *context)
{
    AiRankCounts handCounts;
    bool hasSingleton = false;
    int write = 0;

    if (!context->leading || hand->count < 5 || hand->count > 8) {
        return;
    }
    AiRankCounts_Build(hand, &handCounts);
    for (Rank rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
        if (handCounts.count[rank] == 1) {
            hasSingleton = true;
            break;
        }
    }
    if (!hasSingleton) {
        return;
    }

    for (int i = 0; i < candidates->count; ++i) {
        const AiCandidate *candidate = &candidates->items[i];
        bool remove = false;

        if (candidate->pattern.type == PATTERN_SINGLE && candidate->cards.count > 0) {
            remove = handCounts.count[candidate->cards.items[0].rank] >= 2;
        }
        if (!remove) {
            candidates->items[write++] = candidates->items[i];
        }
    }
    candidates->count = write;
}

static void PromotePreferredLeadTypes(AiCandidateList *candidates, int limit)
{
    static const PatternType kPreferred[4] = {
        PATTERN_STRAIGHT,
        PATTERN_CONSECUTIVE_PAIRS,
        PATTERN_TRIPLE_WITH_PAIR,
        PATTERN_PLANE
    };
    int replace;

    if (limit <= 0 || candidates->count <= limit) {
        return;
    }
    replace = limit - 1;
    for (int p = 0; p < 4; ++p) {
        const PatternType type = kPreferred[p];
        int found = -1;
        bool alreadyPresent = false;

        for (int i = 0; i < limit; ++i) {
            if (candidates->items[i].pattern.type == type) {
                alreadyPresent = true;
                break;
            }
        }
        if (alreadyPresent) {
            continue;
        }
        for (int i = limit; i < candidates->count; ++i) {
            if (candidates->items[i].pattern.type == type) {
                found = i;
                break;
            }
        }
        if (found >= 0 && replace >= 0) {
            const AiCandidate held = candidates->items[replace];

            candidates->items[replace] = candidates->items[found];
            candidates->items[found] = held;
            --replace;
        }
    }
}

/* ---- the strategy ------------------------------------------------------ */

typedef struct UnknownCache {
    Cards cards;
    bool has;
} UnknownCache;

static const Cards *UnknownCache_Get(UnknownCache *cache, const Cards *hand,
                                     const AiContext *context)
{
    if (!cache->has) {
        cache->cards = UnknownOpponentCards(hand, context);
        cache->has = true;
    }
    return &cache->cards;
}

static AiMoveChoice ChooseStrongMove(const Cards *hand, const AiContext *context)
{
    AiCandidateList candidates;
    AiMoveChoice choice;
    UnknownCache unknown;
    const AiCandidate *best;
    int planLimit;
    int rolloutLimit;
    int strongPlannerBestPlan;
    bool useStrongLeadPlanner;
    bool gaveUp = false;

    memset(&choice, 0, sizeof(choice));
    memset(&unknown, 0, sizeof(unknown));
    AiCandidateList_Init(&candidates);

    Ai_GenerateCandidates(hand, context, &candidates);
    if (candidates.count == 0) {
        gaveUp = true;
        goto cleanup;
    }

    for (int i = 0; i < candidates.count; ++i) {
        candidates.items[i].score += StrongAdjustment(&candidates.items[i], context);
    }

    SortCandidates(candidates.items, candidates.count, StrongCandidateLess);
    if (context->leading && context->currentPlayerIndex == context->roundLeaderIndex) {
        Ai_DeduplicateCandidates(&candidates);
    }
    FilterStrongBombSplits(&candidates, hand, context);
    FilterStrongMidgameSingleSplits(&candidates, hand, context);
    if (candidates.count == 0) {
        gaveUp = true;
        goto cleanup;
    }

    planLimit = context->leading ? 24 : 18;
    if (planLimit > candidates.count) {
        planLimit = candidates.count;
    }
    if (context->leading && context->ownRemainingCards >= 13) {
        PromotePreferredLeadTypes(&candidates, planLimit);
    }
    for (int i = 0; i < planLimit; ++i) {
        AiCandidate *candidate = &candidates.items[i];

        if (!context->leading || candidate->remainder.count <= 12) {
            const int planBonus = LeadCountPlanBonus(&candidate->remainder);
            const bool strongEarlyLead =
                context->currentPlayerIndex == context->roundLeaderIndex &&
                context->ownRemainingCards >= 13;
            const int leadPlanWeight = strongEarlyLead
                ? 5
                : (context->currentPlayerIndex == context->roundLeaderIndex ? 3 : 5);

            candidate->score += context->leading ? planBonus * leadPlanWeight : planBonus;
        }
        if (!context->leading && context->currentTrickPassCount > 0 &&
            candidate->remainder.count <= 10) {
            candidate->score += CachedUnknownSampledControlBonus(
                candidate, context, UnknownCache_Get(&unknown, hand, context));
        }
        if (!context->leading && candidate->remainder.count <= 8) {
            candidate->score += RemainderFinishSafetyBonus(candidate, context) / 2;
        }
    }
    SortCandidates(candidates.items, planLimit, StrongCandidateLess);

    useStrongLeadPlanner = context->leading && context->ownRemainingCards >= 9;
    rolloutLimit = context->leading ? 8 : 4;
    if (rolloutLimit > candidates.count) {
        rolloutLimit = candidates.count;
    }
    strongPlannerBestPlan = 99;
    if (useStrongLeadPlanner) {
        for (int i = 0; i < rolloutLimit; ++i) {
            const AiCandidate *candidate = &candidates.items[i];
            const int plan = candidate->remainder.count <= 12
                ? MinimumLeadCount(&candidate->remainder)
                : 99;

            if (plan < strongPlannerBestPlan) {
                strongPlannerBestPlan = plan;
            }
        }
    }
    for (int i = 0; i < rolloutLimit; ++i) {
        AiCandidate *candidate = &candidates.items[i];
        bool replacedByRollout = false;
        bool useFollowRollout;
        int plannerCandidatePlan;
        bool canWinStrongPlanner;

        if (candidate->remainder.count == 0) {
            continue;
        }
        useFollowRollout = !context->leading &&
            context->currentTrickPassCount > 0 &&
            candidate->remainder.count <= 8;
        plannerCandidatePlan = useStrongLeadPlanner && candidate->remainder.count <= 12
            ? MinimumLeadCount(&candidate->remainder)
            : 99;
        canWinStrongPlanner = !useStrongLeadPlanner ||
            plannerCandidatePlan == strongPlannerBestPlan;
        if (canWinStrongPlanner &&
            (context->leading ? ShouldUseRollout(candidate, context) : useFollowRollout)) {
            const int rolloutBonus = FastRolloutBonus(
                candidate, context, UnknownCache_Get(&unknown, hand, context),
                useStrongLeadPlanner ? 9 : 5);

            candidate->score = rolloutBonus * 24 + candidate->score / 120;
            replacedByRollout = true;
        }
        candidate->score += StrongPostRolloutAdjustment(candidate, context);
        if (replacedByRollout && hand->count <= 7 &&
            PartialBombCardsUsed(candidate, hand) > 0) {
            candidate->score -= candidate->disruptionPenalty * 2;
        }
        if (replacedByRollout &&
            context->leading &&
            context->currentPlayerIndex == context->roundLeaderIndex &&
            context->ownRemainingCards >= 13) {
            candidate->score += LeadCountPlanBonus(&candidate->remainder) * 3;
        }
        if (!context->leading && candidate->remainder.count > 0 &&
            context->ownRemainingCards <= 4 &&
            context->minOpponentRemainingCards > 4 &&
            (candidate->pattern.type == PATTERN_SINGLE ||
             candidate->pattern.type == PATTERN_PAIR)) {
            candidate->score -= RankValue(candidate->pattern.mainRank) * 30;
        }
    }
    SortCandidates(candidates.items, rolloutLimit, StrongCandidateLess);

    best = &candidates.items[0];
    if (context->leading && context->ownRemainingCards >= 9) {
        const int plannerLimit = candidates.count < 8 ? candidates.count : 8;
        long long bestSelectionScore = -0x7fffffffffffffffLL;

        for (int i = 0; i < plannerLimit; ++i) {
            const AiCandidate *candidate = &candidates.items[i];
            const int candidatePlan = candidate->remainder.count <= 12
                ? MinimumLeadCount(&candidate->remainder)
                : 99;
            long long selectionScore;

            if (candidatePlan != strongPlannerBestPlan) {
                continue;
            }
            selectionScore = (long long)candidate->score -
                (long long)candidatePlan * 20000LL +
                (long long)candidate->cards.count * 1000LL;
            if (selectionScore > bestSelectionScore) {
                best = candidate;
                bestSelectionScore = selectionScore;
            }
        }
    }

    {
        Str reason;

        Str_Init(&reason);
        Str_Append(&reason, "强 AI 推荐 ");
        Str_Append(&reason, PatternName(best->pattern.type));
        choice = AiMoveChoice_MakePlay(&best->cards, &best->pattern, Str_CStr(&reason),
                                       best->disruptionPenalty);
        Str_Free(&reason);
    }

cleanup:
    /* The single release point for the candidate list, on both the early exits and the
     * success path. */
    AiCandidateList_Free(&candidates);
    if (gaveUp) {
        return AiMoveChoice_MakePass(context->leading ? "强 AI 没有可出的牌型"
                                                      : "强 AI 压牌失败");
    }
    return choice;
}

AiMoveChoice StrongAiStrategy_ChooseMove(const Cards *hand, const AiContext *context)
{
    return ChooseStrongMove(hand, context);
}
