#pragma once

#include "core/Geometry.h"
#include "rules/CppCompat.h"

namespace pdk::scenes::layout {

constexpr Rect Table{60.0f, 72.0f, 1160.0f, 480.0f};
constexpr Rect Ai1Plate{84.0f, 16.0f, 412.0f, 132.0f};
constexpr Rect Ai2Plate{784.0f, 16.0f, 412.0f, 132.0f};
constexpr Rect PlayerPlate{24.0f, 626.0f, 262.0f, 78.0f};
constexpr float AvatarSize = 60.0f;
constexpr float AiCardHeight = 72.0f;
constexpr float HandBottom = 706.0f;
constexpr float SelectedLift = 24.0f;

inline Rect PlateFor(rules::PlayerId player) {
    return player == PLAYER_AI2 ? Ai2Plate : Ai1Plate;
}

// AI1 sits on the left with its avatar on the outer edge; AI2 mirrors it.
inline Rect AvatarRect(rules::PlayerId player) {
    if (player == PLAYER_HUMAN) {
        return {PlayerPlate.x + 14.0f, PlayerPlate.y + 13.0f, 52.0f, 52.0f};
    }
    const Rect plate = PlateFor(player);
    const float x = player == PLAYER_AI1 ? plate.x + 18.0f : plate.x + plate.width - 18.0f - AvatarSize;
    return {x, plate.y + 20.0f, AvatarSize, AvatarSize};
}

inline Rect AiInfoArea(rules::PlayerId player) {
    const Rect plate = PlateFor(player);
    const float x = player == PLAYER_AI1 ? plate.x + 96.0f : plate.x + 18.0f;
    return {x, plate.y + 12.0f, plate.width - 114.0f, plate.height - 24.0f};
}

inline Point AvatarCenter(rules::PlayerId player) {
    const Rect avatar = AvatarRect(player);
    return {avatar.x + avatar.width * 0.5f, avatar.y + avatar.height * 0.5f};
}

} // namespace pdk::scenes::layout
