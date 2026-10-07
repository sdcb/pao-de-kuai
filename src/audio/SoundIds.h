#pragma once

/*
 * Sound effect identifiers.
 *
 * Pure C: the scoped enum became a uint8_t typedef plus SOUND_* constants, which
 * keeps the identifier compact for the fixed sound table the engine indexes by it.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t SoundId;

enum {
    SOUND_BUTTON_CLICK = 0,
    SOUND_CONFIRM = 1,
    SOUND_CANCEL = 2,
    SOUND_TOAST = 3,
    SOUND_PAUSE = 4,
    SOUND_RESUME = 5,
    SOUND_SELECT_CARD = 6,
    SOUND_DESELECT_CARD = 7,
    SOUND_DEAL_CARD = 8,
    SOUND_PLAY_CARDS = 9,
    SOUND_PASS = 10,
    SOUND_HINT = 11,
    SOUND_INVALID_MOVE = 12,
    SOUND_TURN_PROMPT = 13,
    SOUND_ROUND_START = 14,
    SOUND_ROUND_END = 15,
    SOUND_BOMB = 16,
    SOUND_SPRING = 17,
    SOUND_WIN = 18,
    SOUND_LOSE = 19,
    SOUND_AI_TALK = 20,
    SOUND_COUNT = 21
};

#ifdef __cplusplus
}
#endif
