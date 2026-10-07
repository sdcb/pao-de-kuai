#pragma once

/*
 * The stats page: today, this month and all-time, one tall card per period, aggregated from the
 * per-round records on disk.  Pure C.
 *
 * `app` is an opaque App* (see app/App.h).  The returned handle owns its state and frees it
 * through the scene vtable's Destroy slot.
 */

#include "core/Scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns an owning handle. */
Scene StatsScene_New(void *app);

#ifdef __cplusplus
}
#endif
