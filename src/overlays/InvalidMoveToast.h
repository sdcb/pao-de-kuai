#pragma once

/*
 * "That combination cannot beat the table" toast: a cinnabar capsule that fades in, holds for two
 * seconds and lifts away.  Pure C; it is one of the two overlays that answer the vtable's Expired
 * slot, which is how App's per-frame sweep drops it (there used to be a dynamic_cast for that).
 *
 * The implementation is private, so the header only exposes the constructor.
 */

#include "core/Overlay.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns an owning handle; a null vtable if the allocation failed. */
Overlay InvalidMoveToast_New(const char *text);

#ifdef __cplusplus
}
#endif
