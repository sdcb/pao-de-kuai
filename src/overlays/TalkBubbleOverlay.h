#pragma once

/*
 * The AI's speech bubble: a paper-coloured rounded plate under the speaker's name plate, with a
 * tail pointing at their avatar.  Pure C.  It answers the Expired slot (three seconds).
 */

#include "core/Overlay.h"
#include "rules/Card.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns an owning handle; a null vtable if the allocation failed. */
Overlay TalkBubbleOverlay_New(PlayerId player, const char *text);

#ifdef __cplusplus
}
#endif
