#include "audio/AudioEngine.h"

#include "audio/AudioDecoder.h"
#include "audio/SoundCatalog.h"
#include "resources/ResourceLoader.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <ksmedia.h>
#include <mfapi.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

namespace pdk::audio {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::size_t MaxVoices = 24;
constexpr float LimiterKnee = 0.8f;

// Padé approximation of tanh; std::tanh would pull in CRT math the VC-LTL build does not link.
float FastTanh(float x) {
    x = std::clamp(x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Unity below the knee, then a tanh shoulder that approaches full scale without clipping.
float SoftLimit(float x) {
    const float magnitude = x < 0.0f ? -x : x;
    if (magnitude <= LimiterKnee) {
        return x;
    }
    const float headroom = 1.0f - LimiterKnee;
    const float shaped = LimiterKnee + headroom * FastTanh((magnitude - LimiterKnee) / headroom);
    return x < 0.0f ? -shaped : shaped;
}

// Four-point Hermite interpolation; the catalog is mono 44.1 kHz and devices usually run at 48 kHz.
std::vector<float> Resample(const std::vector<float>& input, std::uint32_t fromRate, std::uint32_t toRate) {
    if (fromRate == toRate || input.empty() || fromRate == 0 || toRate == 0) {
        return input;
    }
    const double step = static_cast<double>(fromRate) / static_cast<double>(toRate);
    const auto outputLength = static_cast<std::size_t>(static_cast<double>(input.size()) / step);
    std::vector<float> output(outputLength);
    const auto at = [&](std::ptrdiff_t index) {
        return index < 0 || index >= static_cast<std::ptrdiff_t>(input.size()) ? 0.0f : input[static_cast<std::size_t>(index)];
    };
    for (std::size_t i = 0; i < outputLength; ++i) {
        const double position = static_cast<double>(i) * step;
        const auto index = static_cast<std::ptrdiff_t>(position);
        const auto t = static_cast<float>(position - static_cast<double>(index));
        const float y0 = at(index - 1);
        const float y1 = at(index);
        const float y2 = at(index + 1);
        const float y3 = at(index + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        output[i] = ((c3 * t + c2) * t + c1) * t + y1;
    }
    return output;
}

class DeviceNotifier final : public IMMNotificationClient {
public:
    DeviceNotifier(std::atomic<bool>& changed, HANDLE wake) : changed_(changed), wake_(wake) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG refs = InterlockedDecrement(&refs_);
        if (refs == 0) {
            delete this;
        }
        return static_cast<ULONG>(refs);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
            *object = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) {
            changed_ = true;
            SetEvent(wake_);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    ~DeviceNotifier() = default;

    LONG refs_{1};
    std::atomic<bool>& changed_;
    HANDLE wake_;
};

enum class SampleFormat {
    Float32,
    Int16
};

bool ParseMixFormat(const WAVEFORMATEX* format, SampleFormat& sampleFormat) {
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && format->wBitsPerSample == 32) {
        sampleFormat = SampleFormat::Float32;
        return true;
    }
    if (format->wFormatTag == WAVE_FORMAT_PCM && format->wBitsPerSample == 16) {
        sampleFormat = SampleFormat::Int16;
        return true;
    }
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        if (IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) && format->wBitsPerSample == 32) {
            sampleFormat = SampleFormat::Float32;
            return true;
        }
        if (IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) && format->wBitsPerSample == 16) {
            sampleFormat = SampleFormat::Int16;
            return true;
        }
    }
    return false;
}

} // namespace

struct AudioEngine::Impl {
    struct Sound {
        AudioData source;
        float volume{1.0f};
        std::shared_ptr<const std::vector<float>> rendered;
        std::uint32_t renderedRate{0};
    };
    struct Voice {
        std::shared_ptr<const std::vector<float>> samples;
        std::size_t position{0};
        float gain{1.0f};
    };

    std::thread thread;
    HANDLE wake{};
    HANDLE bufferEvent{};
    std::atomic<bool> stop{false};
    std::atomic<bool> deviceChanged{false};
    std::atomic<float> masterVolume{0.8f};

    std::mutex mutex;
    std::map<SoundId, Sound> sounds;
    std::vector<SoundId> pending;
    std::uint32_t deviceRate{0};

    // Render thread only.
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> render;
    UINT32 bufferFrames{0};
    UINT32 channels{2};
    SampleFormat sampleFormat{SampleFormat::Float32};
    std::vector<Voice> voices;
    std::vector<float> mix;

    void Run();
    bool Open();
    void Close();
    void RenderAvailable();
    void DrainPending();
    void Write(BYTE* data, UINT32 frames);
};

