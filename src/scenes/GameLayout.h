#pragma once

/*
 * The game table's fixed 1280x720 geometry.  Pure C now, so the converted GameScene and the talk
 * bubble overlay can share it.
 *
 * The names are GameLayout_* because C has no namespaces.
 */

#include "core/Geometry.h"
#include "rules/Card.h"
#include "rules/Scoring.h"

enum {
    GAME_LAYOUT_AVATAR_SIZE = 60,
    GAME_LAYOUT_AI_CARD_HEIGHT = 72,
    GAME_LAYOUT_HAND_BOTTOM = 706,
    GAME_LAYOUT_SELECTED_LIFT = 24
};

static const Rect GAME_LAYOUT_TABLE = {60.0f, 72.0f, 1160.0f, 480.0f};
static const Rect GAME_LAYOUT_AI1_PLATE = {84.0f, 16.0f, 412.0f, 132.0f};
static const Rect GAME_LAYOUT_AI2_PLATE = {784.0f, 16.0f, 412.0f, 132.0f};
static const Rect GAME_LAYOUT_PLAYER_PLATE = {24.0f, 626.0f, 262.0f, 78.0f};

static inline Rect GameLayout_PlateFor(PlayerId player)
{
    return player == PLAYER_AI2 ? GAME_LAYOUT_AI2_PLATE : GAME_LAYOUT_AI1_PLATE;
}

/* AI1 sits on the left with its avatar on the outer edge; AI2 mirrors it. */
static inline Rect GameLayout_AvatarRect(PlayerId player)
{
    Rect plate;
    float x;

    if (player == PLAYER_HUMAN) {
        return Rect_Make(GAME_LAYOUT_PLAYER_PLATE.x + 14.0f, GAME_LAYOUT_PLAYER_PLATE.y + 13.0f,
                         52.0f, 52.0f);
    }
    plate = GameLayout_PlateFor(player);
    x = player == PLAYER_AI1 ? plate.x + 18.0f
                             : plate.x + plate.width - 18.0f - (float)GAME_LAYOUT_AVATAR_SIZE;
    return Rect_Make(x, plate.y + 20.0f, (float)GAME_LAYOUT_AVATAR_SIZE,
                     (float)GAME_LAYOUT_AVATAR_SIZE);
}

static inline Rect GameLayout_AiInfoArea(PlayerId player)
{
    const Rect plate = GameLayout_PlateFor(player);
    const float x = player == PLAYER_AI1 ? plate.x + 96.0f : plate.x + 18.0f;

    return Rect_Make(x, plate.y + 12.0f, plate.width - 114.0f, plate.height - 24.0f);
}

static inline Point GameLayout_AvatarCenter(PlayerId player)
{
    const Rect avatar = GameLayout_AvatarRect(player);

    return Point_Make(avatar.x + avatar.width * 0.5f, avatar.y + avatar.height * 0.5f);
}
