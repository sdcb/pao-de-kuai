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
#include <memory>
#include <string>
#include <vector>

#include "game/AiPlayer.h"
#include "game/Player.h"
#include "game/StrategyMetadata.h"
#include "game/TurnRecord.h"

namespace pdk::game {

using ::AiContext;
using ::AiMoveChoice;
using ::AiPlayer;
using ::AiStrategy;
using ::AiStrategyVtbl;
using ::GameAction;
using ::OptionalPassObservation;
using ::PassHistory;
using ::PassObservation;
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

using ::PDK_AI_REASON_CAP;
using ::PDK_AI_SEATS;
using ::PDK_PASS_HISTORY_MAX;

using ::AiMoveChoice_MakePass;
using ::AiMoveChoice_MakePlay;
using ::OptionalPassObservation_Set;
using ::PassHistories_Copy;
using ::PassHistory_Add;
using ::PassHistory_Clear;
using ::PassObservations_Clear;
using ::PassObservations_Copy;

/* Test-facing shorthands for the two shapes that used to be plain assignment. */
inline void SetRemainingCards(AiContext& context, int player, int ai1, int ai2)
{
    context.remainingCards[0] = player;
    context.remainingCards[1] = ai1;
    context.remainingCards[2] = ai2;
}

inline void SetPassObservation(OptionalPassObservation& slot, const PassObservation& observation)
{
    slot.value = observation;
    slot.has = true;
}

/*
 * The old AiStrategy hierarchy, now that the C interface is a (vtable, user) pair.
 * These three classes exist only so the not-yet-ported C++ call sites and the
 * brute-force test strategy keep their shape; the real bodies are the free functions.
 */
class AiStrategyClass {
public:
    virtual ~AiStrategyClass() = default;
    virtual AiMoveChoice ChooseMove(const Cards& hand, const AiContext& context) = 0;
    virtual StrategyMetadata Metadata() const { return UnknownStrategyMetadata(); }
};

namespace detail {

inline AiMoveChoice AiStrategyClass_ChooseMove(void* user, const Cards* hand, const AiContext* context)
{
    return static_cast<AiStrategyClass*>(user)->ChooseMove(*hand, *context);
}

inline StrategyMetadata AiStrategyClass_Metadata(void* user)
{
    return static_cast<AiStrategyClass*>(user)->Metadata();
}

inline void AiStrategyClass_Destroy(void* user)
{
    delete static_cast<AiStrategyClass*>(user);
}

/* Borrows a C++ strategy as a C interface without taking ownership. */
inline ::AiStrategy Borrow(AiStrategyClass* strategy)
{
    static const AiStrategyVtbl vtbl = {
        AiStrategyClass_ChooseMove,
        AiStrategyClass_Metadata,
        NULL
    };
    ::AiStrategy borrowed;
    borrowed.vtbl = (strategy != nullptr) ? &vtbl : NULL;
    borrowed.user = strategy;
    return borrowed;
}

/* Hands a C++ strategy to the C side, which now owns it. */
inline ::AiStrategy Transfer(AiStrategyClass* strategy)
{
    static const AiStrategyVtbl vtbl = {
        AiStrategyClass_ChooseMove,
        AiStrategyClass_Metadata,
        AiStrategyClass_Destroy
    };
    ::AiStrategy owned;
    owned.vtbl = (strategy != nullptr) ? &vtbl : NULL;
    owned.user = strategy;
    return owned;
}

} // namespace detail

class BasicAiStrategy final : public AiStrategyClass {
public:
    AiMoveChoice ChooseMove(const Cards& hand, const AiContext& context) override
    {
        return BasicAiStrategy_ChooseMove(&hand, &context);
    }

    StrategyMetadata Metadata() const override { return BasicStrategyMetadata(); }
};

class StrongAiStrategy final : public AiStrategyClass {
public:
    AiMoveChoice ChooseMove(const Cards& hand, const AiContext& context) override
    {
        return StrongAiStrategy_ChooseMove(&hand, &context);
    }

    StrategyMetadata Metadata() const override { return StrongStrategyMetadata(); }
};

/* Kept because call sites say game::AiStrategy; the C name is the interface itself. */
using AiStrategyBase = AiStrategyClass;

} // namespace pdk::game

