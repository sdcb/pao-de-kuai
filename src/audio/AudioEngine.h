#pragma once

#include "audio/AudioDecoder.h"
#include "audio/SoundIds.h"

#include <map>
#include <memory>
#include <vector>

#include <xaudio2.h>

namespace pdk::audio {

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool Initialize();
    void LoadAllFromResources();
    void SetMasterVolume(float volume);
    void Play(SoundId id);
    void Update();
    bool Available() const { return available_; }

private:
    struct LoadedSound {
        AudioData audio;
        float volume{1.0f};
    };
    struct ActiveVoice;

    IXAudio2* engine_{nullptr};
    IXAudio2MasteringVoice* masteringVoice_{nullptr};
    bool available_{false};
    bool mediaFoundationStarted_{false};
    float masterVolume_{0.8f};
    std::map<SoundId, LoadedSound> sounds_;
    std::vector<std::unique_ptr<ActiveVoice>> activeVoices_;
};

} // namespace pdk::audio
