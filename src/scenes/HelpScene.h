#pragma once

/*
 * The rules page: the shared game rules, the scoring/托管 notes and a small gallery of example
 * hands drawn with the real card faces.  Pure C.
 *
 * `app` is an opaque App* (see app/App.h).  The returned handle owns its state and frees it
 * through the scene vtable's Destroy slot.
 */

#include "core/Scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns an owning handle. */
Scene HelpScene_New(void *app);

#ifdef __cplusplus
}
#endif
