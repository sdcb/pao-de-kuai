#pragma once

/*
 * TEMPORARY TRANSITION HEADER -- DELETE WHEN THE PORT IS DONE.
 *
 * `src/audio/` is pure C now (plan.md S6): the engine is a value type with
 * AudioEngine_* free functions, the sample buffers are refcounted, and the sound
 * ids are SOUND_* constants.
 *
 * The only thing the C++ side still needs is the class shape for `App`, which
 * holds one by value.  Everything else (the SOUND_* constants) is already usable
 * straight from the C headers.
 */

#include "audio/AudioDecoder.h"
#include "audio/AudioEngine.h"
#include "audio/SoundCatalog.h"
#include "audio/SoundIds.h"

namespace pdk::audio {

using ::SoundCatalogEntry;
using ::SoundId;

class AudioEngine {
public:
    AudioEngine() { ::AudioEngine_Init(&data_); }
    ~AudioEngine() { ::AudioEngine_Destroy(&data_); }

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool Initialize() { return ::AudioEngine_Initialize(&data_); }
    void LoadAllFromResources() { ::AudioEngine_LoadAllFromResources(&data_); }
    void SetMasterVolume(float volume) { ::AudioEngine_SetMasterVolume(&data_, volume); }
    void Play(SoundId id) { ::AudioEngine_Play(&data_, id); }
    bool Available() const { return ::AudioEngine_Available(&data_); }

private:
    ::AudioEngine data_;
};

} // namespace pdk::audio