void AudioEngine::Impl::Run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    DeviceNotifier* notifier = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
        notifier = new DeviceNotifier(deviceChanged, wake);
        if (FAILED(enumerator->RegisterEndpointNotificationCallback(notifier))) {
            notifier->Release();
            notifier = nullptr;
        }
    }

    while (!stop) {
        if (deviceChanged.exchange(false) || !client) {
            if (!Open()) {
                // No usable output yet (unplugged, disabled); retry quietly.
                WaitForSingleObject(wake, 1000);
                continue;
            }
        }
        const HANDLE handles[2]{bufferEvent, wake};
        const DWORD signaled = WaitForMultipleObjects(2, handles, FALSE, 200);
        if (stop) {
            break;
        }
        if (signaled == WAIT_OBJECT_0 + 1 && deviceChanged) {
            continue;
        }
        RenderAvailable();
    }

    Close();
    if (notifier) {
        enumerator->UnregisterEndpointNotificationCallback(notifier);
        notifier->Release();
    }
    enumerator.Reset();
    if (task) {
        AvRevertMmThreadCharacteristics(task);
    }
    CoUninitialize();
}

bool AudioEngine::Impl::Open() {
    Close();
    if (!enumerator) {
        return false;
    }
    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) ||
        FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.ReleaseAndGetAddressOf())))) {
        client.Reset();
        return false;
    }

    WAVEFORMATEX* format = nullptr;
    if (FAILED(client->GetMixFormat(&format)) || !format) {
        client.Reset();
        return false;
    }
    SampleFormat parsed{};
    if (!ParseMixFormat(format, parsed)) {
        CoTaskMemFree(format);
        client.Reset();
        return false;
    }
    sampleFormat = parsed;
    channels = format->nChannels;
    const std::uint32_t rate = format->nSamplesPerSec;

    // Windows 10 can run shared mode at the engine's minimum period (often 2-3 ms instead of 10 ms).
    bool initialized = false;
    ComPtr<IAudioClient3> client3;
    if (SUCCEEDED(client.As(&client3))) {
        UINT32 defaultPeriod = 0;
        UINT32 fundamentalPeriod = 0;
        UINT32 minPeriod = 0;
        UINT32 maxPeriod = 0;
        if (SUCCEEDED(client3->GetSharedModeEnginePeriod(format, &defaultPeriod, &fundamentalPeriod, &minPeriod, &maxPeriod)) &&
            SUCCEEDED(client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minPeriod, format, nullptr))) {
            initialized = true;
        }
    }
    client3.Reset();
    if (!initialized) {
        client.Reset();
        if (SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.ReleaseAndGetAddressOf())))) {
            initialized = SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, format, nullptr));
        }
    }
    CoTaskMemFree(format);

    if (!initialized ||
        FAILED(client->SetEventHandle(bufferEvent)) ||
        FAILED(client->GetBufferSize(&bufferFrames)) ||
        FAILED(client->GetService(IID_PPV_ARGS(&render))) ||
        FAILED(client->Start())) {
        Close();
        return false;
    }

    {
        std::lock_guard lock(mutex);
        deviceRate = rate;
        pending.clear();
        for (auto& [id, sound] : sounds) {
            if (sound.renderedRate != rate) {
                sound.rendered = std::make_shared<const std::vector<float>>(Resample(sound.source.samples, sound.source.sampleRate, rate));
                sound.renderedRate = rate;
            }
        }
    }
    voices.clear();
    return true;
}

void AudioEngine::Impl::Close() {
    if (client) {
        client->Stop();
    }
    render.Reset();
    client.Reset();
    voices.clear();
}

void AudioEngine::Impl::DrainPending() {
    std::lock_guard lock(mutex);
    for (const SoundId id : pending) {
        auto it = sounds.find(id);
        if (it == sounds.end()) {
            continue;
        }
        Sound& sound = it->second;
        if (sound.renderedRate != deviceRate) {
            sound.rendered = std::make_shared<const std::vector<float>>(Resample(sound.source.samples, sound.source.sampleRate, deviceRate));
            sound.renderedRate = deviceRate;
        }
        if (voices.size() >= MaxVoices) {
            voices.erase(voices.begin());
        }
        voices.push_back(Voice{sound.rendered, 0, sound.volume});
    }
    pending.clear();
}

void AudioEngine::Impl::RenderAvailable() {
    if (!client || !render) {
        return;
    }
    UINT32 padding = 0;
    if (FAILED(client->GetCurrentPadding(&padding))) {
        deviceChanged = true;
        return;
    }
    const UINT32 frames = bufferFrames > padding ? bufferFrames - padding : 0;
    if (frames == 0) {
        return;
    }
    BYTE* data = nullptr;
    if (FAILED(render->GetBuffer(frames, &data))) {
        deviceChanged = true;
        return;
    }
    DrainPending();
    if (voices.empty()) {
        render->ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT);
        return;
    }
    Write(data, frames);
    if (FAILED(render->ReleaseBuffer(frames, 0))) {
        deviceChanged = true;
    }
}

