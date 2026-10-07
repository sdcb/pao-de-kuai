#include "core/Tween.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Tween.h is also consumed by C++ translation units during the port, so this
 * file must not use any C-only construct in the header and must not rely on
 * <algorithm>/<cmath>. */

static float Clampf(float value, float low, float high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

float Ease(Easing easing, float t)
{
    t = Clampf(t, 0.0f, 1.0f);
    switch (easing) {
    case EASING_LINEAR:
        return t;
    case EASING_OUT_CUBIC:
        return 1.0f - powf(1.0f - t, 3.0f);
    case EASING_IN_OUT_CUBIC:
        return t < 0.5f ? 4.0f * t * t * t
                        : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) * 0.5f;
    case EASING_OUT_BACK: {
        const float c1 = 1.70158f;
        const float c3 = c1 + 1.0f;
        return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
    }
    }
    return t;
}

void Tween_Init(Tween *tween, float from, float to, float duration, Easing easing)
{
    tween->from = from;
    tween->to = to;
    tween->duration = duration;
    tween->elapsed = 0.0f;
    tween->delay = 0.0f;
    tween->easing = easing;
    tween->finished = false;
    tween->onValue = NULL;
    tween->onComplete = NULL;
    tween->user = NULL;
}

void Tween_SetDelay(Tween *tween, float delay)
{
    tween->delay = delay;
}

void Tween_SetCallbacks(Tween *tween, void (*onValue)(void *, float),
                        void (*onComplete)(void *), void *user)
{
    tween->onValue = onValue;
    tween->onComplete = onComplete;
    tween->user = user;
}

void Tween_Update(Tween *tween, float dt)
{
    float activeElapsed;
    float t;
    float eased;

    if (tween->finished) {
        return;
    }
    tween->elapsed += dt;
    if (tween->elapsed < tween->delay) {
        return;
    }
    activeElapsed = tween->elapsed - tween->delay;
    t = tween->duration <= 0.0f
            ? 1.0f
            : Clampf(activeElapsed / tween->duration, 0.0f, 1.0f);
    eased = Ease(tween->easing, t);
    if (tween->onValue != NULL) {
        tween->onValue(tween->user, tween->from + (tween->to - tween->from) * eased);
    }
    if (t >= 1.0f) {
        tween->finished = true;
        if (tween->onComplete != NULL) {
            tween->onComplete(tween->user);
        }
    }
}

bool Tween_Finished(const Tween *tween)
{
    return tween->finished;
}

void TweenSet_Init(TweenSet *set)
{
    set->items = NULL;
    set->count = 0;
    set->cap = 0;
}

void TweenSet_Free(TweenSet *set)
{
    free(set->items);
    TweenSet_Init(set);
}

bool TweenSet_Add(TweenSet *set, const Tween *tween)
{
    if (set->count == set->cap) {
        const int next = set->cap > 0 ? set->cap * 2 : 4;
        Tween *grown = (Tween *)realloc(set->items, (size_t)next * sizeof(Tween));
        if (grown == NULL) {
            return false;
        }
        set->items = grown;
        set->cap = next;
    }
    set->items[set->count++] = *tween;
    return true;
}

void TweenSet_Update(TweenSet *set, float dt)
{
    int kept = 0;

    for (int i = 0; i < set->count; ++i) {
        Tween_Update(&set->items[i], dt);
        if (!set->items[i].finished) {
            if (kept != i) {
                set->items[kept] = set->items[i];
            }
            ++kept;
        }
    }
    set->count = kept;
}

bool TweenSet_Empty(const TweenSet *set)
{
    return set->count == 0;
}

void TweenSet_Clear(TweenSet *set)
{
    set->count = 0;
}
