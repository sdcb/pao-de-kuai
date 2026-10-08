#pragma once

#include "audio/SoundIds.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SoundCatalogEntry {
    SoundId id;
    int resourceId;
    /* File name of the source mp3, kept for diagnostics; a string literal. */
    const char *fileName;
    float recommendedVolume;
} SoundCatalogEntry;

/* Returns a pointer to a file-scope table and writes its length to `count`
 * (which may be NULL).  The table is SOUND_COUNT entries, indexed by SoundId. */
const SoundCatalogEntry *SoundCatalog_Entries(int *count);

#ifdef __cplusplus
}
#endif
