#pragma once

/*
 * The loading screen: the short "spreading out the table" pause before the game scene, and the
 * "gathering the record" pause before the stats scene.  Pure C.
 *
 * The C++ version constructed the next scene itself (`ChangeScene(make_unique<GameScene>(...))`);
 * a C translation unit cannot, so both exits go through App_EnterGame / App_EnterStats.
 *
 * `app` is an opaque App* (see app/App.h).  The returned handle owns its state and releases it
 * through the scene vtable's Destroy slot, exactly like the overlays.
 */

#include "core/Scene.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum LoadingTarget {
    LOADING_TARGET_GAME = 0,
    LOADING_TARGET_STATS = 1
} LoadingTarget;

/* Returns an owning handle. */
Scene LoadingScene_New(void *app, LoadingTarget target);

#ifdef __cplusplus
}
#endif
