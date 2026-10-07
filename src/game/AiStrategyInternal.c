#include "game/AiStrategyInternal.h"

#include "core/Str.h"

#include <stdlib.h>
#include <string.h>

void AiRankCounts_Build(const Cards *cards, AiRankCounts *out)
{
    AiRankCounts_Clear(out);
    for (int i = 0; i < cards->count; ++i) {
        const Rank rank = cards->items[i].rank;
        if (rank < PDK_RANK_SLOTS) {
            out->count[rank]++;
        }
    }
}

void AiCandidateList_Init(AiCandidateList *list)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void AiCandidateList_Free(AiCandidateList *list)
{
    free(list->items);
    AiCandidateList_Init(list);
}

bool AiCandidateList_Push(AiCandidateList *list, const AiCandidate *candidate)
{
    if (list->count >= list->capacity) {
        const int next = list->capacity > 0 ? list->capacity * 2 : 64;
        AiCandidate *grown =
            (AiCandidate *)realloc(list->items, sizeof(AiCandidate) * (size_t)next);

        if (grown == NULL) {
            return false;
        }
        list->items = grown;
        list->capacity = next;
    }
    list->items[list->count++] = *candidate;
    return true;
}

void AiMaskList_Init(AiMaskList *list)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void AiMaskList_Free(AiMaskList *list)
{
    free(list->items);
    AiMaskList_Init(list);
}

bool AiMaskList_Push(AiMaskList *list, uint64_t mask)
{
    if (list->count >= list->capacity) {
        const int next = list->capacity > 0 ? list->capacity * 2 : 256;
        uint64_t *grown = (uint64_t *)realloc(list->items, sizeof(uint64_t) * (size_t)next);

        if (grown == NULL) {
            return false;
        }
        list->items = grown;
        list->capacity = next;
    }
    list->items[list->count++] = mask;
    return true;
}

/* FNV-1a.  Only used to skip most of the comparisons in AiKeySet_Add. */
static unsigned AiKeyHash(const char *key)
{
    unsigned hash = 2166136261u;

    for (int i = 0; key[i] != '\0'; ++i) {
        hash ^= (unsigned char)key[i];
        hash *= 16777619u;
    }
    return hash;
}

bool AiKeySet_Init(AiKeySet *set, int capacity)
{
    set->keys = NULL;
    set->hashes = NULL;
    set->count = 0;
    set->capacity = capacity > 0 ? capacity : 1;

    set->keys = (char (*)[PDK_AI_KEY_CAP])malloc(sizeof(char[PDK_AI_KEY_CAP]) *
                                                (size_t)set->capacity);
    if (set->keys == NULL) {
        set->capacity = 0;
        return false;
    }
    set->hashes = (unsigned *)malloc(sizeof(unsigned) * (size_t)set->capacity);
    if (set->hashes == NULL) {
        free(set->keys);
        set->keys = NULL;
        set->capacity = 0;
        return false;
    }
    return true;
}

void AiKeySet_Free(AiKeySet *set)
{
    free(set->keys);
    free(set->hashes);
    set->keys = NULL;
    set->hashes = NULL;
    set->count = 0;
    set->capacity = 0;
}

bool AiKeySet_Add(AiKeySet *set, const char *key)
{
    const unsigned hash = AiKeyHash(key);

    for (int i = 0; i < set->count; ++i) {
        if (set->hashes[i] == hash && strcmp(set->keys[i], key) == 0) {
            return false;
        }
    }
    if (set->count >= set->capacity) {
        /* Out of room: report the key as new rather than dropping a candidate. */
        return true;
    }
    Str_CopyTo(set->keys[set->count], PDK_AI_KEY_CAP, key);
    set->hashes[set->count] = hash;
    set->count++;
    return true;
}
