#pragma once

/*
 * Appends a finished round to the local per-day statistics file.
 *
 * Pure C.  The original class held nothing but a StatStore and its default constructor
 * already resolved the root to the process current directory, so RoundRecorder_Init
 * does the same and AppendToday just supplies today's date key.
 */

#include "stats/StatStore.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RoundRecorder {
    StatStore store;
} RoundRecorder;

/* `root` is the statistics root inside the process current directory. */
void RoundRecorder_Init(RoundRecorder *recorder);
bool RoundRecorder_AppendToday(RoundRecorder *recorder, const RoundRecord *record);

#ifdef __cplusplus
}
#endif
