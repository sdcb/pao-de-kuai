/*
 * Decodes every embedded sound effect and checks the invariants the audio engine
 * relies on.  This is the automated half of plan.md S6: the WASAPI path itself can
 * only be confirmed by listening, but everything up to the mixer -- resource
 * lookup, mp3 -> mono float PCM, the 44.1 kHz contract and the encoder-delay trim
 * -- is deterministic and is checked here.
 *
 * No audio device is needed: it never creates an engine, only Media Foundation.
 */

#include "audio/AudioDecoder.h"
#include "audio/SoundCatalog.h"
#include "audio/SoundIds.h"
#include "resources/ResourceLoader.h"

#include <mfapi.h>

#include <stdio.h>

/* Mirrors PDK_MP3_ENCODER_DELAY_SAMPLES in AudioDecoder.c: a stream shorter than
 * this is left untouched, so the "first sample is faded to zero" check below only
 * applies above it. */
enum { ENCODER_DELAY_SAMPLES = 577 };

int main(void)
{
    int count = 0;
    int failures = 0;
    const SoundCatalogEntry *catalog;

    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) {
        printf("FAIL: MFStartup\n");
        return 1;
    }

    catalog = SoundCatalog_Entries(&count);
    if (count != SOUND_COUNT) {
        printf("FAIL: catalog has %d entries, expected %d\n", count, SOUND_COUNT);
        ++failures;
    }

    for (int i = 0; i < count; ++i) {
        ByteBuffer bytes;
        AudioData data;
        bool ok = true;

        ByteBuffer_Init(&bytes);
        AudioData_Init(&data);

        if (catalog[i].id != (SoundId)i) {
            printf("FAIL: catalog[%d].id = %d, expected the table to be indexed by SoundId\n",
                   i, (int)catalog[i].id);
            ++failures;
            ok = false;
        }
        if (!ok) {
            continue;
        }
        if (!LoadResourceBytes(catalog[i].resourceId, NULL, NULL, &bytes)) {
            printf("FAIL: %-24s resource %d not embedded\n", catalog[i].fileName,
                   catalog[i].resourceId);
            ++failures;
            continue;
        }
        if (!DecodeMp3ToPcm(bytes.data, bytes.size, &data)) {
            printf("FAIL: %-24s decode failed (%d bytes)\n", catalog[i].fileName, bytes.size);
            ++failures;
            ByteBuffer_Free(&bytes);
            continue;
        }
        if (data.sampleRate != 44100u) {
            printf("FAIL: %-24s sampleRate = %u, expected 44100\n", catalog[i].fileName,
                   (unsigned)data.sampleRate);
            ++failures;
        }
        if (data.count <= 0) {
            printf("FAIL: %-24s produced no samples\n", catalog[i].fileName);
            ++failures;
        } else if (data.count > ENCODER_DELAY_SAMPLES && data.samples[0] != 0.0f) {
            /* TrimEncoderDelay scales the first of the 32 fade samples by 0/32, so a
             * stream long enough to be trimmed must start with an exact zero.  This is
             * the cheapest available signature that the MP3 encoder delay was removed. */
            printf("FAIL: %-24s first sample = %.6f, expected the delay-trim fade\n",
                   catalog[i].fileName, data.samples[0]);
            ++failures;
        } else {
            printf("ok   %-24s rate=%u samples=%d\n", catalog[i].fileName,
                   (unsigned)data.sampleRate, data.count);
        }

        AudioData_Free(&data);
        ByteBuffer_Free(&bytes);
    }

    MFShutdown();

    if (failures != 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all %d embedded sounds decoded\n", count);
    return 0;
}
