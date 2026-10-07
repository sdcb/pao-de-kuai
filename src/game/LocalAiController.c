#include "game/LocalAiController.h"

#include "core/Str.h"

#include <windows.h>

#include <stdlib.h>
#include <string.h>

/*
 * The shared half of the controller.
 *
 * A request is answered on a detached worker thread, and the controller can be destroyed
 * while that search is still running (GameState replaces its controller list at any
 * time), so the mutex, the generation counter and the result slot cannot live in the
 * controller itself.  They live here and are reference counted: the controller holds one
 * reference and every in-flight worker holds another.
 *
 * The generation counter is what makes cancellation correct rather than merely likely: a
 * worker whose generation no longer matches has been superseded or cancelled and drops
 * its result instead of publishing it.
 */
typedef struct LocalAiShared {
    volatile LONG refs;
    CRITICAL_SECTION mutex;
    int generation;
    bool pending;
    ExternalAiResult result;
    bool hasResult;
} LocalAiShared;

struct LocalAiController {
    LocalAiKind kinds[PDK_AI_SEATS];
    bool configured[PDK_AI_SEATS];
    LocalAiShared *shared;
};

/* Everything a worker needs, including its own copy of the request. */
typedef struct LocalAiTask {
    LocalAiShared *shared;
    LocalAiKind kind;
    int generation;
    ExternalAiRequest request;
} LocalAiTask;

static LocalAiShared *LocalAiShared_Create(void)
{
    LocalAiShared *shared = (LocalAiShared *)malloc(sizeof(LocalAiShared));

    if (shared == NULL) {
        return NULL;
    }
    shared->refs = 1;
    InitializeCriticalSection(&shared->mutex);
    shared->generation = 0;
    shared->pending = false;
    shared->hasResult = false;
    memset(&shared->result, 0, sizeof(shared->result));
    return shared;
}

static void LocalAiShared_AddRef(LocalAiShared *shared)
{
    InterlockedIncrement(&shared->refs);
}

static void LocalAiShared_Release(LocalAiShared *shared)
{
    if (InterlockedDecrement(&shared->refs) == 0) {
        DeleteCriticalSection(&shared->mutex);
        free(shared);
    }
}

static void LocalAiShared_Publish(LocalAiShared *shared, int generation,
                                  const ExternalAiResult *result)
{
    EnterCriticalSection(&shared->mutex);
    if (shared->generation == generation) {
        shared->pending = false;
        shared->result = *result;
        shared->hasResult = true;
    }
    LeaveCriticalSection(&shared->mutex);
}

static DWORD WINAPI LocalAiWorker(LPVOID parameter)
{
    LocalAiTask *task = (LocalAiTask *)parameter;
    const Cards *hand = &task->request.snapshot.hands[PlayerIndex(task->request.player)];
    ExternalAiResult result;

    memset(&result, 0, sizeof(result));
    result.source = TURN_SOURCE_LOCAL_AI;
    /*
     * The C++ version wrapped this in try/catch and reported "Local AI strategy failed".
     * C has no exceptions, and both strategies are pure computation over value types, so
     * there is no failure path left to report.
     */
    if (task->kind == LOCAL_AI_STRONG) {
        result.localChoice = StrongAiStrategy_ChooseMove(hand, &task->request.context);
    } else {
        result.localChoice = BasicAiStrategy_ChooseMove(hand, &task->request.context);
    }
    result.ok = true;
    result.hasLocalChoice = true;

    LocalAiShared_Publish(task->shared, task->generation, &result);
    LocalAiShared_Release(task->shared);
    free(task);
    return 0;
}

LocalAiController *LocalAiController_Create(void)
{
    LocalAiController *controller = (LocalAiController *)malloc(sizeof(LocalAiController));

    if (controller == NULL) {
        return NULL;
    }
    memset(controller, 0, sizeof(*controller));
    controller->shared = LocalAiShared_Create();
    if (controller->shared == NULL) {
        free(controller);
        return NULL;
    }
    return controller;
}

static void LocalAiController_DestroyImpl(void *user)
{
    LocalAiController *controller = (LocalAiController *)user;

    if (controller == NULL) {
        return;
    }
    LocalAiShared_Release(controller->shared);
    free(controller);
}

void LocalAiController_SetStrategy(LocalAiController *controller, PlayerId player,
                                  LocalAiKind kind)
{
    const int index = PlayerIndex(player);

    if (controller == NULL || index < 0 || index >= PDK_AI_SEATS) {
        return;
    }
    controller->kinds[index] = kind;
    controller->configured[index] = true;
}

