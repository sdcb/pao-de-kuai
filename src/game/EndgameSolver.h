#pragma once

/*
 * Exact endgame search for the strong strategy.
 *
 * Given every hand (perfect information -- the strategy calls this once per sampled
 * deal of the unknown cards), it plays the rest of the round out with minimax: the
 * human seat maximises its own points, both AI seats minimise them.  That matches the
 * strong strategy's team play, where the two AIs play together against the human; when
 * the strong strategy plays the human seat (autoplay) it is the "both AIs are against
 * me" view, which is the cautious one.
 *
 * Points are scored exactly like the round: the winner takes every loser's remaining
 * cards, a loser's single last card is free, a loser who never played pays 32, and a
 * bomb that is not beaten within its trick gives +20 to its owner and -10 to each other
 * seat.  Hands are rank counts because suits never affect a pattern.
 */

#include "rules/HandPattern.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EndgamePosition {
    uint8_t counts[3][RANK_TWO + 1]; /* counts[seat][rank], rank 3..15 */
    bool played[3];                  /* the seat has played a card this round */
    int current;                     /* seat to move */
    int lastMover;                   /* owner of `last`; leads when both others pass */
    bool hasLast;
    HandPattern last;
    int passCount;                   /* passes since `last` */
} EndgamePosition;

/* The seat after `seat` in turn order (counterclockwise, as in the game). */
int Endgame_NextSeat(int seat);

/*
 * Writes the minimax value of the points `humanSeat` collects from this position on
 * (bombs already scored are not included).  Returns false, leaving *out untouched,
 * when the search needs more than `nodeBudget` nodes.
 */
bool Endgame_Solve(const EndgamePosition *position, int humanSeat, int nodeBudget, int *out);

/*
 * Every distinct legal play for the seat to move, as rank-count selections; exposed for
 * the tests, which compare it with the subset search through the real validator.
 * Returns the count written (at most `cap`).
 */
int Endgame_LegalPlays(const EndgamePosition *position, uint8_t (*plays)[RANK_TWO + 1],
                       HandPattern *patterns, int cap);

#ifdef __cplusplus
}
#endif
