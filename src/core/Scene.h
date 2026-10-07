#pragma once

/*
 * The scene interface: what a full-screen app screen must provide.
 *
 * Pure C.  The abstract class became the usual (vtable, user) pair -- the same shape
 * AiStrategyVtbl and ExternalAiControllerVtbl already use -- so the 12 not-yet-converted C++
 * scenes keep compiling through core/CppCompat.h's SceneClass bridge, and a converted scene
 * implements the table directly.
 *
 * One slot exists beyond the original virtuals:
 *   - `RestartRound` replaces `App::RestartCurrentGame`'s `dynamic_cast<scenes::GameScene*>`,
 *     because a C vtable carries no RTTI.  The base returns false, so "the current scene is not a
 *     game" is the default rather than a special case; that is strictly cleaner than the cast.
 *
 * The lifecycle calls are optional (NULL is a valid no-op) except Update and Render, which were
 * pure virtual.  The inline wrappers hide the NULL checks so a caller never has to know.
 *
 * RenderContext is only forward declared: this header is included by the C++ app shell and by
 * every scene, so it must stay cheap and must not pull in the Direct2D shim.
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RenderContext RenderContext;
typedef struct Scene Scene;

typedef struct SceneVtbl {
    void (*OnEnter)(void *user);
    void (*OnExit)(void *user);
    void (*Update)(void *user, float dt);
    void (*Render)(void *user, RenderContext *context);
    bool (*OnMouseMove)(void *user, float x, float y);
    bool (*OnMouseDown)(void *user, float x, float y);
    bool (*OnMouseUp)(void *user, float x, float y);
    void (*OnD2DResourcesLost)(void *user);
    void (*OnD2DResourcesRecreated)(void *user);
    /* True when this scene handled a "restart the current round" request. */
    bool (*RestartRound)(void *user);
    /* NULL for an implementation that owns nothing to free. */
    void (*Destroy)(void *user);
} SceneVtbl;

struct Scene {
    const SceneVtbl *vtbl;
    void *user;
};

static inline bool Scene_IsNull(const Scene *scene)
{
    return scene->vtbl == NULL;
}

static inline void Scene_OnEnter(Scene *scene)
{
    if (scene->vtbl->OnEnter != NULL) {
        scene->vtbl->OnEnter(scene->user);
    }
}

static inline void Scene_OnExit(Scene *scene)
{
    if (scene->vtbl->OnExit != NULL) {
        scene->vtbl->OnExit(scene->user);
    }
}

static inline void Scene_Update(Scene *scene, float dt)
{
    scene->vtbl->Update(scene->user, dt);
}

static inline void Scene_Render(Scene *scene, RenderContext *context)
{
    scene->vtbl->Render(scene->user, context);
}

static inline bool Scene_OnMouseMove(Scene *scene, float x, float y)
{
    return scene->vtbl->OnMouseMove != NULL ? scene->vtbl->OnMouseMove(scene->user, x, y) : false;
}

static inline bool Scene_OnMouseDown(Scene *scene, float x, float y)
{
    return scene->vtbl->OnMouseDown != NULL ? scene->vtbl->OnMouseDown(scene->user, x, y) : false;
}

static inline bool Scene_OnMouseUp(Scene *scene, float x, float y)
{
    return scene->vtbl->OnMouseUp != NULL ? scene->vtbl->OnMouseUp(scene->user, x, y) : false;
}

static inline void Scene_OnD2DResourcesLost(Scene *scene)
{
    if (scene->vtbl->OnD2DResourcesLost != NULL) {
        scene->vtbl->OnD2DResourcesLost(scene->user);
    }
}

static inline void Scene_OnD2DResourcesRecreated(Scene *scene)
{
    if (scene->vtbl->OnD2DResourcesRecreated != NULL) {
        scene->vtbl->OnD2DResourcesRecreated(scene->user);
    }
}

static inline bool Scene_RestartRound(Scene *scene)
{
    return scene->vtbl->RestartRound != NULL ? scene->vtbl->RestartRound(scene->user) : false;
}

/* Frees the implementation when the vtable says how, then clears the handle. */
static inline void Scene_Release(Scene *scene)
{
    if (scene->vtbl != NULL && scene->vtbl->Destroy != NULL) {
        scene->vtbl->Destroy(scene->user);
    }
    scene->vtbl = NULL;
    scene->user = NULL;
}

#ifdef __cplusplus
}
#endif
