#pragma once

/*
 * "Back to the main menu?" modal, pushed when the player leaves a round.  Pure C.
 */

#include "core/Overlay.h"

#ifdef __cplusplus
extern "C" {
#endif

/* `app` is an opaque App* (see app/AppApi.h).  Returns an owning handle. */
Overlay ReturnToMenuOverlay_New(void *app);

#ifdef __cplusplus
}
#endif
