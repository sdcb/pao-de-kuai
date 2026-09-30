#pragma once

#include <d2d1.h>

#include <algorithm>
#include <cmath>

namespace pdk::ui {

inline float Clamp01(float t) {
    return std::clamp(t, 0.0f, 1.0f);
}

// std::lround is not exported by msvcrt.dll, which the VC-LTL build links against.
inline int RoundToInt(float value) {
    return static_cast<int>(value < 0.0f ? value - 0.5f : value + 0.5f);
}

inline float Lerp(float from, float to, float t) {
    return from + (to - from) * t;
}

inline float EaseOutCubic(float t) {
    const float u = 1.0f - Clamp01(t);
    return 1.0f - u * u * u;
}

inline float EaseInOutSine(float t) {
    return 0.5f - 0.5f * std::cos(Clamp01(t) * 3.14159265f);
}

inline float EaseOutBack(float t, float overshoot = 1.70158f) {
    const float u = Clamp01(t) - 1.0f;
    return 1.0f + (overshoot + 1.0f) * u * u * u + overshoot * u * u;
}

// Frame-rate independent exponential approach; rate is roughly 1 / time constant.
inline float Approach(float current, float target, float rate, float dt) {
    return target + (current - target) * std::exp(-rate * dt);
}

// Linear 0..1 progress through [start, start + duration].
inline float Progress(float time, float start, float duration) {
    return duration <= 0.0f ? 1.0f : Clamp01((time - start) / duration);
}

inline D2D1_COLOR_F LerpColor(D2D1_COLOR_F from, D2D1_COLOR_F to, float t) {
    return D2D1_COLOR_F{
        Lerp(from.r, to.r, t),
        Lerp(from.g, to.g, t),
        Lerp(from.b, to.b, t),
        Lerp(from.a, to.a, t)};
}

} // namespace pdk::ui
