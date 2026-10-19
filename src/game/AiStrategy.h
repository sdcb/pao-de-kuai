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

/*
 * Every tunable of the strong strategy in one place.  The game always plays with
 * StrongAiConfig_Default(); the tests build other configurations to compare a candidate
 * against a frozen earlier version (StrongAiConfig_V22) in the same process.
 */
typedef struct StrongAiConfig {
    /* Rollout outcome scoring (see RolloutOutcomeScore). */
    bool teamPlay;           /* the two AI seats play together against the human */
    int teamWinScore;        /* score of the teammate going out */
    int pointWeight;         /* weight of the round points in a rollout outcome */
    /* Heuristics. */
    bool endgameDefense;     /* leads a 1-2 card threat cannot answer, feeding a teammate */
    bool bombSplitPenalty;
    /* Search budget. */
    int leadSamples;         /* rollout samples when the lead planner is active */
    int otherSamples;        /* rollout samples otherwise */
    int leadRolloutLimit;    /* candidates that get a rollout when leading */
    int followRolloutLimit;  /* ... and when following */
    int leadRolloutRemainder;     /* lead rollouts once our remainder is at most this */
    int leadRolloutOpponentCards; /* ... or an opponent is down to at most this */
    int followRolloutRemainder;   /* follow rollouts once our remainder is at most this */
    /* Who plays the strong rollout policy inside a rollout: 0 only the deciding seat
     * (others play basic), 1 also its teammate, 2 every seat. */
    int rolloutPolicy;
    /* Exact endgame search (EndgameSolver) in place of the rollout once every hand is
     * small: used when the three hands hold at most solverMaxCards cards after the
     * candidate, with solverSamples deals of the unknown cards, solverNodes nodes per
     * deal (a deal that needs more falls back to the rollout), and the human seat's
     * points scaled by solverScale into rollout-score units. */
    bool endgameSolver;
    int solverMaxCards;
    int solverSamples;
    int solverNodes;
    int solverScale;
    /* Leading a pair that could have joined a run of pairs costs this much (0 = off). */
    int pairRunPenalty;
    /* Leading a low single to a next-seat teammate who has not played yet, so it can
     * escape the 32-point spring, earns this much (0 = off). */
    int springRescueBonus;
} StrongAiConfig;

StrongAiConfig StrongAiConfig_Default(void);
/* StrongAI 2.2 exactly, kept as the reference opponent for tuning. */
StrongAiConfig StrongAiConfig_V22(void);

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
    /* Strong strategy tuning; NULL means StrongAiConfig_Default().  Only the strong
     * strategy reads it. */
    const StrongAiConfig *strongConfig;

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
/* Same, with an explicit configuration instead of context->strongConfig. */
AiMoveChoice StrongAiStrategy_ChooseMoveWithConfig(const Cards *hand, const AiContext *context,
                                                   const StrongAiConfig *config);
StrategyMetadata StrongAiStrategy_Metadata(void);

#ifdef __cplusplus
}
#endif
