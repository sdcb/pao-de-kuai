#include "audio/AudioEngine.h"

#include "audio/AudioDecoder.h"
#include "audio/SoundCatalog.h"
#include "resources/ResourceLoader.h"

#include <algorithm>
#include <atomic>

#include <mfapi.h>

namespace pdk::audio {

struct AudioEngine::ActiveVoice final : IXAudio2VoiceCallback {
    IXAudio2SourceVoice* voice{nullptr};
    std::atomic<bool> done{false};

    ~ActiveVoice() {
        if (voice) {
            voice->DestroyVoice();
        }
    }

    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override { done = true; }
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void*) override { done = true; }
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override { done = true; }
};

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    activeVoices_.clear();
    if (masteringVoice_) {
        masteringVoice_->DestroyVoice();
        masteringVoice_ = nullptr;
    }
    if (engine_) {
        engine_->Release();
        engine_ = nullptr;
    }
    if (mediaFoundationStarted_) {
        MFShutdown();
        mediaFoundationStarted_ = false;
    }
}

bool AudioEngine::Initialize() {
    if (available_) {
        return true;
    }
    if (!mediaFoundationStarted_) {
        if (FAILED(MFStartup(MF_VERSION))) {
            available_ = false;
            return false;
        }
        mediaFoundationStarted_ = true;
    }
    if (FAILED(XAudio2Create(&engine_, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
        available_ = false;
        return false;
    }
    if (FAILED(engine_->CreateMasteringVoice(&masteringVoice_))) {
        engine_->Release();
        engine_ = nullptr;
        available_ = false;
        return false;
    }
    available_ = true;
    return true;
}

void AudioEngine::LoadAllFromResources() {
    if (!Initialize()) {
        return;
    }
    for (const SoundCatalogEntry& entry : SoundCatalog()) {
        const auto bytes = resources::LoadResourceBytes(entry.resourceId);
        AudioData audio;
        if (DecodeMp3ToPcm(bytes, audio)) {
            sounds_[entry.id] = LoadedSound{std::move(audio), entry.recommendedVolume};
        }
    }
}

void AudioEngine::SetMasterVolume(float volume) {
    masterVolume_ = std::clamp(volume, 0.0f, 1.0f);
}

void AudioEngine::Play(SoundId id) {
    if (!available_ || !engine_) {
        return;
    }
    auto it = sounds_.find(id);
    if (it == sounds_.end()) {
        return;
    }

    auto active = std::make_unique<ActiveVoice>();
    if (FAILED(engine_->CreateSourceVoice(&active->voice, &it->second.audio.format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, active.get()))) {
        return;
    }
    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(it->second.audio.pcm.size());
    buffer.pAudioData = it->second.audio.pcm.data();
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    if (FAILED(active->voice->SubmitSourceBuffer(&buffer))) {
        return;
    }
    active->voice->SetVolume(masterVolume_ * it->second.volume);
    if (FAILED(active->voice->Start())) {
        return;
    }
    activeVoices_.push_back(std::move(active));
}

void AudioEngine::Update() {
    activeVoices_.erase(
        std::remove_if(activeVoices_.begin(), activeVoices_.end(), [](const std::unique_ptr<ActiveVoice>& voice) {
            return voice->done.load();
        }),
        activeVoices_.end());
}

} // namespace pdk::audio
