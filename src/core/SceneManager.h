#pragma once

/*
 * Owns the current scene and swaps it, firing OnExit on the old one and OnEnter on the new one.
 *
 * Pure C and header-only.  The C++ version held a std::unique_ptr<Scene>; here the manager owns
 * the C handle instead, so it works for a converted C scene and for a C++ scene bridged through
 * core/CppCompat.h alike.  Ownership is explicit: Change takes the new scene's handle (releasing
 * whatever it replaced) and SceneManager_Release drops the last one.
 */

#include "core/Scene.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SceneManager SceneManager;

static inline void SceneManager_Init(SceneManager *manager);

struct SceneManager {
    Scene scene;
    bool has;

#ifdef __cplusplus
    /* ---- TEMPORARY C++ shim: delete with src/core/CppCompat.h ---- */
    SceneManager() { SceneManager_Init(this); }
#endif
};

static inline void SceneManager_Init(SceneManager *manager)
{
    manager->scene.vtbl = NULL;
    manager->scene.user = NULL;
    manager->has = false;
}

static inline void SceneManager_Release(SceneManager *manager)
{
    if (manager->has) {
        Scene_OnExit(&manager->scene);
        Scene_Release(&manager->scene);
        manager->has = false;
    }
}

/* Takes ownership of `scene`; a null handle just clears the current scene. */
static inline void SceneManager_Change(SceneManager *manager, Scene scene)
{
    SceneManager_Release(manager);
    manager->scene = scene;
    manager->has = scene.vtbl != NULL;
    if (manager->has) {
        Scene_OnEnter(&manager->scene);
    }
}

/* NULL when there is no scene. */
static inline Scene *SceneManager_Current(SceneManager *manager)
{
    return manager->has ? &manager->scene : NULL;
}

#ifdef __cplusplus
}
#endif
