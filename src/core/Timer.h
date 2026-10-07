#pragma once

/*
 * Frame delta timer, replacing the std::chrono-based C++ class (plan.md 2).
 *
 * A zero-initialised FrameTimer is valid: the first Tick lazily starts the
 * clock, so callers do not have to remember FrameTimer_Init.  dt is clamped to
 * 0.1 s exactly as before, so a stalled frame cannot teleport animations.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FrameTimer {
    long long last; /* QueryPerformanceCounter ticks */
} FrameTimer;

void FrameTimer_Init(FrameTimer *timer);
float FrameTimer_Tick(FrameTimer *timer);

#ifdef __cplusplus
}
#endif
