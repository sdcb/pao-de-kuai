#include "core/Str.h"

#include <stdlib.h>
#include <string.h>

#include <windows.h>

/*
 * Implementation notes
 *  - Growth doubles from a small initial capacity; every wait-free path stays
 *    allocation-free because Str_Reserve returns false instead of aborting.
 *  - All functions tolerate a zero-initialised struct ({0}), which is how the
 *    rest of the code declares its strings, so there is no "must call Init"
 *    trap: Str_Init only exists to be explicit.
 *  - On allocation failure the string keeps whatever it already held; callers
 *    that must not lose data check the bool return values.
 */

#define PDK_STR_MIN_CAP 32

static bool GrowBytes(void **data, int *cap, int len, int extra, size_t elemSize)
{
    if (extra < 0) {
        return false;
    }
    const int needed = len + extra + 1;
    if (needed <= *cap) {
        return true;
    }
    int next = *cap > 0 ? *cap : PDK_STR_MIN_CAP;
    while (next < needed) {
        if (next > (1 << 28)) {
            next = needed;
            break;
        }
        next *= 2;
    }
    void *grown = realloc(*data, (size_t)next * elemSize);
    if (grown == NULL) {
        return false;
    }
    *data = grown;
    *cap = next;
    return true;
}

/* ---- byte strings ---------------------------------------------------- */

void Str_Init(Str *s)
{
    s->data = NULL;
    s->len = 0;
    s->cap = 0;
}

void Str_Free(Str *s)
{
    free(s->data);
    Str_Init(s);
}

void Str_Clear(Str *s)
{
    s->len = 0;
    if (s->data != NULL) {
        s->data[0] = '\0';
    }
}

bool Str_Reserve(Str *s, int extra)
{
    if (!GrowBytes((void **)&s->data, &s->cap, s->len, extra, sizeof(char))) {
        return false;
    }
    if (s->len == 0 && s->data != NULL) {
        s->data[0] = '\0';
    }
    return true;
}

void Str_AppendN(Str *s, const char *text, int count)
{
    if (text == NULL || count <= 0) {
        return;
    }
    if (!Str_Reserve(s, count)) {
        return;
    }
    memcpy(s->data + s->len, text, (size_t)count);
    s->len += count;
    s->data[s->len] = '\0';
}

void Str_Append(Str *s, const char *text)
{
    if (text == NULL) {
        return;
    }
    Str_AppendN(s, text, (int)strlen(text));
}

void Str_AppendChar(Str *s, char c)
{
    Str_AppendN(s, &c, 1);
}

void Str_AssignN(Str *s, const char *text, int count)
{
    Str_Clear(s);
    Str_AppendN(s, text, count);
}

void Str_Assign(Str *s, const char *text)
{
    Str_Clear(s);
    Str_Append(s, text);
}

void Str_AppendUnsigned(Str *s, unsigned long long value)
{
    char digits[24];
    int count = 0;
    do {
        digits[count++] = (char)('0' + (int)(value % 10u));
        value /= 10u;
    } while (value != 0 && count < (int)sizeof(digits));
    while (count > 0) {
        Str_AppendChar(s, digits[--count]);
    }
}

void Str_AppendNumber(Str *s, long long value)
{
    if (value < 0) {
        Str_AppendChar(s, '-');
        /* Negating LLONG_MIN overflows; do the magnitude in unsigned space. */
        Str_AppendUnsigned(s, (unsigned long long)0 - (unsigned long long)value);
        return;
    }
    Str_AppendUnsigned(s, (unsigned long long)value);
}

void Str_AppendPaddedNumber(Str *s, int value, int width)
{
    char digits[24];
    int count = 0;
    const bool negative = value < 0;
    unsigned int magnitude = negative ? (unsigned int)0 - (unsigned int)value
                                      : (unsigned int)value;
    do {
        digits[count++] = (char)('0' + (int)(magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude != 0 && count < (int)sizeof(digits));

    if (negative) {
        Str_AppendChar(s, '-');
    }
    for (int pad = width - count; pad > 0; --pad) {
        Str_AppendChar(s, '0');
    }
    while (count > 0) {
        Str_AppendChar(s, digits[--count]);
    }
}

bool Str_Equals(const Str *s, const char *text)
{
    if (text == NULL) {
        return s->len == 0;
    }
    const size_t other = strlen(text);
    return other == (size_t)s->len &&
           (other == 0 || memcmp(Str_CStr(s), text, other) == 0);
}

const char *Str_CStr(const Str *s)
{
    return s->data != NULL ? s->data : "";
}

/* ---- wide strings ---------------------------------------------------- */

void WStr_Init(WStr *s)
{
    s->data = NULL;
    s->len = 0;
    s->cap = 0;
}

void WStr_Free(WStr *s)
{
    free(s->data);
    WStr_Init(s);
}

void WStr_Clear(WStr *s)
{
    s->len = 0;
    if (s->data != NULL) {
        s->data[0] = L'\0';
    }
}

bool WStr_Reserve(WStr *s, int extra)
{
    if (!GrowBytes((void **)&s->data, &s->cap, s->len, extra, sizeof(wchar_t))) {
        return false;
    }
    if (s->len == 0 && s->data != NULL) {
        s->data[0] = L'\0';
    }
    return true;
}

void WStr_AppendN(WStr *s, const wchar_t *text, int count)
{
    if (text == NULL || count <= 0) {
        return;
    }
    if (!WStr_Reserve(s, count)) {
        return;
    }
    memcpy(s->data + s->len, text, (size_t)count * sizeof(wchar_t));
    s->len += count;
    s->data[s->len] = L'\0';
}

void WStr_Append(WStr *s, const wchar_t *text)
{
    if (text == NULL) {
        return;
    }
    WStr_AppendN(s, text, (int)wcslen(text));
}

void WStr_AppendChar(WStr *s, wchar_t c)
{
    WStr_AppendN(s, &c, 1);
}

void WStr_AssignN(WStr *s, const wchar_t *text, int count)
{
    WStr_Clear(s);
    WStr_AppendN(s, text, count);
}

void WStr_Assign(WStr *s, const wchar_t *text)
{
    WStr_Clear(s);
    WStr_Append(s, text);
}

bool WStr_Equals(const WStr *s, const wchar_t *text)
{
    if (text == NULL) {
        return s->len == 0;
    }
    const size_t other = wcslen(text);
    return other == (size_t)s->len &&
           (other == 0 || memcmp(WStr_CStr(s), text, other * sizeof(wchar_t)) == 0);
}

const wchar_t *WStr_CStr(const WStr *s)
{
    return s->data != NULL ? s->data : L"";
}

/* ---- conversions ----------------------------------------------------- */

wchar_t *PdkUtf8ToWide(const char *text)
{
    if (text == NULL) {
        return NULL;
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (needed <= 0) {
        return NULL;
    }
    wchar_t *out = (wchar_t *)malloc((size_t)needed * sizeof(wchar_t));
    if (out == NULL) {
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, out, needed) <= 0) {
        free(out);
        return NULL;
    }
    return out;
}

char *PdkWideToUtf8(const wchar_t *text)
{
    if (text == NULL) {
        return NULL;
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (needed <= 0) {
        return NULL;
    }
    char *out = (char *)malloc((size_t)needed);
    if (out == NULL) {
        return NULL;
    }
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, out, needed, NULL, NULL) <= 0) {
        free(out);
        return NULL;
    }
    return out;
}
