#include "game/AiStrategy.h"

void AiStrategy_Release(AiStrategy *strategy)
{
    if (strategy->vtbl != NULL && strategy->vtbl->Destroy != NULL) {
        strategy->vtbl->Destroy(strategy->user);
    }
    strategy->vtbl = NULL;
    strategy->user = NULL;
}

AiMoveChoice BasicAiStrategy_ChooseMoveAdapter(void *user, const Cards *hand,
                                              const AiContext *context);
AiMoveChoice StrongAiStrategy_ChooseMoveAdapter(void *user, const Cards *hand,
                                               const AiContext *context);
StrategyMetadata BasicAiStrategy_MetadataAdapter(void *user);
StrategyMetadata StrongAiStrategy_MetadataAdapter(void *user);

AiMoveChoice BasicAiStrategy_ChooseMoveAdapter(void *user, const Cards *hand,
                                              const AiContext *context)
{
    (void)user;
    return BasicAiStrategy_ChooseMove(hand, context);
}

AiMoveChoice StrongAiStrategy_ChooseMoveAdapter(void *user, const Cards *hand,
                                               const AiContext *context)
{
    (void)user;
    return StrongAiStrategy_ChooseMove(hand, context);
}

StrategyMetadata BasicAiStrategy_MetadataAdapter(void *user)
{
    (void)user;
    return BasicAiStrategy_Metadata();
}

StrategyMetadata StrongAiStrategy_MetadataAdapter(void *user)
{
    (void)user;
    return StrongAiStrategy_Metadata();
}

/* Both built-ins own nothing, so Destroy stays NULL and Release is a no-op for them. */
static const AiStrategyVtbl kBasicVtbl = {
    BasicAiStrategy_ChooseMoveAdapter,
    BasicAiStrategy_MetadataAdapter,
    NULL
};

static const AiStrategyVtbl kStrongVtbl = {
    StrongAiStrategy_ChooseMoveAdapter,
    StrongAiStrategy_MetadataAdapter,
    NULL
};

AiStrategy BasicAiStrategy_Instance(void)
{
    AiStrategy strategy;
    strategy.vtbl = &kBasicVtbl;
    strategy.user = NULL;
    return strategy;
}

AiStrategy StrongAiStrategy_Instance(void)
{
    AiStrategy strategy;
    strategy.vtbl = &kStrongVtbl;
    strategy.user = NULL;
    return strategy;
}

StrategyMetadata BasicAiStrategy_Metadata(void)
{
    return BasicStrategyMetadata();
}

StrategyMetadata StrongAiStrategy_Metadata(void)
{
    return StrongStrategyMetadata();
}
