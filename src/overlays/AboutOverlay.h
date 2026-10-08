#pragma once

/*
 * The about panel: the seal, the credits and the three link/tech/licence sections.  Pure C.
 */

#include "core/Overlay.h"

#ifdef __cplusplus
extern "C" {
#endif

/* `app` is an opaque App* (see app/App.h).  Returns an owning handle. */
Overlay AboutOverlay_New(void *app);

#ifdef __cplusplus
}
#endif
