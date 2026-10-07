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
#include "rules/CppCompat.h"
#include "game/ExternalAiController.h"
#include "game/LocalAiController.h"
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

using ::LOCAL_AI_BASIC;
using ::LOCAL_AI_STRONG;
using ::ExternalAiController_CanHandle;
using ::ExternalAiController_Cancel;
using ::ExternalAiController_Destroy;
using ::ExternalAiController_HasPending;
using ::ExternalAiController_MetadataFor;
using ::ExternalAiController_Start;
using ::ExternalAiController_TryGetResult;

using ::ExternalAiController;
using ::ExternalAiControllerVtbl;
using ::ExternalAiRequest;
using ::ExternalAiResult;
using ::LocalAiKind;
using ::LocalAiController;
using ::LocalAiController_Create;
using ::LocalAiController_Interface;
using ::LocalAiController_SetStrategy;
using ::PDK_AI_ERROR_CAP;
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

/*
 * The old ExternalAiController abstract class.  The test double still subclasses it, so
 * the facade bridges a C++ implementation onto the C vtable; Transfer hands ownership to
 * the C side, which is how GameState takes its controllers.
 */
class ExternalAiControllerClass {
public:
    virtual ~ExternalAiControllerClass() = default;

    virtual bool CanHandle(rules::PlayerId player) const = 0;
    virtual StrategyMetadata MetadataFor(rules::PlayerId player) const
    {
        return UnknownStrategyMetadata();
    }
    virtual bool HasPending() const = 0;
    virtual void Start(const ExternalAiRequest& request) = 0;
    /* Writes into `out` and returns true when a result is ready. */
    virtual bool TryGetResult(ExternalAiResult& out) = 0;
    virtual void Cancel() = 0;
};

namespace detail {

inline bool ExternalAiControllerClass_CanHandle(void* user, PlayerId player)
{
    return static_cast<ExternalAiControllerClass*>(user)->CanHandle(player);
}

inline StrategyMetadata ExternalAiControllerClass_MetadataFor(void* user, PlayerId player)
{
    return static_cast<ExternalAiControllerClass*>(user)->MetadataFor(player);
}

inline bool ExternalAiControllerClass_HasPending(void* user)
{
    return static_cast<ExternalAiControllerClass*>(user)->HasPending();
}

inline void ExternalAiControllerClass_Start(void* user, const ExternalAiRequest* request)
{
    static_cast<ExternalAiControllerClass*>(user)->Start(*request);
}

inline bool ExternalAiControllerClass_TryGetResult(void* user, ExternalAiResult* out)
{
    return static_cast<ExternalAiControllerClass*>(user)->TryGetResult(*out);
}

inline void ExternalAiControllerClass_Cancel(void* user)
{
    static_cast<ExternalAiControllerClass*>(user)->Cancel();
}

inline void ExternalAiControllerClass_Destroy(void* user)
{
    delete static_cast<ExternalAiControllerClass*>(user);
}

} // namespace detail

/* Takes ownership of the controller; GameState destroys it. */
inline ::ExternalAiController Transfer(ExternalAiControllerClass* controller)
{
    static const ExternalAiControllerVtbl vtbl = {
        detail::ExternalAiControllerClass_CanHandle,
        detail::ExternalAiControllerClass_MetadataFor,
        detail::ExternalAiControllerClass_HasPending,
        detail::ExternalAiControllerClass_Start,
        detail::ExternalAiControllerClass_TryGetResult,
        detail::ExternalAiControllerClass_Cancel,
        detail::ExternalAiControllerClass_Destroy
    };
    ::ExternalAiController owned;
    owned.vtbl = (controller != nullptr) ? &vtbl : NULL;
    owned.user = controller;
    return owned;
}

} // namespace pdk::game

