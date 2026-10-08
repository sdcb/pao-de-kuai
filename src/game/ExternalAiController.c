#include "game/ExternalAiController.h"

void ExternalAiController_Destroy(ExternalAiController *controller)
{
    if (controller->vtbl != NULL && controller->vtbl->Destroy != NULL) {
        controller->vtbl->Destroy(controller->user);
    }
    controller->vtbl = NULL;
    controller->user = NULL;
}
