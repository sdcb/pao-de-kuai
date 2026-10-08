#include "audio/AudioDecoder.h"

#include "audio/MfCompat.h"
#include "graphics/Com.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
    PDK_PCM_SAMPLE_RATE = 44100,
    PDK_PCM_CHANNELS = 1,
    PDK_PCM_BITS_PER_SAMPLE = 32
};
#define PDK_PCM_BLOCK_ALIGN (PDK_PCM_CHANNELS * PDK_PCM_BITS_PER_SAMPLE / 8)
#define PDK_PCM_AVG_BYTES (PDK_PCM_SAMPLE_RATE * PDK_PCM_BLOCK_ALIGN)

/*
 * Media Foundation outputs LAME's encoder delay as leading padding: 577 samples at
 * 44.1 kHz for the Xing-less files written by assets/audio/generate_sfx.ps1.
 * Pre-echo from sharp transients can reach -12 dB of peak inside that padding, so a
 * level gate cannot find the real start; trim a fixed amount.
 */
enum {
    PDK_MP3_ENCODER_DELAY_SAMPLES = 577,
    PDK_DELAY_FADE_SAMPLES = 32
};

void AudioData_Init(AudioData *data)
{
    data->sampleRate = 0;
    data->samples = NULL;
    data->count = 0;
    data->capacity = 0;
}

void AudioData_Free(AudioData *data)
{
    free(data->samples);
    AudioData_Init(data);
}

bool AudioData_Append(AudioData *data, const float *samples, int count)
{
    if (count <= 0) {
        return true;
    }
    if (data->count + count > data->capacity) {
        int next = data->capacity > 0 ? data->capacity : 4096;
        float *grown;
        while (next < data->count + count) {
            next *= 2;
        }
        grown = (float *)realloc(data->samples, (size_t)next * sizeof(float));
        if (grown == NULL) {
            return false;
        }
        data->samples = grown;
        data->capacity = next;
    }
    memcpy(data->samples + data->count, samples, (size_t)count * sizeof(float));
    data->count += count;
    return true;
}

static bool ConfigurePcmOutput(IMFSourceReader *reader)
{
    IMFMediaType *mediaType = NULL;
    bool ok = false;

    if (FAILED(MFCreateMediaType(&mediaType))) {
        return false;
    }
    if (FAILED(PDK_CALL(mediaType, SetGUID, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio)) ||
        FAILED(PDK_CALL(mediaType, SetGUID, &MF_MT_SUBTYPE, &MFAudioFormat_Float)) ||
        FAILED(PDK_CALL(mediaType, SetUINT32, &MF_MT_AUDIO_NUM_CHANNELS, PDK_PCM_CHANNELS)) ||
        FAILED(PDK_CALL(mediaType, SetUINT32, &MF_MT_AUDIO_SAMPLES_PER_SECOND, PDK_PCM_SAMPLE_RATE)) ||
        FAILED(PDK_CALL(mediaType, SetUINT32, &MF_MT_AUDIO_BITS_PER_SAMPLE, PDK_PCM_BITS_PER_SAMPLE)) ||
        FAILED(PDK_CALL(mediaType, SetUINT32, &MF_MT_AUDIO_BLOCK_ALIGNMENT, PDK_PCM_BLOCK_ALIGN)) ||
        FAILED(PDK_CALL(mediaType, SetUINT32, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, PDK_PCM_AVG_BYTES)) ||
        FAILED(PDK_CALL(reader, SetCurrentMediaType, MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                        NULL, mediaType))) {
        goto cleanup;
    }
    ok = true;

cleanup:
    PDK_RELEASE_VALUE(mediaType);
    return ok;
}

static void AppendSamples(IMFSample *sample, AudioData *out)
{
    IMFMediaBuffer *buffer = NULL;
    BYTE *data = NULL;
    DWORD maxLength = 0;
    DWORD currentLength = 0;

    if (FAILED(PDK_CALL(sample, ConvertToContiguousBuffer, &buffer))) {
        return;
    }
    if (SUCCEEDED(PDK_CALL(buffer, Lock, &data, &maxLength, &currentLength)) && data != NULL) {
        PDK_UNUSED(maxLength);
        AudioData_Append(out, (const float *)data, (int)(currentLength / sizeof(float)));
        PDK_CALL0(buffer, Unlock);
    }
    PDK_RELEASE_VALUE(buffer);
}

static void TrimEncoderDelay(AudioData *data)
{
    const int trim = PDK_MP3_ENCODER_DELAY_SAMPLES - PDK_DELAY_FADE_SAMPLES;

    if (data->count <= PDK_MP3_ENCODER_DELAY_SAMPLES) {
        return;
    }
    memmove(data->samples, data->samples + trim,
            (size_t)(data->count - trim) * sizeof(float));
    data->count -= trim;
    for (int i = 0; i < PDK_DELAY_FADE_SAMPLES; ++i) {
        data->samples[i] *= (float)i / (float)PDK_DELAY_FADE_SAMPLES;
    }
}

bool DecodeMp3ToPcm(const uint8_t *bytes, int size, AudioData *out)
{
    IStream *stream = NULL;
    IMFByteStream *byteStream = NULL;
    IMFSourceReader *reader = NULL;
    bool ok = false;

    AudioData_Init(out);
    if (bytes == NULL || size <= 0) {
        return false;
    }

    stream = SHCreateMemStream(bytes, (UINT)size);
    if (stream == NULL) {
        return false;
    }
    if (FAILED(MFCreateMFByteStreamOnStream(stream, &byteStream))) {
        goto cleanup;
    }
    if (FAILED(MFCreateSourceReaderFromByteStream(byteStream, NULL, &reader))) {
        goto cleanup;
    }
    if (!ConfigurePcmOutput(reader)) {
        goto cleanup;
    }

    out->sampleRate = PDK_PCM_SAMPLE_RATE;

    for (;;) {
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        IMFSample *sample = NULL;
        const HRESULT hr = PDK_CALL(reader, ReadSample, MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0,
                                    &streamIndex, &flags, &timestamp, &sample);
        PDK_UNUSED(streamIndex);
        PDK_UNUSED(timestamp);
        if (FAILED(hr)) {
            goto cleanup;
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            PDK_RELEASE_VALUE(sample);
            break;
        }
        if (sample != NULL) {
            AppendSamples(sample, out);
            PDK_RELEASE_VALUE(sample);
        }
    }

    TrimEncoderDelay(out);
    ok = out->count > 0;

cleanup:
    PDK_RELEASE_VALUE(reader);
    PDK_RELEASE_VALUE(byteStream);
    PDK_RELEASE_VALUE(stream);
    if (!ok) {
        AudioData_Free(out);
    }
    return ok;
}