static bool LocalAiController_CanHandleImpl(void *user, PlayerId player)
{
    LocalAiController *controller = (LocalAiController *)user;
    const int index = PlayerIndex(player);

    return controller != NULL && index >= 0 && index < PDK_AI_SEATS &&
           controller->configured[index];
}

static StrategyMetadata LocalAiController_MetadataForImpl(void *user, PlayerId player)
{
    LocalAiController *controller = (LocalAiController *)user;
    const int index = PlayerIndex(player);

    if (controller == NULL || index < 0 || index >= PDK_AI_SEATS ||
        !controller->configured[index]) {
        return UnknownStrategyMetadata();
    }
    return controller->kinds[index] == LOCAL_AI_STRONG ? StrongStrategyMetadata()
                                                      : BasicStrategyMetadata();
}

static bool LocalAiController_HasPendingImpl(void *user)
{
    LocalAiController *controller = (LocalAiController *)user;
    bool pending;

    if (controller == NULL) {
        return false;
    }
    EnterCriticalSection(&controller->shared->mutex);
    pending = controller->shared->pending;
    LeaveCriticalSection(&controller->shared->mutex);
    return pending;
}

static void LocalAiController_StartImpl(void *user, const ExternalAiRequest *request)
{
    LocalAiController *controller = (LocalAiController *)user;
    LocalAiShared *shared;
    LocalAiTask *task;
    HANDLE thread;
    int index;
    int generation;
    LocalAiKind kind;

    if (controller == NULL || request == NULL) {
        return;
    }
    index = PlayerIndex(request->player);
    shared = controller->shared;

    if (index < 0 || index >= PDK_AI_SEATS || !controller->configured[index]) {
        ExternalAiResult failed;

        memset(&failed, 0, sizeof(failed));
        failed.source = TURN_SOURCE_LOCAL_AI;
        Str_CopyTo(failed.errorMessage, PDK_AI_ERROR_CAP,
                   "No local AI strategy configured for player");
        EnterCriticalSection(&shared->mutex);
        shared->pending = false;
        shared->result = failed;
        shared->hasResult = true;
        LeaveCriticalSection(&shared->mutex);
        return;
    }
    kind = controller->kinds[index];

    EnterCriticalSection(&shared->mutex);
    generation = ++shared->generation;
    shared->pending = true;
    shared->hasResult = false;
    LeaveCriticalSection(&shared->mutex);

    task = (LocalAiTask *)malloc(sizeof(LocalAiTask));
    if (task == NULL) {
        return;
    }
    LocalAiShared_AddRef(shared);
    task->shared = shared;
    task->kind = kind;
    task->generation = generation;
    task->request = *request;

    thread = CreateThread(NULL, 0, LocalAiWorker, task, 0, NULL);
    if (thread == NULL) {
        LocalAiShared_Release(shared);
        free(task);
        return;
    }
    /* Closed immediately: the thread is detached, exactly like the old std::thread. */
    CloseHandle(thread);
}

static bool LocalAiController_TryGetResultImpl(void *user, ExternalAiResult *out)
{
    LocalAiController *controller = (LocalAiController *)user;
    LocalAiShared *shared;
    bool ready;

    if (controller == NULL || out == NULL) {
        return false;
    }
    shared = controller->shared;
    EnterCriticalSection(&shared->mutex);
    ready = shared->hasResult;
    if (ready) {
        *out = shared->result;
        shared->hasResult = false;
    }
    LeaveCriticalSection(&shared->mutex);
    return ready;
}

static void LocalAiController_CancelImpl(void *user)
{
    LocalAiController *controller = (LocalAiController *)user;

    if (controller == NULL) {
        return;
    }
    EnterCriticalSection(&controller->shared->mutex);
    ++controller->shared->generation;
    controller->shared->pending = false;
    controller->shared->hasResult = false;
    LeaveCriticalSection(&controller->shared->mutex);
}

static const ExternalAiControllerVtbl kLocalAiControllerVtbl = {
    LocalAiController_CanHandleImpl,
    LocalAiController_MetadataForImpl,
    LocalAiController_HasPendingImpl,
    LocalAiController_StartImpl,
    LocalAiController_TryGetResultImpl,
    LocalAiController_CancelImpl,
    LocalAiController_DestroyImpl
};

ExternalAiController LocalAiController_Interface(LocalAiController *controller)
{
    /* NOTE: this local cannot be called `interface` -- MinGW's basetyps.h does
     * `#define interface struct` for COM headers, so the name silently turns the
     * declaration into a syntax error. */
    ExternalAiController controllerView;

    if (controller == NULL) {
        controllerView.vtbl = NULL;
        controllerView.user = NULL;
        return controllerView;
    }
    controllerView.vtbl = &kLocalAiControllerVtbl;
    controllerView.user = controller;
    return controllerView;
}
