#pragma once

/*
 * The pluggable "who plays this seat" interface.
 *
 * Pure C.  The abstract class became the usual (vtable, user) pair, and the two
 * std::optional returns became a bool plus an out parameter.
 *
 * Two dead fields were dropped rather than given fixed-size replacements:
 * `ExternalAiRequest::humanName` and `::history` were written by GameState on every
 * request (history by copying the whole turn-record vector) and never read by any
 * implementation, including the test double.  Dropping them removes a per-turn vector
 * copy and keeps an ExternalAiRequest at a fixed ~4 KB.
 *
 * Ownership: the vtable's Destroy slot frees the implementation object.  GameState owns
 * the controllers it is handed and destroys them.  A copied ExternalAiController value
 * is a *borrow* of the same (vtable, user) pair -- which is exactly why the interface is
 * a small copyable struct instead of a handle with a reference count.
 */

#include "game/AiStrategy.h"
#include "game/TurnRecord.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PDK_AI_ERROR_CAP = 192 };

typedef struct ExternalAiRequest {
    int turnNo;
    PlayerId player;
    TurnSnapshot snapshot;
    AiContext context;
} ExternalAiRequest;

typedef struct ExternalAiResult {
    bool ok;
    char errorMessage[PDK_AI_ERROR_CAP];
    TurnDecisionSource source;
    AiMoveChoice localChoice;
    /* Replaces the old std::optional<AiMoveChoice>. */
    bool hasLocalChoice;
} ExternalAiResult;

typedef struct ExternalAiController ExternalAiController;

typedef struct ExternalAiControllerVtbl {
    bool (*CanHandle)(void *user, PlayerId player);
    /* Defaults to "unknown" when the implementation has nothing to say. */
    StrategyMetadata (*MetadataFor)(void *user, PlayerId player);
    bool (*HasPending)(void *user);
    void (*Start)(void *user, const ExternalAiRequest *request);
    /* Writes the finished result into `out` and returns true when one is ready. */
    bool (*TryGetResult)(void *user, ExternalAiResult *out);
    void (*Cancel)(void *user);
    /* NULL leaves the implementation alive, which only makes sense for static ones. */
    void (*Destroy)(void *user);
} ExternalAiControllerVtbl;

struct ExternalAiController {
    const ExternalAiControllerVtbl *vtbl;
    void *user;
};

static inline bool ExternalAiController_CanHandle(const ExternalAiController *controller,
                                                 PlayerId player)
{
    return controller->vtbl->CanHandle(controller->user, player);
}

static inline StrategyMetadata ExternalAiController_MetadataFor(
    const ExternalAiController *controller, PlayerId player)
{
    return controller->vtbl->MetadataFor(controller->user, player);
}

static inline bool ExternalAiController_HasPending(const ExternalAiController *controller)
{
    return controller->vtbl->HasPending(controller->user);
}

static inline void ExternalAiController_Start(const ExternalAiController *controller,
                                             const ExternalAiRequest *request)
{
    controller->vtbl->Start(controller->user, request);
}

static inline bool ExternalAiController_TryGetResult(const ExternalAiController *controller,
                                                    ExternalAiResult *out)
{
    return controller->vtbl->TryGetResult(controller->user, out);
}

static inline void ExternalAiController_Cancel(const ExternalAiController *controller)
{
    controller->vtbl->Cancel(controller->user);
}

static inline bool ExternalAiController_IsNull(const ExternalAiController *controller)
{
    return controller->vtbl == NULL;
}

/* Calls the Destroy slot when there is one, then clears the interface. */
void ExternalAiController_Destroy(ExternalAiController *controller);

#ifdef __cplusplus
}
#endif
