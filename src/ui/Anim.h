#pragma once

/*
 * Small animation maths helpers.
 *
 * Pure C, but also consumed by the C++ translation units that are still part of
 * the port, so this header stays inside the common subset of C17 and C++17:
 * no compound literals, no designated initializers, no default arguments.  The
 * old `EaseOutBack(t, overshoot = 1.70158f)` therefore became two functions.
 *
 * `std::lround` is still avoided in favour of RoundToInt: UCRT does export
 * `lround`, but keeping one rounding implementation is a style and size rule
 * that outlived the runtime it was originally written for.
 */

#include <math.h>
#include <stdbool.h>

#include <d2d1.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline float Clamp(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

static inline float Clamp01(float t)
{
    return Clamp(t, 0.0f, 1.0f);
}

static inline int RoundToInt(float value)
{
    return (int)(value < 0.0f ? value - 0.5f : value + 0.5f);
}

static inline float Lerp(float from, float to, float t)
{
    return from + (to - from) * t;
}

static inline float EaseOutCubic(float t)
{
    const float u = 1.0f - Clamp01(t);
    return 1.0f - u * u * u;
}

static inline float EaseInOutSine(float t)
{
    return 0.5f - 0.5f * cosf(Clamp01(t) * 3.14159265f);
}

static inline float EaseOutBackWith(float t, float overshoot)
{
    const float u = Clamp01(t) - 1.0f;
    return 1.0f + (overshoot + 1.0f) * u * u * u + overshoot * u * u;
}

static inline float EaseOutBack(float t)
{
    return EaseOutBackWith(t, 1.70158f);
}

/* Frame-rate independent exponential approach; rate is roughly 1 / time constant. */
static inline float Approach(float current, float target, float rate, float dt)
{
    return target + (current - target) * expf(-rate * dt);
}

/* Linear 0..1 progress through [start, start + duration]. */
static inline float Progress(float time, float start, float duration)
{
    return duration <= 0.0f ? 1.0f : Clamp01((time - start) / duration);
}

/* Clamp01 for the 0..1 case; this is the general one the layouts need. */
static inline float ClampF(float value, float low, float high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static inline D2D1_COLOR_F LerpColor(D2D1_COLOR_F from, D2D1_COLOR_F to, float t)
{
    D2D1_COLOR_F out;
    out.r = Lerp(from.r, to.r, t);
    out.g = Lerp(from.g, to.g, t);
    out.b = Lerp(from.b, to.b, t);
    out.a = Lerp(from.a, to.a, t);
    return out;
}

#ifdef __cplusplus
}
#endif
