#include "resources/ResourceLoader.h"

#include <stdlib.h>
#include <string.h>

void ByteBuffer_Init(ByteBuffer *buffer)
{
    buffer->data = NULL;
    buffer->size = 0;
}

void ByteBuffer_Free(ByteBuffer *buffer)
{
    free(buffer->data);
    ByteBuffer_Init(buffer);
}

bool LoadResourceBytes(int resourceId, LPCWSTR type, HMODULE module, ByteBuffer *out)
{
    HRSRC resource;
    HGLOBAL loaded;
    DWORD size;
    const void *data;

    ByteBuffer_Init(out);
    if (module == NULL) {
        module = GetModuleHandleW(NULL);
    }
    resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId),
                             type != NULL ? type : RT_RCDATA);
    if (resource == NULL) {
        return false;
    }
    loaded = LoadResource(module, resource);
    if (loaded == NULL) {
        return false;
    }
    size = SizeofResource(module, resource);
    data = LockResource(loaded);
    if (data == NULL || size == 0) {
        return false;
    }
    out->data = (uint8_t *)malloc((size_t)size);
    if (out->data == NULL) {
        return false;
    }
    memcpy(out->data, data, (size_t)size);
    out->size = (int)size;
    return true;
}
