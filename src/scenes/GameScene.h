#pragma once

/*
 * The game table: the deal animation, the three seats, the human hand, the played cards, the
 * action button row and the end-of-round hand-off to the result overlay.  Pure C.
 *
 * `app` is an opaque App* (see app/AppApi.h).  The returned handle owns its state -- including a
 * GameState by value, which is why Destroy calls GameState_Destroy -- and frees it through the
 * scene vtable's Destroy slot.  This is the only scene that answers the vtable's RestartRound
 * slot, which is what makes App::RestartCurrentGame restart a round instead of starting a game.
 */

#include "core/Scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * `mock` deals a fixed seed; `midgame` additionally skips the deal and scripts a couple of moves
 * (scene_viewer's `--mock midgame`).  C has no default arguments, so callers pass both.
 * Returns an owning handle.
 */
Scene GameScene_New(void *app, bool mock, bool midgame);

#ifdef __cplusplus
}
#endif
