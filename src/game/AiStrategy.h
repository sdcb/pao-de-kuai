#pragma once

/*
 * AI strategy input/output and the strategy interface.
 *
 * Pure C.  Three things needed care here:
 *   - the `reason` text is a fixed buffer, like every other turn text;
 *   - `optional<PassObservation>` became a value plus a `has` flag, and the per-player
 *     `vector<PassObservation>` history a fixed array plus a count (bounded by the
 *     number of tricks a player can pass in);
 *   - the virtual base class became the usual (vtable, user) pair, so a strategy can
 *     still be injected -- the tests do exactly that with a brute-force one.
 *
 * The built-ins need no instance at all: their bodies are free functions and the
 * vtable instances simply point at them, which removes the per-request
 * make_unique<AiStrategy> the async controller used to perform.
 *
 * TEMPORARY: the `#ifdef __cplusplus` default constructor on AiContext keeps the
 * not-yet-ported C++ callers working, since their `AiContext context;` relied on the
 * old defaulted member initialisers (`leading` in particular defaulted to true).  It
 * goes away with src/game/CppCompat.h.
 */

#include "core/Str.h"
#include "game/StrategyMetadata.h"
#include "rules/MoveValidator.h"

#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PDK_AI_REASON_CAP = 128 };
enum { PDK_AI_SEATS = 3 };
/* A player passes at most once per trick and a round holds at most 48 cards, so this
 * cannot overflow in practice; the helper drops extras rather than growing. */
enum { PDK_PASS_HISTORY_MAX = 64 };

typedef struct AiMoveChoice {
    bool pass;
    Cards cards;
    HandPattern pattern;
    char reason[PDK_AI_REASON_CAP];
    /* Strategy metadata for flavor text; gameplay validation still uses cards/pattern. */
    int disruptionPenalty;
} AiMoveChoice;

/* Reliable because this game enforces "must play if you can beat it"; a legal pass
 * proves the player could not beat this pattern at that time. */
typedef struct PassObservation {
    HandPattern pattern;
    int remainingCards;
} PassObservation;

typedef struct OptionalPassObservation {
    PassObservation value;
    bool has;
} OptionalPassObservation;

typedef struct PassHistory {
    PassObservation items[PDK_PASS_HISTORY_MAX];
    int count;
} PassHistory;

static inline void PassObservations_Clear(OptionalPassObservation *observations, int count)
{
    for (int i = 0; i < count; ++i) {
        observations[i].has = false;
    }
}

static inline void PassHistory_Clear(PassHistory *history)
{
    history->count = 0;
}

static inline bool PassHistory_Add(PassHistory *history, const PassObservation *observation)
{
    if (history->count >= PDK_PASS_HISTORY_MAX) {
        return false;
    }
    history->items[history->count++] = *observation;
    return true;
}

static inline void PassObservations_Copy(OptionalPassObservation *dst,
                                        const OptionalPassObservation *src, int count)
{
    for (int i = 0; i < count; ++i) {
        dst[i] = src[i];
    }
}

static inline void PassHistories_Copy(PassHistory *dst, const PassHistory *src, int count)
{
    for (int i = 0; i < count; ++i) {
        dst[i] = src[i];
    }
}

static inline void OptionalPassObservation_Set(OptionalPassObservation *slot,
                                              const PassObservation *observation)
{
    slot->value = *observation;
    slot->has = true;
}

/*
 * Builders for the two shapes an AiMoveChoice is ever created in.  They exist because
 * a char[] member cannot be aggregate-initialised from a computed `const char*` (only
 * from a literal), which every "pass with a reason" site needed.
 */
static inline AiMoveChoice AiMoveChoice_MakePass(const char *reason)
{
    AiMoveChoice choice;
    memset(&choice, 0, sizeof(choice));
    choice.pass = true;
    Str_CopyTo(choice.reason, PDK_AI_REASON_CAP, reason);
    return choice;
}

static inline AiMoveChoice AiMoveChoice_MakePlay(const Cards *cards, const HandPattern *pattern,
                                                const char *reason, int disruptionPenalty)
{
    AiMoveChoice choice;
    memset(&choice, 0, sizeof(choice));
    choice.pass = false;
    choice.cards = *cards;
    choice.pattern = *pattern;
    choice.disruptionPenalty = disruptionPenalty;
    Str_CopyTo(choice.reason, PDK_AI_REASON_CAP, reason);
    return choice;
}

typedef struct AiContext {
    bool leading;
    HandPattern previous;
    int ownRemainingCards;
    int currentPlayerIndex;
    int lastMovePlayerIndex;
    int trickLeaderIndex;
    int roundLeaderIndex;
    int currentTrickPassCount;
    int nextPlayerRemainingCards;
    int minOpponentRemainingCards;
    int remainingCards[PDK_AI_SEATS];
    Cards playedCards;
    OptionalPassObservation passObservations[PDK_AI_SEATS];
    PassHistory passHistory[PDK_AI_SEATS];

#ifdef __cplusplus
    /* ---- TEMPORARY C++ shim: delete with src/game/CppCompat.h ---- */
    AiContext()
    {
        memset(this, 0, sizeof(*this));
        leading = true; /* the old member initialiser was `bool leading{true}` */
    }
#endif
} AiContext;

/* ---- the strategy interface ------------------------------------------ */

typedef struct AiStrategy AiStrategy;

typedef struct AiStrategyVtbl {
    AiMoveChoice (*ChooseMove)(void *user, const Cards *hand, const AiContext *context);
    StrategyMetadata (*Metadata)(void *user);
    /* NULL for the built-in singletons, which own nothing. */
    void (*Destroy)(void *user);
} AiStrategyVtbl;

struct AiStrategy {
    const AiStrategyVtbl *vtbl;
    void *user;
};

static inline AiMoveChoice AiStrategy_ChooseMove(AiStrategy *strategy, const Cards *hand,
                                                const AiContext *context)
{
    return strategy->vtbl->ChooseMove(strategy->user, hand, context);
}

static inline StrategyMetadata AiStrategy_Metadata(const AiStrategy *strategy)
{
    return strategy->vtbl->Metadata(strategy->user);
}

/* Frees the injected instance when the vtable has a Destroy slot; the built-ins leave
 * it NULL, so this tolerates them. */
void AiStrategy_Release(AiStrategy *strategy);

/* ---- the built-in strategies ----------------------------------------- */

AiStrategy BasicAiStrategy_Instance(void);
AiStrategy StrongAiStrategy_Instance(void);

AiMoveChoice BasicAiStrategy_ChooseMove(const Cards *hand, const AiContext *context);
StrategyMetadata BasicAiStrategy_Metadata(void);
AiMoveChoice StrongAiStrategy_ChooseMove(const Cards *hand, const AiContext *context);
StrategyMetadata StrongAiStrategy_Metadata(void);

#ifdef __cplusplus
}
#endif
