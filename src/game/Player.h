#pragma once

/*
 * One seat's state: display name, hand and whether the seat has ever played.
 *
 * Pure C.  The hand is the fixed-capacity Cards type and the name is a fixed buffer
 * (a player name is typed into one text field), so the struct has no ownership and is
 * freely copyable -- GameState keeps three of them by value.
 */

#include "core/Str.h"
#include "rules/Card.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PDK_SEAT_NAME_CAP = 64 };

typedef struct PlayerState {
    char name[PDK_SEAT_NAME_CAP];
    Cards hand;
    bool hasPlayedCards;
} PlayerState;

static inline bool PlayerState_Empty(const PlayerState *player)
{
    return player->hand.count == 0;
}

static inline void PlayerState_Init(PlayerState *player, const char *name)
{
    Str_CopyTo(player->name, PDK_SEAT_NAME_CAP, name);
    Cards_Clear(&player->hand);
    player->hasPlayedCards = false;
}

#ifdef __cplusplus
}
#endif
