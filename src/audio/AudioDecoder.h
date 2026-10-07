#pragma once

/*
 * Media Foundation mp3 -> float PCM decoding.
 *
 * Pure C.  AudioData owns a growable float buffer (Init/Free/Append) instead of a
 * std::vector, and the input is a plain byte span instead of std::span.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mono float samples at the source sample rate. */
typedef struct AudioData {
    uint32_t sampleRate;
    float *samples;
    int count;
    int capacity;
} AudioData;

void AudioData_Init(AudioData *data);
void AudioData_Free(AudioData *data);
bool AudioData_Append(AudioData *data, const float *samples, int count);

/* Decodes a whole mp3 held in memory into `out`, which is left empty on failure.
 * `out` must be initialised (AudioData_Init / zeroed). */
bool DecodeMp3ToPcm(const uint8_t *bytes, int size, AudioData *out);

#ifdef __cplusplus
}
#endif
