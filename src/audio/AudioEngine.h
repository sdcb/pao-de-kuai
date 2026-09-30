#pragma once

#include "audio/SoundIds.h"

#include <memory>

namespace pdk::audio {

// WASAPI shared-mode output with a small software mixer on a "Pro Audio" thread.
// Play() only queues a request, so it is cheap to call from the UI thread.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool Initialize();
    void LoadAllFromResources();
    void SetMasterVolume(float volume);
    void Play(SoundId id);
    bool Available() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool mediaFoundationStarted_{false};
};

} // namespace pdk::audio
