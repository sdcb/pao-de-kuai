#pragma once

/*
 * Value tweens, replacing the old std::function-based C++ class (plan.md 2).
 *
 * Callbacks are plain function pointers plus a `user` pointer, so a caller that
 * used to write a capturing lambda now passes a small thunk and the object it
 * belongs to.  `TweenSet` owns a growable array of tweens and compacts finished
 * ones out on Update, exactly like the previous std::remove_if pass.
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Easing {
    EASING_LINEAR,
    EASING_OUT_CUBIC,
    EASING_IN_OUT_CUBIC,
    EASING_OUT_BACK
} Easing;

float Ease(Easing easing, float t);

typedef struct Tween {
    float from;
    float to;
    float duration;
    float elapsed;
    float delay;
    Easing easing;
    bool finished;
    void (*onValue)(void *user, float value);
    void (*onComplete)(void *user);
    void *user;
} Tween;

void Tween_Init(Tween *tween, float from, float to, float duration, Easing easing);
void Tween_SetDelay(Tween *tween, float delay);
/* Either callback may be NULL.  `user` is passed through unchanged. */
void Tween_SetCallbacks(Tween *tween, void (*onValue)(void *, float),
                        void (*onComplete)(void *), void *user);
void Tween_Update(Tween *tween, float dt);
bool Tween_Finished(const Tween *tween);

typedef struct TweenSet {
    Tween *items;
    int count;
    int cap;
} TweenSet;

void TweenSet_Init(TweenSet *set);
void TweenSet_Free(TweenSet *set);
/* Copies the tween into the set; the caller's copy stays valid but unused. */
bool TweenSet_Add(TweenSet *set, const Tween *tween);
void TweenSet_Update(TweenSet *set, float dt);
bool TweenSet_Empty(const TweenSet *set);
void TweenSet_Clear(TweenSet *set);

#ifdef __cplusplus
}
#endif
