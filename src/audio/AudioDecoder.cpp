#include "audio/AudioDecoder.h"

#include "audio/MfCompat.h"

#include <limits>

#include <wrl/client.h>

namespace pdk::audio {
namespace {

constexpr UINT32 PcmSampleRate = 44100;
constexpr UINT32 PcmChannels = 1;
constexpr UINT32 PcmBitsPerSample = 32;
constexpr UINT32 PcmBlockAlign = PcmChannels * PcmBitsPerSample / 8;
constexpr UINT32 PcmAvgBytesPerSecond = PcmSampleRate * PcmBlockAlign;

// Media Foundation outputs LAME's encoder delay as leading padding: 577 samples at 44.1 kHz for the
// Xing-less files written by assets/audio/generate_sfx.ps1. Pre-echo from sharp transients can reach
// -12 dB of peak inside that padding, so a level gate cannot find the real start; trim a fixed amount.
constexpr std::size_t Mp3EncoderDelaySamples = 577;
constexpr std::size_t DelayFadeSamples = 32;

using Microsoft::WRL::ComPtr;

bool ConfigurePcmOutput(IMFSourceReader* reader) {
    ComPtr<IMFMediaType> mediaType;
    if (FAILED(MFCreateMediaType(&mediaType))) {
        return false;
    }
    if (FAILED(mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
        FAILED(mediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float)) ||
        FAILED(mediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, PcmChannels)) ||
        FAILED(mediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, PcmSampleRate)) ||
        FAILED(mediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, PcmBitsPerSample)) ||
        FAILED(mediaType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, PcmBlockAlign)) ||
        FAILED(mediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, PcmAvgBytesPerSecond)) ||
        FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, mediaType.Get()))) {
        return false;
    }
    return true;
}

void AppendSamples(IMFSample* sample, std::vector<float>& samples) {
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) {
        return;
    }

    BYTE* data = nullptr;
    DWORD maxLength = 0;
    DWORD currentLength = 0;
    if (FAILED(buffer->Lock(&data, &maxLength, &currentLength))) {
        return;
    }
    (void)maxLength;
    const auto* begin = reinterpret_cast<const float*>(data);
    samples.insert(samples.end(), begin, begin + currentLength / sizeof(float));
    buffer->Unlock();
}

void TrimEncoderDelay(std::vector<float>& samples) {
    constexpr std::size_t trim = Mp3EncoderDelaySamples - DelayFadeSamples;
    if (samples.size() <= Mp3EncoderDelaySamples) {
        return;
    }

    samples.erase(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(trim));
    for (std::size_t i = 0; i < DelayFadeSamples; ++i) {
        samples[i] *= static_cast<float>(i) / static_cast<float>(DelayFadeSamples);
    }
}

} // namespace

bool DecodeMp3ToPcm(std::span<const std::uint8_t> bytes, AudioData& out) {
    out = {};
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<UINT>::max())) {
        return false;
    }

    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size())));
    if (!stream) {
        return false;
    }

    ComPtr<IMFByteStream> byteStream;
    if (FAILED(MFCreateMFByteStreamOnStream(stream.Get(), &byteStream))) {
        return false;
    }

    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromByteStream(byteStream.Get(), nullptr, &reader))) {
        return false;
    }
    if (!ConfigurePcmOutput(reader.Get())) {
        return false;
    }

    out.sampleRate = PcmSampleRate;

    while (true) {
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        const HRESULT hr = reader->ReadSample(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            0,
            &streamIndex,
            &flags,
            &timestamp,
            &sample);
        (void)streamIndex;
        (void)timestamp;
        if (FAILED(hr)) {
            return false;
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            break;
        }
        if (sample) {
            AppendSamples(sample.Get(), out.samples);
        }
    }

    TrimEncoderDelay(out.samples);
    return !out.samples.empty();
}

} // namespace pdk::audio