void AudioEngine::Impl::Write(BYTE* data, UINT32 frames) {
    mix.assign(frames, 0.0f);
    for (Voice& voice : voices) {
        const std::vector<float>& samples = *voice.samples;
        const std::size_t count = std::min<std::size_t>(frames, samples.size() - voice.position);
        const float* source = samples.data() + voice.position;
        for (std::size_t i = 0; i < count; ++i) {
            mix[i] += source[i] * voice.gain;
        }
        voice.position += count;
    }
    voices.erase(std::remove_if(voices.begin(), voices.end(), [](const Voice& voice) {
        return voice.position >= voice.samples->size();
    }), voices.end());

    // Mono content goes to the front pair; surround channels stay silent.
    const float master = masterVolume.load(std::memory_order_relaxed);
    const UINT32 audible = std::min<UINT32>(channels, 2);
    if (sampleFormat == SampleFormat::Float32) {
        auto* out = reinterpret_cast<float*>(data);
        for (UINT32 frame = 0; frame < frames; ++frame) {
            const float value = SoftLimit(mix[frame] * master);
            for (UINT32 channel = 0; channel < channels; ++channel) {
                *out++ = channel < audible ? value : 0.0f;
            }
        }
    } else {
        auto* out = reinterpret_cast<std::int16_t*>(data);
        for (UINT32 frame = 0; frame < frames; ++frame) {
            const auto value = static_cast<std::int16_t>(SoftLimit(mix[frame] * master) * 32767.0f);
            for (UINT32 channel = 0; channel < channels; ++channel) {
                *out++ = channel < audible ? value : std::int16_t{0};
            }
        }
    }
}

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    if (impl_) {
        impl_->stop = true;
        SetEvent(impl_->wake);
        if (impl_->thread.joinable()) {
            impl_->thread.join();
        }
        CloseHandle(impl_->wake);
        CloseHandle(impl_->bufferEvent);
        impl_.reset();
    }
    if (mediaFoundationStarted_) {
        MFShutdown();
        mediaFoundationStarted_ = false;
    }
}

bool AudioEngine::Initialize() {
    if (impl_) {
        return true;
    }
    if (!mediaFoundationStarted_) {
        if (FAILED(MFStartup(MF_VERSION))) {
            return false;
        }
        mediaFoundationStarted_ = true;
    }
    auto impl = std::make_unique<Impl>();
    impl->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    impl->bufferEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!impl->wake || !impl->bufferEvent) {
        if (impl->wake) {
            CloseHandle(impl->wake);
        }
        if (impl->bufferEvent) {
            CloseHandle(impl->bufferEvent);
        }
        return false;
    }
    impl_ = std::move(impl);
    impl_->thread = std::thread([impl = impl_.get()] { impl->Run(); });
    return true;
}

void AudioEngine::LoadAllFromResources() {
    if (!Initialize()) {
        return;
    }
    std::uint32_t rate = 0;
    {
        std::lock_guard lock(impl_->mutex);
        rate = impl_->deviceRate;
    }
    for (const SoundCatalogEntry& entry : SoundCatalog()) {
        const auto bytes = resources::LoadResourceBytes(entry.resourceId);
        Impl::Sound sound;
        if (!DecodeMp3ToPcm(bytes, sound.source)) {
            continue;
        }
        sound.volume = entry.recommendedVolume;
        if (rate != 0) {
            sound.rendered = std::make_shared<const std::vector<float>>(Resample(sound.source.samples, sound.source.sampleRate, rate));
            sound.renderedRate = rate;
        }
        std::lock_guard lock(impl_->mutex);
        impl_->sounds[entry.id] = std::move(sound);
    }
}

void AudioEngine::SetMasterVolume(float volume) {
    if (impl_) {
        impl_->masterVolume = std::clamp(volume, 0.0f, 1.0f);
    }
}

void AudioEngine::Play(SoundId id) {
    if (!impl_) {
        return;
    }
    std::lock_guard lock(impl_->mutex);
    // Without an open device nothing drains the queue; keep only the latest requests.
    if (impl_->pending.size() >= MaxVoices) {
        impl_->pending.erase(impl_->pending.begin());
    }
    impl_->pending.push_back(id);
}

bool AudioEngine::Available() const {
    return impl_ != nullptr;
}

} // namespace pdk::audio
