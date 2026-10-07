#pragma once

/*
 * The settings modal: player name, master volume, the two seat strategies and the round-trace
 * switch, plus the save/cancel pair.  Pure C.
 *
 * It owns a draft copy of the settings, which is why it fills the most vtable slots of any
 * overlay -- it is the only one that takes text and IME input.  Enter saves, Esc cancels, and
 * cancelling restores the volume the app had when the modal opened.
 */

#include "core/Overlay.h"

#ifdef __cplusplus
extern "C" {
#endif

/* `app` is an opaque App* (see app/App.h).  Returns an owning handle. */
Overlay SettingsOverlay_New(void *app);

#ifdef __cplusplus
}
#endif
