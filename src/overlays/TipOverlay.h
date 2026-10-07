#pragma once

/*
 * The hint toast: a dark capsule with a gold sparkle, shown when the player asks for a tip.
 * Clicking anywhere dismisses it (OnMouseDown closes it and returns false, i.e. the click still
 * reaches the scene).  Pure C.
 */

#include "core/Overlay.h"

#ifdef __cplusplus
extern "C" {
#endif

/* `app` is an opaque App* (see app/App.h).  Returns an owning handle. */
Overlay TipOverlay_New(void *app, const char *text);

#ifdef __cplusplus
}
#endif
