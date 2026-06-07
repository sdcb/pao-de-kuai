#include "audio/AudioDecoder.h"

#include <limits>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlwapi.h>
#include <wrl/client.h>

namespace pdk::audio {
namespace {

constexpr UINT32 PcmSampleRate = 44100;
constexpr UINT32 PcmChannels = 1;
constexpr UINT32 PcmBitsPerSample = 16;
constexpr UINT32 PcmBlockAlign = PcmChannels * PcmBitsPerSample / 8;
constexpr UINT32 PcmAvgBytesPerSecond = PcmSampleRate * PcmBlockAlign;

using Microsoft::WRL::ComPtr;

bool ConfigurePcmOutput(IMFSourceReader* reader) {
    ComPtr<IMFMediaType> mediaType;
    if (FAILED(MFCreateMediaType(&mediaType))) {
        return false;
    }
    if (FAILED(mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
        FAILED(mediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) ||
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

void AppendSampleBytes(IMFSample* sample, std::vector<std::uint8_t>& pcm) {
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
    const auto* begin = static_cast<const std::uint8_t*>(data);
    pcm.insert(pcm.end(), begin, begin + currentLength);
    buffer->Unlock();
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

    out.format.wFormatTag = WAVE_FORMAT_PCM;
    out.format.nChannels = static_cast<WORD>(PcmChannels);
    out.format.nSamplesPerSec = PcmSampleRate;
    out.format.nAvgBytesPerSec = PcmAvgBytesPerSecond;
    out.format.nBlockAlign = static_cast<WORD>(PcmBlockAlign);
    out.format.wBitsPerSample = static_cast<WORD>(PcmBitsPerSample);
    out.format.cbSize = 0;

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
            AppendSampleBytes(sample.Get(), out.pcm);
        }
    }

    return !out.pcm.empty();
}

} // namespace pdk::audio
