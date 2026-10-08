#pragma once

/*
 * WASAPI shared-mode output with a small software mixer on a "Pro Audio" thread.
 *
 * Pure C.  The previous type was a pimpl class; the state lives behind a single
 * heap pointer created by AudioEngine_Initialize and released by
 * AudioEngine_Destroy, and the struct itself is a plain value so callers can hold
 * it by value.  Play() only queues a request, so it stays cheap to call from the
 * UI thread.
 *
 * Threading contract (same as before):
 *   - the render thread owns the WASAPI objects and the voice list;
 *   - a CRITICAL_SECTION guards the sound table, the pending queue and the device
 *     sample rate;
 *   - stop / deviceChanged / master volume are interlocked.
 */

#include "audio/SoundIds.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudioEngineState AudioEngineState;

typedef struct AudioEngine {
    AudioEngineState *state;
    bool mediaFoundationStarted;
} AudioEngine;

void AudioEngine_Init(AudioEngine *engine);
/* Joins the render thread and shuts Media Foundation down if we started it. */
void AudioEngine_Destroy(AudioEngine *engine);

/* Starts Media Foundation and the render thread.  Idempotent; a second call
 * returns true without doing anything. */
bool AudioEngine_Initialize(AudioEngine *engine);
void AudioEngine_LoadAllFromResources(AudioEngine *engine);
void AudioEngine_SetMasterVolume(AudioEngine *engine, float volume);
/* Safe to call before Initialize or after a failed one; the request is dropped. */
void AudioEngine_Play(AudioEngine *engine, SoundId id);
bool AudioEngine_Available(const AudioEngine *engine);

#ifdef __cplusplus
}
#endif
