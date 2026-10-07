#pragma once

/*
 * The overlay interface: what a modal layer stacked on top of the current scene must provide.
 *
 * Pure C, same (vtable, user) shape as core/Scene.h.  Two slots go beyond the original virtuals,
 * because a C vtable carries no RTTI and App.cpp had exactly two dynamic_casts:
 *   - `Expired` replaces `dynamic_cast<const overlays::InvalidMoveToast*>` /
 *     `<const overlays::TalkBubbleOverlay*>` followed by `->Expired()`.  NULL means "never
 *     expires", so App's per-frame sweep is a NULL check instead of a type test.
 *   - `Destroy` frees the implementation.
 *
 * The original virtuals each had a do-nothing default, so all of them are optional here and the
 * inline wrappers absorb the NULL checks.  OnText takes a NUL-terminated wide string instead of
 * std::wstring, and TextCaretRect writes through a pointer instead of a reference.
 */

#include "core/Geometry.h"
#include "core/KeyEvent.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RenderContext RenderContext;
typedef struct Overlay Overlay;

typedef struct OverlayVtbl {
    void (*Update)(void *user, float dt);
    void (*Render)(void *user, RenderContext *context);
    bool (*BlocksInputBelow)(void *user);
    bool (*OnMouseMove)(void *user, float x, float y);
    bool (*OnMouseDown)(void *user, float x, float y);
    bool (*OnMouseUp)(void *user, float x, float y);
    bool (*OnKeyDown)(void *user, const KeyEvent *key);
    /* True when the overlay consumed the text. */
    bool (*OnText)(void *user, const wchar_t *text);
    bool (*WantsTextInput)(void *user);
    void (*OnImeComposition)(void *user, const wchar_t *text, int cursor);
    bool (*TextCaretRect)(void *user, Rect *caret);
    /* NULL = never expires. */
    bool (*Expired)(void *user);
    /* NULL for an implementation that owns nothing to free. */
    void (*Destroy)(void *user);
} OverlayVtbl;

struct Overlay {
    const OverlayVtbl *vtbl;
    void *user;
};

static inline bool Overlay_IsNull(const Overlay *overlay)
{
    return overlay->vtbl == NULL;
}

static inline void Overlay_Update(Overlay *overlay, float dt)
{
    if (overlay->vtbl->Update != NULL) {
        overlay->vtbl->Update(overlay->user, dt);
    }
}

static inline void Overlay_Render(Overlay *overlay, RenderContext *context)
{
    if (overlay->vtbl->Render != NULL) {
        overlay->vtbl->Render(overlay->user, context);
    }
}

/* The original returned false, i.e. input reaches the scene below. */
static inline bool Overlay_BlocksInputBelow(const Overlay *overlay)
{
    return overlay->vtbl->BlocksInputBelow != NULL
               ? overlay->vtbl->BlocksInputBelow(overlay->user)
               : false;
}

static inline bool Overlay_OnMouseMove(Overlay *overlay, float x, float y)
{
    return overlay->vtbl->OnMouseMove != NULL ? overlay->vtbl->OnMouseMove(overlay->user, x, y)
                                              : false;
}

static inline bool Overlay_OnMouseDown(Overlay *overlay, float x, float y)
{
    return overlay->vtbl->OnMouseDown != NULL ? overlay->vtbl->OnMouseDown(overlay->user, x, y)
                                              : false;
}

static inline bool Overlay_OnMouseUp(Overlay *overlay, float x, float y)
{
    return overlay->vtbl->OnMouseUp != NULL ? overlay->vtbl->OnMouseUp(overlay->user, x, y)
                                            : false;
}

static inline bool Overlay_OnKeyDown(Overlay *overlay, const KeyEvent *key)
{
    return overlay->vtbl->OnKeyDown != NULL ? overlay->vtbl->OnKeyDown(overlay->user, key) : false;
}

static inline bool Overlay_OnText(Overlay *overlay, const wchar_t *text)
{
    return overlay->vtbl->OnText != NULL ? overlay->vtbl->OnText(overlay->user, text) : false;
}

static inline bool Overlay_WantsTextInput(const Overlay *overlay)
{
    return overlay->vtbl->WantsTextInput != NULL
               ? overlay->vtbl->WantsTextInput(overlay->user)
               : false;
}

static inline void Overlay_OnImeComposition(Overlay *overlay, const wchar_t *text, int cursor)
{
    if (overlay->vtbl->OnImeComposition != NULL) {
        overlay->vtbl->OnImeComposition(overlay->user, text, cursor);
    }
}

static inline bool Overlay_TextCaretRect(const Overlay *overlay, Rect *caret)
{
    return overlay->vtbl->TextCaretRect != NULL
               ? overlay->vtbl->TextCaretRect(overlay->user, caret)
               : false;
}

static inline bool Overlay_Expired(const Overlay *overlay)
{
    return overlay->vtbl->Expired != NULL ? overlay->vtbl->Expired(overlay->user) : false;
}

/* Frees the implementation when the vtable says how, then clears the handle. */
static inline void Overlay_Release(Overlay *overlay)
{
    if (overlay->vtbl != NULL && overlay->vtbl->Destroy != NULL) {
        overlay->vtbl->Destroy(overlay->user);
    }
    overlay->vtbl = NULL;
    overlay->user = NULL;
}

#ifdef __cplusplus
}
#endif
