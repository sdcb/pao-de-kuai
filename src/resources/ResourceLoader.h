#pragma once

/*
 * Reads an embedded .rc resource into a heap buffer.
 *
 * Pure C.  The previous API returned std::vector<std::uint8_t>; ByteBuffer owns a
 * malloc'd copy instead, so callers must ByteBuffer_Free it.  Everything the
 * project embeds (the card atlas PNG and each mp3) is loaded once at startup, so
 * the copy is not on any hot path.
 */

#include <stdbool.h>
#include <stdint.h>

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ByteBuffer {
    uint8_t *data;
    int size;
} ByteBuffer;

void ByteBuffer_Init(ByteBuffer *buffer);
void ByteBuffer_Free(ByteBuffer *buffer);

/* `type` may be NULL for RT_RCDATA and `module` NULL for the current module.
 * Returns false (and leaves `out` empty) when the resource is missing. */
bool LoadResourceBytes(int resourceId, LPCWSTR type, HMODULE module, ByteBuffer *out);

#ifdef __cplusplus
}
#endif
