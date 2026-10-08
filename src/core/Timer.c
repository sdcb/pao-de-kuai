#include "core/Timer.h"

#include <windows.h>

/*
 * QueryPerformanceCounter replaces <chrono>: it is the same steady clock on
 * Windows and costs one import, which matters for the size budget.  Both
 * pdk_core and pdk_app may include this header, so keep it free of any other
 * dependency.
 */

static long long QueryTicks(void)
{
    LARGE_INTEGER value;
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

static long long TicksPerSecond(void)
{
    LARGE_INTEGER value;
    QueryPerformanceFrequency(&value);
    return value.QuadPart > 0 ? value.QuadPart : 1;
}

void FrameTimer_Init(FrameTimer *timer)
{
    timer->last = QueryTicks();
}

float FrameTimer_Tick(FrameTimer *timer)
{
    long long now;
    float dt;

    if (timer->last == 0) {
        /* A zero-initialised timer is a documented valid state. */
        timer->last = QueryTicks();
        return 0.0f;
    }
    now = QueryTicks();
    dt = (float)((double)(now - timer->last) / (double)TicksPerSecond());
    timer->last = now;
    return dt > 0.1f ? 0.1f : dt;
}
