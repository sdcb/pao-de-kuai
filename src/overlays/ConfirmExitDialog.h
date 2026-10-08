#pragma once

/*
 * "Really quit?" modal: a danger button that exits and a secondary one that stays.  Pure C.
 */

#include "core/Overlay.h"

#ifdef __cplusplus
extern "C" {
#endif

/* `app` is an opaque App* (see app/App.h).  Returns an owning handle. */
Overlay ConfirmExitDialog_New(void *app);

#ifdef __cplusplus
}
#endif
