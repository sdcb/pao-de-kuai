#include "audio/AudioEngine.h"

#include "audio/AudioDecoder.h"
#include "audio/MfCompat.h"
#include "audio/SoundCatalog.h"
#include "graphics/Com.h"
#include "resources/ResourceLoader.h"

#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <ksmedia.h>
#include <mfapi.h>
#include <mmdeviceapi.h>

/*
 * Ported from the pimpl-based C++ engine without changing the audio path:
 *   - std::shared_ptr<const std::vector<float>> -> refcounted SampleBuffer, so a
 *     voice and the sound table can share one rendered buffer;
 *   - std::mutex -> CRITICAL_SECTION, std::thread -> CreateThread;
 *   - std::atomic<bool>/<float> -> interlocked LONG (the float is kept as its bit
 *     pattern, since there is no 32-bit interlocked float exchange);
 *   - std::map<SoundId, Sound> -> a table indexed by SoundId;
 *   - the voice list and the pending queue are fixed arrays of MaxVoices.
 */

enum { PDK_MAX_VOICES = 24 };
#define PDK_LIMITER_KNEE 0.8f

/* ---- shared sample buffer -------------------------------------------- */

typedef struct SampleBuffer {
    volatile LONG refs;
    int count;
    float *data;
} SampleBuffer;

static SampleBuffer *SampleBuffer_Create(int count)
{
    SampleBuffer *buffer = (SampleBuffer *)malloc(sizeof(SampleBuffer));
    if (buffer == NULL) {
        return NULL;
    }
    buffer->refs = 1;
    buffer->count = count;
    buffer->data = count > 0 ? (float *)malloc((size_t)count * sizeof(float)) : NULL;
    if (count > 0 && buffer->data == NULL) {
        free(buffer);
        return NULL;
    }
    return buffer;
}

static SampleBuffer *SampleBuffer_Retain(SampleBuffer *buffer)
{
    if (buffer != NULL) {
        InterlockedIncrement(&buffer->refs);
    }
    return buffer;
}

static void SampleBuffer_Release(SampleBuffer *buffer)
{
    if (buffer != NULL && InterlockedDecrement(&buffer->refs) == 0) {
        free(buffer->data);
        free(buffer);
    }
}

/* ---- float atomics --------------------------------------------------- */

static float LoadFloat(const volatile LONG *bits)
{
    LONG raw = InterlockedCompareExchange((volatile LONG *)bits, 0, 0);
    float value;
    memcpy(&value, &raw, sizeof(value));
    return value;
}

static void StoreFloat(volatile LONG *bits, float value)
{
    LONG raw;
    memcpy(&raw, &value, sizeof(raw));
    InterlockedExchange(bits, raw);
}

/* ---- float helpers --------------------------------------------------- */

