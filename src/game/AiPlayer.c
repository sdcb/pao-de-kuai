#include "game/AiPlayer.h"

void AiPlayer_Init(AiPlayer *player)
{
    player->strategy = BasicAiStrategy_Instance();
    player->ownsStrategy = false;
}

void AiPlayer_Destroy(AiPlayer *player)
{
    if (player->ownsStrategy) {
        AiStrategy_Release(&player->strategy);
    }
    player->ownsStrategy = false;
    player->strategy.vtbl = NULL;
    player->strategy.user = NULL;
}

void AiPlayer_SetStrategy(AiPlayer *player, AiStrategy strategy, bool takeOwnership)
{
    if (player->ownsStrategy) {
        AiStrategy_Release(&player->strategy);
    }
    if (strategy.vtbl == NULL) {
        player->strategy = BasicAiStrategy_Instance();
        player->ownsStrategy = false;
        return;
    }
    player->strategy = strategy;
    player->ownsStrategy = takeOwnership;
}

AiMoveChoice AiPlayer_ChooseMove(AiPlayer *player, const Cards *hand, const AiContext *context)
{
    return AiStrategy_ChooseMove(&player->strategy, hand, context);
}

StrategyMetadata AiPlayer_Metadata(const AiPlayer *player)
{
    return AiStrategy_Metadata(&player->strategy);
}
