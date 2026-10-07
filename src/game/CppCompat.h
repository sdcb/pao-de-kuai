#pragma once

/*
 * TEMPORARY TRANSITION HEADER -- DELETE WHEN THE PORT IS DONE.
 *
 * The game value types are pure C now (plan.md S4), so GameAction, PlayerState,
 * StrategyMetadata, TurnSnapshot, TurnDecisionTrace and TurnRecord live at global
 * scope.  The C++ translation units that are not converted yet spell them
 * `game::X`, and this header is the only thing that keeps that spelling working.
 *
 * It grows as the rest of src/game is converted; it disappears together with the
 * last C++ file under src/.
 */

#include <array>
#include <string>
#include <vector>

#include "game/Player.h"
#include "game/StrategyMetadata.h"
#include "game/TurnRecord.h"

namespace pdk::game {

using ::GameAction;
using ::PlayerState;
using ::StrategyMetadata;
using ::TurnDecisionReason;
using ::TurnDecisionSource;
using ::TurnDecisionTrace;
using ::TurnRecord;
using ::TurnSnapshot;

using ::TURN_REASON_CANNOT_BEAT;
using ::TURN_REASON_NORMAL_CHOICE;
using ::TURN_REASON_ONLY_LEGAL_MOVE;
using ::TURN_SOURCE_HUMAN;
using ::TURN_SOURCE_LOCAL_AI;
using ::TURN_SOURCE_SYSTEM;

} // namespace pdk::game