static float Clampf(float value, float low, float high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

/* Pade approximation of tanh; std::tanh would pull in CRT math this build does not
 * need otherwise. */
static float FastTanh(float x)
{
    float x2;
    x = Clampf(x, -3.0f, 3.0f);
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* Unity below the knee, then a tanh shoulder that approaches full scale without
 * clipping. */
static float SoftLimit(float x)
{
    const float magnitude = x < 0.0f ? -x : x;
    float headroom;
    float shaped;

    if (magnitude <= PDK_LIMITER_KNEE) {
        return x;
    }
    headroom = 1.0f - PDK_LIMITER_KNEE;
    shaped = PDK_LIMITER_KNEE + headroom * FastTanh((magnitude - PDK_LIMITER_KNEE) / headroom);
    return x < 0.0f ? -shaped : shaped;
}

/* Four-point Hermite interpolation; the catalog is mono 44.1 kHz and devices
 * usually run at 48 kHz.  Always returns a new buffer, including a plain copy when
 * the rates already match, because the caller stores it as "the rendered version
 * for this device rate" and checks for NULL to mean failure.  NULL therefore only
 * means "nothing to render" or "out of memory". */
static SampleBuffer *Resample(const float *input, int inputCount,
                              uint32_t fromRate, uint32_t toRate)
{
    double step;
    int outputLength;
    SampleBuffer *output;

    if (inputCount <= 0 || fromRate == 0 || toRate == 0) {
        return NULL;
    }
    if (fromRate == toRate) {
        output = SampleBuffer_Create(inputCount);
        if (output != NULL) {
            memcpy(output->data, input, (size_t)inputCount * sizeof(float));
        }
        return output;
    }
    step = (double)fromRate / (double)toRate;
    outputLength = (int)((double)inputCount / step);
    if (outputLength <= 0) {
        return NULL;
    }
    output = SampleBuffer_Create(outputLength);
    if (output == NULL) {
        return NULL;
    }
    for (int i = 0; i < outputLength; ++i) {
        const double position = (double)i * step;
        const int index = (int)position;
        const float t = (float)(position - (double)index);
        float y0;
        float y1;
        float y2;
        float y3;
        float c1;
        float c2;
        float c3;

        y0 = (index - 1 < 0) ? 0.0f : input[index - 1];
        y1 = (index < 0 || index >= inputCount) ? 0.0f : input[index];
        y2 = (index + 1 >= inputCount) ? 0.0f : input[index + 1];
        y3 = (index + 2 >= inputCount) ? 0.0f : input[index + 2];
        c1 = 0.5f * (y2 - y0);
        c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        output->data[i] = ((c3 * t + c2) * t + c1) * t + y1;
    }
    return output;
}

/* ---- device change notification -------------------------------------- */

/*
 * The one COM callback the application has to implement itself (plan.md 3.3).
 * The vtable is spelled out in full and its size is asserted against the SDK's own
 * C-mode vtable below, so a layout mistake fails the build on both toolchains.
 */
typedef struct PdkDeviceNotifier PdkDeviceNotifier;

typedef struct PdkDeviceNotifierVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(PdkDeviceNotifier *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(PdkDeviceNotifier *This);
    ULONG (STDMETHODCALLTYPE *Release)(PdkDeviceNotifier *This);
    HRESULT (STDMETHODCALLTYPE *OnDeviceStateChanged)(PdkDeviceNotifier *This, LPCWSTR deviceId, DWORD newState);
    HRESULT (STDMETHODCALLTYPE *OnDeviceAdded)(PdkDeviceNotifier *This, LPCWSTR deviceId);
    HRESULT (STDMETHODCALLTYPE *OnDeviceRemoved)(PdkDeviceNotifier *This, LPCWSTR deviceId);
    HRESULT (STDMETHODCALLTYPE *OnDefaultDeviceChanged)(PdkDeviceNotifier *This, EDataFlow flow, ERole role, LPCWSTR defaultDeviceId);
    HRESULT (STDMETHODCALLTYPE *OnPropertyValueChanged)(PdkDeviceNotifier *This, LPCWSTR deviceId, const PROPERTYKEY key);
} PdkDeviceNotifierVtbl;

_Static_assert(sizeof(PdkDeviceNotifierVtbl) == sizeof(IMMNotificationClientVtbl),
               "PDK device notifier vtable must match IMMNotificationClientVtbl");

struct PdkDeviceNotifier {
    const PdkDeviceNotifierVtbl *lpVtbl;
    volatile LONG refs;
    volatile LONG *deviceChanged;
    HANDLE wake;
};

static ULONG STDMETHODCALLTYPE Notifier_AddRef(PdkDeviceNotifier *This);
static ULONG STDMETHODCALLTYPE Notifier_Release(PdkDeviceNotifier *This);

static HRESULT STDMETHODCALLTYPE Notifier_QueryInterface(PdkDeviceNotifier *This, REFIID riid, void **object)
{
    if (object == NULL) {
        return E_POINTER;
    }
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMMNotificationClient)) {
        *object = This;
        Notifier_AddRef(This);
        return S_OK;
    }
    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE Notifier_AddRef(PdkDeviceNotifier *This)
{
    return (ULONG)InterlockedIncrement(&This->refs);
}

static ULONG STDMETHODCALLTYPE Notifier_Release(PdkDeviceNotifier *This)
{
    const LONG refs = InterlockedDecrement(&This->refs);
    if (refs == 0) {
        free(This);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE Notifier_OnDeviceStateChanged(PdkDeviceNotifier *This, LPCWSTR deviceId, DWORD newState)
{
    PDK_UNUSED(This);
    PDK_UNUSED(deviceId);
    PDK_UNUSED(newState);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Notifier_OnDeviceAdded(PdkDeviceNotifier *This, LPCWSTR deviceId)
{
    PDK_UNUSED(This);
    PDK_UNUSED(deviceId);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Notifier_OnDeviceRemoved(PdkDeviceNotifier *This, LPCWSTR deviceId)
{
    PDK_UNUSED(This);
    PDK_UNUSED(deviceId);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Notifier_OnDefaultDeviceChanged(PdkDeviceNotifier *This, EDataFlow flow, ERole role, LPCWSTR defaultDeviceId)
{
    PDK_UNUSED(defaultDeviceId);
    if (flow == eRender && role == eConsole) {
        InterlockedExchange(This->deviceChanged, 1);
        SetEvent(This->wake);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Notifier_OnPropertyValueChanged(PdkDeviceNotifier *This, LPCWSTR deviceId, const PROPERTYKEY key)
{
    PDK_UNUSED(This);
    PDK_UNUSED(deviceId);
    PDK_UNUSED(key);
    return S_OK;
}

static const PdkDeviceNotifierVtbl kDeviceNotifierVtbl = {
    Notifier_QueryInterface,
    Notifier_AddRef,
    Notifier_Release,
    Notifier_OnDeviceStateChanged,
    Notifier_OnDeviceAdded,
    Notifier_OnDeviceRemoved,
    Notifier_OnDefaultDeviceChanged,
    Notifier_OnPropertyValueChanged
};

static PdkDeviceNotifier *Notifier_Create(volatile LONG *deviceChanged, HANDLE wake)
{
    PdkDeviceNotifier *notifier = (PdkDeviceNotifier *)malloc(sizeof(PdkDeviceNotifier));
    if (notifier == NULL) {
        return NULL;
    }
    notifier->lpVtbl = &kDeviceNotifierVtbl;
    notifier->refs = 1;
    notifier->deviceChanged = deviceChanged;
    notifier->wake = wake;
    return notifier;
}

/* ---- sample format --------------------------------------------------- */

typedef enum SampleFormat {
    SAMPLE_FORMAT_FLOAT32,
    SAMPLE_FORMAT_INT16
} SampleFormat;

static bool ParseMixFormat(const WAVEFORMATEX *format, SampleFormat *sampleFormat)
{
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && format->wBitsPerSample == 32) {
        *sampleFormat = SAMPLE_FORMAT_FLOAT32;
        return true;
    }
    if (format->wFormatTag == WAVE_FORMAT_PCM && format->wBitsPerSample == 16) {
        *sampleFormat = SAMPLE_FORMAT_INT16;
        return true;
    }
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22) {
        const WAVEFORMATEXTENSIBLE *extensible = (const WAVEFORMATEXTENSIBLE *)format;
        if (IsEqualGUID(&extensible->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) &&
            format->wBitsPerSample == 32) {
            *sampleFormat = SAMPLE_FORMAT_FLOAT32;
            return true;
        }
        if (IsEqualGUID(&extensible->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM) &&
            format->wBitsPerSample == 16) {
            *sampleFormat = SAMPLE_FORMAT_INT16;
            return true;
        }
    }
    return false;
}

/* ---- engine state ---------------------------------------------------- */

typedef struct Sound {
    AudioData source;
    float volume;
    SampleBuffer *rendered;
    uint32_t renderedRate;
} Sound;

typedef struct Voice {
    SampleBuffer *samples;
    int position;
    float gain;
} Voice;

struct AudioEngineState {
    HANDLE thread;
    HANDLE wake;
    HANDLE bufferEvent;
    volatile LONG stop;
    volatile LONG deviceChanged;
    volatile LONG masterVolumeBits;

    CRITICAL_SECTION lock;
    bool lockReady;
    Sound sounds[SOUND_COUNT];
    SoundId pending[PDK_MAX_VOICES];
    int pendingCount;
    uint32_t deviceRate;

    /* Render thread only. */
    IMMDeviceEnumerator *enumerator;
    IAudioClient *client;
    IAudioRenderClient *render;
    UINT32 bufferFrames;
    UINT32 channels;
    SampleFormat sampleFormat;
    Voice voices[PDK_MAX_VOICES];
    int voiceCount;
    float *mix;
    int mixCapacity;
};

static void DrainPending(AudioEngineState *state);

static bool OpenDevice(AudioEngineState *state)
{
    IMMDevice *device = NULL;
    WAVEFORMATEX *format = NULL;
    SampleFormat parsed = SAMPLE_FORMAT_FLOAT32;
    uint32_t rate = 0;
    bool initialized = false;

    /* Close first, like the C++ version did. */
    if (state->client != NULL) {
        PDK_CALL0(state->client, Stop);
    }
    PDK_RELEASE(state->render);
    PDK_RELEASE(state->client);
    state->voiceCount = 0;

    if (state->enumerator == NULL) {
        return false;
    }
    if (FAILED(PDK_CALL(state->enumerator, GetDefaultAudioEndpoint, eRender, eConsole, &device)) ||
        device == NULL) {
        return false;
    }
    if (FAILED(PDK_CALL(device, Activate, &IID_IAudioClient, CLSCTX_ALL, NULL,
                        (void **)&state->client))) {
        PDK_RELEASE_VALUE(device);
        return false;
    }
    if (FAILED(PDK_CALL(state->client, GetMixFormat, &format)) || format == NULL) {
        PDK_RELEASE_VALUE(device);
        PDK_RELEASE(state->client);
        return false;
    }
    if (!ParseMixFormat(format, &parsed)) {
        CoTaskMemFree(format);
        PDK_RELEASE_VALUE(device);
        PDK_RELEASE(state->client);
        return false;
    }
    state->sampleFormat = parsed;
    state->channels = format->nChannels;
    rate = format->nSamplesPerSec;

    /* Windows 10 can run shared mode at the engine's minimum period (often 2-3 ms
     * instead of 10 ms).  IAudioClient3 is Win10-only, which matches the new
     * minimum supported system (plan.md revision 2). */
    {
        IAudioClient3 *client3 = NULL;
        if (SUCCEEDED(PDK_QUERY(state->client, &IID_IAudioClient3, &client3)) && client3 != NULL) {
            UINT32 defaultPeriod = 0;
            UINT32 fundamentalPeriod = 0;
            UINT32 minPeriod = 0;
            UINT32 maxPeriod = 0;
            if (SUCCEEDED(PDK_CALL(client3, GetSharedModeEnginePeriod, format, &defaultPeriod,
                                   &fundamentalPeriod, &minPeriod, &maxPeriod)) &&
                SUCCEEDED(PDK_CALL(client3, InitializeSharedAudioStream,
                                   AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minPeriod, format, NULL))) {
                initialized = true;
            }
            PDK_RELEASE(client3);
        }
    }
    if (!initialized) {
        PDK_RELEASE(state->client);
        if (SUCCEEDED(PDK_CALL(device, Activate, &IID_IAudioClient, CLSCTX_ALL, NULL,
                               (void **)&state->client))) {
            initialized = SUCCEEDED(PDK_CALL(state->client, Initialize, AUDCLNT_SHAREMODE_SHARED,
                                             AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, format, NULL));
        }
    }
    CoTaskMemFree(format);
    PDK_RELEASE_VALUE(device);

    if (!initialized ||
        FAILED(PDK_CALL(state->client, SetEventHandle, state->bufferEvent)) ||
        FAILED(PDK_CALL(state->client, GetBufferSize, &state->bufferFrames)) ||
        FAILED(PDK_CALL(state->client, GetService, &IID_IAudioRenderClient, (void **)&state->render)) ||
        FAILED(PDK_CALL0(state->client, Start))) {
        PDK_RELEASE(state->render);
        PDK_RELEASE(state->client);
        state->voiceCount = 0;
        return false;
    }

    EnterCriticalSection(&state->lock);
    state->deviceRate = rate;
    state->pendingCount = 0;
    for (int id = 0; id < SOUND_COUNT; ++id) {
        Sound *sound = &state->sounds[id];
        if (sound->renderedRate != rate && sound->source.count > 0) {
            SampleBuffer *rendered = Resample(sound->source.samples, sound->source.count,
                                              sound->source.sampleRate, rate);
            if (rendered != NULL) {
                SampleBuffer_Release(sound->rendered);
                sound->rendered = rendered;
                sound->renderedRate = rate;
            }
        }
    }
    LeaveCriticalSection(&state->lock);
    return true;
}

static void WriteFrames(AudioEngineState *state, BYTE *data, UINT32 frames)
{
    const float master = LoadFloat(&state->masterVolumeBits);
    const UINT32 audible = state->channels < 2 ? state->channels : 2;

    if ((int)frames > state->mixCapacity) {
        float *grown = (float *)realloc(state->mix, (size_t)frames * sizeof(float));
        if (grown == NULL) {
            return;
        }
        state->mix = grown;
        state->mixCapacity = (int)frames;
    }
    memset(state->mix, 0, (size_t)frames * sizeof(float));

    for (int v = 0; v < state->voiceCount; ++v) {
        Voice *voice = &state->voices[v];
        const int available = voice->samples->count - voice->position;
        const int count = (int)frames < available ? (int)frames : available;
        const float *source = voice->samples->data + voice->position;
        for (int i = 0; i < count; ++i) {
            state->mix[i] += source[i] * voice->gain;
        }
        voice->position += count;
    }
    {
        int kept = 0;
        for (int v = 0; v < state->voiceCount; ++v) {
            if (state->voices[v].position < state->voices[v].samples->count) {
                if (kept != v) {
                    state->voices[kept] = state->voices[v];
                }
                ++kept;
            } else {
                SampleBuffer_Release(state->voices[v].samples);
            }
        }
        state->voiceCount = kept;
    }

    /* Mono content goes to the front pair; surround channels stay silent. */
    if (state->sampleFormat == SAMPLE_FORMAT_FLOAT32) {
        float *out = (float *)data;
        for (UINT32 frame = 0; frame < frames; ++frame) {
            const float value = SoftLimit(state->mix[frame] * master);
            for (UINT32 channel = 0; channel < state->channels; ++channel) {
                *out++ = channel < audible ? value : 0.0f;
            }
        }
    } else {
        int16_t *out = (int16_t *)data;
        for (UINT32 frame = 0; frame < frames; ++frame) {
            const int16_t value = (int16_t)(SoftLimit(state->mix[frame] * master) * 32767.0f);
            for (UINT32 channel = 0; channel < state->channels; ++channel) {
                *out++ = channel < audible ? value : (int16_t)0;
            }
        }
    }
}

static void RenderAvailable(AudioEngineState *state)
{
    UINT32 padding = 0;
    UINT32 frames;
    BYTE *data = NULL;

    if (state->client == NULL || state->render == NULL) {
        return;
    }
    if (FAILED(PDK_CALL(state->client, GetCurrentPadding, &padding))) {
        InterlockedExchange(&state->deviceChanged, 1);
        return;
    }
    frames = state->bufferFrames > padding ? state->bufferFrames - padding : 0;
    if (frames == 0) {
        return;
    }
    if (FAILED(PDK_CALL(state->render, GetBuffer, frames, &data)) || data == NULL) {
        InterlockedExchange(&state->deviceChanged, 1);
        return;
    }
    DrainPending(state);
    if (state->voiceCount == 0) {
        PDK_CALL(state->render, ReleaseBuffer, frames, AUDCLNT_BUFFERFLAGS_SILENT);
        return;
    }
    WriteFrames(state, data, frames);
    if (FAILED(PDK_CALL(state->render, ReleaseBuffer, frames, 0))) {
        InterlockedExchange(&state->deviceChanged, 1);
    }
}

static void DrainPending(AudioEngineState *state)
{
    EnterCriticalSection(&state->lock);
    for (int i = 0; i < state->pendingCount; ++i) {
        Sound *sound = &state->sounds[state->pending[i]];
        /* Mirrors the C++ version: whenever the rendered buffer is stale for the
         * current device rate, re-render it (Resample copies when rates match). */
        if (sound->source.count > 0 && sound->renderedRate != state->deviceRate) {
            SampleBuffer *rendered = Resample(sound->source.samples, sound->source.count,
                                              sound->source.sampleRate, state->deviceRate);
            if (rendered != NULL) {
                SampleBuffer_Release(sound->rendered);
                sound->rendered = rendered;
                sound->renderedRate = state->deviceRate;
            }
        }
        if (sound->rendered == NULL) {
            continue;
        }
        if (state->voiceCount >= PDK_MAX_VOICES) {
            /* Drop the oldest voice, like the vector erase(begin) used to. */
            SampleBuffer_Release(state->voices[0].samples);
            for (int v = 1; v < state->voiceCount; ++v) {
                state->voices[v - 1] = state->voices[v];
            }
            --state->voiceCount;
        }
        state->voices[state->voiceCount].samples = SampleBuffer_Retain(sound->rendered);
        state->voices[state->voiceCount].position = 0;
        state->voices[state->voiceCount].gain = sound->volume;
        ++state->voiceCount;
    }
    state->pendingCount = 0;
    LeaveCriticalSection(&state->lock);
}

static DWORD WINAPI RenderThreadProc(LPVOID parameter)
{
    AudioEngineState *state = (AudioEngineState *)parameter;
    PdkDeviceNotifier *notifier = NULL;
    DWORD taskIndex = 0;
    HANDLE task;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    if (SUCCEEDED(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                                   &IID_IMMDeviceEnumerator, (void **)&state->enumerator)) &&
        state->enumerator != NULL) {
        notifier = Notifier_Create(&state->deviceChanged, state->wake);
        if (notifier != NULL &&
            FAILED(PDK_CALL(state->enumerator, RegisterEndpointNotificationCallback, (IMMNotificationClient *)notifier))) {
            Notifier_Release(notifier);
            notifier = NULL;
        }
    }

    while (state->stop == 0) {
        if (InterlockedExchange(&state->deviceChanged, 0) != 0 || state->client == NULL) {
            if (!OpenDevice(state)) {
                /* No usable output yet (unplugged, disabled); retry quietly. */
                WaitForSingleObject(state->wake, 1000);
                continue;
            }
        }
        {
            const HANDLE handles[2] = {state->bufferEvent, state->wake};
            const DWORD signaled = WaitForMultipleObjects(2, handles, FALSE, 200);
            if (state->stop != 0) {
                break;
            }
            if (signaled == WAIT_OBJECT_0 + 1 && state->deviceChanged != 0) {
                continue;
            }
        }
        RenderAvailable(state);
    }

    if (state->client != NULL) {
        PDK_CALL0(state->client, Stop);
    }
    PDK_RELEASE(state->render);
    PDK_RELEASE(state->client);
    state->voiceCount = 0;
    if (notifier != NULL) {
        if (state->enumerator != NULL) {
            PDK_CALL(state->enumerator, UnregisterEndpointNotificationCallback, (IMMNotificationClient *)notifier);
        }
        Notifier_Release(notifier);
    }
    PDK_RELEASE(state->enumerator);
    if (task != NULL) {
        AvRevertMmThreadCharacteristics(task);
    }
    CoUninitialize();
    return 0;
}

/* ---- public API ------------------------------------------------------ */

void AudioEngine_Init(AudioEngine *engine)
{
    engine->state = NULL;
    engine->mediaFoundationStarted = false;
}

void AudioEngine_Destroy(AudioEngine *engine)
{
    AudioEngineState *state = engine->state;

    if (state != NULL) {
        InterlockedExchange(&state->stop, 1);
        SetEvent(state->wake);
        if (state->thread != NULL) {
            WaitForSingleObject(state->thread, INFINITE);
            CloseHandle(state->thread);
        }
        CloseHandle(state->wake);
        CloseHandle(state->bufferEvent);
        for (int id = 0; id < SOUND_COUNT; ++id) {
            SampleBuffer_Release(state->sounds[id].rendered);
            AudioData_Free(&state->sounds[id].source);
        }
        for (int v = 0; v < state->voiceCount; ++v) {
            SampleBuffer_Release(state->voices[v].samples);
        }
        free(state->mix);
        if (state->lockReady) {
            DeleteCriticalSection(&state->lock);
        }
        free(state);
        engine->state = NULL;
    }
    if (engine->mediaFoundationStarted) {
        MFShutdown();
        engine->mediaFoundationStarted = false;
    }
}

bool AudioEngine_Initialize(AudioEngine *engine)
{
    AudioEngineState *state;

    if (engine->state != NULL) {
        return true;
    }
    if (!engine->mediaFoundationStarted) {
        if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) {
            return false;
        }
        engine->mediaFoundationStarted = true;
    }
    state = (AudioEngineState *)calloc(1, sizeof(AudioEngineState));
    if (state == NULL) {
        return false;
    }
    state->masterVolumeBits = 0;
    StoreFloat(&state->masterVolumeBits, 0.8f);
    InitializeCriticalSection(&state->lock);
    state->lockReady = true;
    state->wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    state->bufferEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (state->wake == NULL || state->bufferEvent == NULL) {
        if (state->wake != NULL) {
            CloseHandle(state->wake);
        }
        if (state->bufferEvent != NULL) {
            CloseHandle(state->bufferEvent);
        }
        DeleteCriticalSection(&state->lock);
        free(state);
        return false;
    }
    engine->state = state;
    state->thread = CreateThread(NULL, 0, RenderThreadProc, state, 0, NULL);
    if (state->thread == NULL) {
        CloseHandle(state->wake);
        CloseHandle(state->bufferEvent);
        DeleteCriticalSection(&state->lock);
        free(state);
        engine->state = NULL;
        return false;
    }
    return true;
}

void AudioEngine_LoadAllFromResources(AudioEngine *engine)
{
    int count = 0;
    const SoundCatalogEntry *catalog;
    uint32_t rate;

    if (!AudioEngine_Initialize(engine)) {
        return;
    }
    catalog = SoundCatalog_Entries(&count);
    EnterCriticalSection(&engine->state->lock);
    rate = engine->state->deviceRate;
    LeaveCriticalSection(&engine->state->lock);

    for (int i = 0; i < count; ++i) {
        const SoundCatalogEntry *entry = &catalog[i];
        ByteBuffer bytes;
        Sound sound;

        memset(&sound, 0, sizeof(sound));
        AudioData_Init(&sound.source);
        ByteBuffer_Init(&bytes);
        if (!LoadResourceBytes(entry->resourceId, NULL, NULL, &bytes)) {
            continue;
        }
        if (!DecodeMp3ToPcm(bytes.data, bytes.size, &sound.source)) {
            ByteBuffer_Free(&bytes);
            continue;
        }
        ByteBuffer_Free(&bytes);
        sound.volume = entry->recommendedVolume;
        if (rate != 0) {
            sound.rendered = Resample(sound.source.samples, sound.source.count,
                                      sound.source.sampleRate, rate);
            sound.renderedRate = rate;
        }
        EnterCriticalSection(&engine->state->lock);
        SampleBuffer_Release(engine->state->sounds[entry->id].rendered);
        AudioData_Free(&engine->state->sounds[entry->id].source);
        engine->state->sounds[entry->id] = sound;
        LeaveCriticalSection(&engine->state->lock);
    }
}

void AudioEngine_SetMasterVolume(AudioEngine *engine, float volume)
{
    if (engine->state != NULL) {
        StoreFloat(&engine->state->masterVolumeBits, Clampf(volume, 0.0f, 1.0f));
    }
}

void AudioEngine_Play(AudioEngine *engine, SoundId id)
{
    AudioEngineState *state = engine->state;

    if (state == NULL || id >= SOUND_COUNT) {
        return;
    }
    EnterCriticalSection(&state->lock);
    /* Without an open device nothing drains the queue; keep only the latest
     * requests. */
    if (state->pendingCount >= PDK_MAX_VOICES) {
        for (int i = 1; i < state->pendingCount; ++i) {
            state->pending[i - 1] = state->pending[i];
        }
        --state->pendingCount;
    }
    state->pending[state->pendingCount++] = id;
    LeaveCriticalSection(&state->lock);
}

bool AudioEngine_Available(const AudioEngine *engine)
{
    return engine->state != NULL;
}
