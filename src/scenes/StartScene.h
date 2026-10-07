#pragma once

/*
 * The main menu: the title block with a fanned straight, the six-button menu, and the parallax
 * halo behind it all.  Pure C.
 *
 * `app` is an opaque App* (see app/AppApi.h).  The returned handle owns its state and frees it
 * through the scene vtable's Destroy slot.
 */

#include "core/Scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns an owning handle. */
Scene StartScene_New(void *app);

#ifdef __cplusplus
}
#endif
