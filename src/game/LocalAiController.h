#pragma once

/*
 * The in-process AI controller: runs the chosen local strategy on a worker thread so the
 * UI thread never blocks on the strong strategy's search.
 *
 * Pure C.  Two pieces of the old implementation needed real care:
 *   - `std::thread(...).detach()` with a captured `shared_ptr<SharedState>` kept the
 *     shared half alive after the controller died.  That is reproduced with an
 *     explicitly reference-counted LocalAiShared (see LocalAiController.c), because the
 *     controller can be destroyed while a search is still running.
 *   - the strategy table was a std::map<PlayerId, LocalAiKind>; there are only three
 *     seats, so it is a fixed array with a configured flag per seat.
 */

#include "game/ExternalAiController.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t LocalAiKind;

enum {
    LOCAL_AI_BASIC = 0,
    LOCAL_AI_STRONG = 1
};

typedef struct LocalAiController LocalAiController;

LocalAiController *LocalAiController_Create(void);

void LocalAiController_SetStrategy(LocalAiController *controller, PlayerId player, LocalAiKind kind);

/*
 * The ExternalAiController interface over this object.  Ownership of the interface --
 * and therefore of the LocalAiController -- passes to whoever is given it, so call
 * SetStrategy before handing it to GameState and do not free the controller afterwards.
 */
ExternalAiController LocalAiController_Interface(LocalAiController *controller);

#ifdef __cplusplus
}
#endif
