#pragma once

/*
 * The end-of-round result panel: the win/lose seal, the score line, the special-event badges and
 * one row per seat.  Pure C.
 */

#include "core/Overlay.h"
#include "stats/DailyStat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* `app` is an opaque App* (see app/AppApi.h).  The record is copied, so the caller keeps its own.
 * Returns an owning handle. */
Overlay RoundResultOverlay_New(void *app, const RoundRecord *record);

#ifdef __cplusplus
}
#endif
