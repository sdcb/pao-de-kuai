#pragma once

/*
 * Writes one round's full audit trail as JSON under `<root>/round-traces/<date>/`.
 *
 * Pure C.  RoundTrace used to *own* its data (std::array members and a
 * std::vector<TurnRecord>), which is why the recorder took it by value-ish reference.
 * It is now a read-only *view*: the recorder only ever walks the trace, so the trace
 * points at GameState's own storage instead of copying it.  That matters because a
 * TurnRecord is about 2.6 KB (two snapshots plus two actions plus the trace texts), so a
 * round's worth of records is hundreds of kilobytes that the old code copied.
 *
 * The old RoundTraceRecorder class held only the root, so the C API takes the root
 * directly rather than introducing a struct with one field.
 */

#include "game/AiStrategy.h"
#include "game/Player.h"
#include "game/StrategyMetadata.h"
#include "game/TurnRecord.h"
#include "stats/StatStore.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RoundTrace {
    unsigned seed;
    const char *playerName;
    const char *startedAt;
    PlayerId roundLeader;
    /* PDK_AI_SEATS entries each, in seat order. */
    const PlayerState *initialPlayers;
    const StrategyMetadata *strategies;
    const TurnRecord *turns;
    int turnCount;
    const RoundRecord *result;
} RoundTrace;

/*
 * `root` NULL or empty means the process current directory, matching the old
 * constructor.  On success, and when `outPath` is non-NULL, the written file's path is
 * copied into it (bounded by `cap`, always NUL terminated).
 */
bool RoundTraceRecorder_WriteRound(const char *root, const RoundTrace *trace, char *outPath,
                                   int cap);

#ifdef __cplusplus
}
#endif
