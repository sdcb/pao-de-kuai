#pragma once

/*
 * One AI seat: which strategy it runs, and whether this struct owns that strategy.
 *
 * Pure C.  The original class owned a unique_ptr<AiStrategy>; here the seat holds the
 * (vtable, user) pair by value plus an ownership flag, so a seat is still copyable and
 * the built-in strategies (which own nothing) cost no allocation.
 */

#include "game/AiStrategy.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AiPlayer {
    AiStrategy strategy;
    bool ownsStrategy;
} AiPlayer;

/* Defaults to the basic strategy, matching the old default constructor. */
void AiPlayer_Init(AiPlayer *player);
void AiPlayer_Destroy(AiPlayer *player);

/* Replaces the strategy, releasing the previous one when this seat owned it.  A
 * strategy with a NULL vtable falls back to basic, matching the old
 * `SetStrategy(nullptr)` behaviour. */
void AiPlayer_SetStrategy(AiPlayer *player, AiStrategy strategy, bool takeOwnership);

AiMoveChoice AiPlayer_ChooseMove(AiPlayer *player, const Cards *hand, const AiContext *context);
StrategyMetadata AiPlayer_Metadata(const AiPlayer *player);

#ifdef __cplusplus
}
#endif
