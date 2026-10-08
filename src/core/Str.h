#pragma once

/*
 * Growable string types for the pure-C sources.
 *
 * Why not <string.h> + malloc by hand everywhere: every module that previously
 * used std::string (UI text, log file names, JSON, window titles) needs append
 * and number formatting, and the project forbids <sstream>/fmt to keep the
 * linked image small (see AGENTS.md "体积约束").  These two types are the only
 * heap-backed string abstraction in src/.
 *
 * Both own their buffer, are NUL terminated at all times, and must be freed
 * with Str_Free / WStr_Free.  There is no RAII in C, so every function that
 * allocates documents its cleanup point.
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- UTF-8 / byte strings -------------------------------------------- */

typedef struct Str {
    char *data; /* NUL terminated; NULL until first append */
    int len;    /* bytes excluding the terminator */
    int cap;    /* allocated bytes including the terminator slot */
} Str;

void Str_Init(Str *s);
void Str_Free(Str *s);
void Str_Clear(Str *s);
/* Grows the buffer so that `extra` more bytes fit (excluding the terminator). */
bool Str_Reserve(Str *s, int extra);
void Str_AppendN(Str *s, const char *text, int count);
void Str_Append(Str *s, const char *text);
void Str_AppendChar(Str *s, char c);
void Str_Assign(Str *s, const char *text);
void Str_AssignN(Str *s, const char *text, int count);
/* Number formatting without <stdio.h> stream formatting. */
void Str_AppendNumber(Str *s, long long value);
void Str_AppendUnsigned(Str *s, unsigned long long value);
void Str_AppendPaddedNumber(Str *s, int value, int width);
bool Str_Equals(const Str *s, const char *text);
/* Never returns NULL, so callers can pass it straight to Win32 as a C string. */
const char *Str_CStr(const Str *s);

/* Copies `src` into a fixed buffer, truncating and always NUL terminating.  NULL src
 * clears the buffer.  This is the one sanctioned way to fill a char[] field; every
 * fixed-buffer struct in the project uses it instead of strncpy. */
void Str_CopyTo(char *dst, int cap, const char *src);

/* ---- wide strings ---------------------------------------------------- */

typedef struct WStr {
    wchar_t *data;
    int len; /* wchar_t units excluding the terminator */
    int cap;
} WStr;

void WStr_Init(WStr *s);
void WStr_Free(WStr *s);
void WStr_Clear(WStr *s);
bool WStr_Reserve(WStr *s, int extra);
void WStr_AppendN(WStr *s, const wchar_t *text, int count);
void WStr_Append(WStr *s, const wchar_t *text);
void WStr_AppendChar(WStr *s, wchar_t c);
void WStr_Assign(WStr *s, const wchar_t *text);
void WStr_AssignN(WStr *s, const wchar_t *text, int count);
bool WStr_Equals(const WStr *s, const wchar_t *text);
const wchar_t *WStr_CStr(const WStr *s);

/* ---- string lists ---------------------------------------------------- */
/*
 * Replaces std::vector<std::string> for the places that need an unbounded
 * number of names (the stat/ directory listing, for example).  Each entry owns
 * its buffer; StrList_Free releases the list and every entry.
 */
typedef struct StrList {
    char **items;
    int count;
    int cap;
} StrList;

void StrList_Init(StrList *list);
void StrList_Free(StrList *list);
void StrList_Clear(StrList *list);
/* Copies the first `len` bytes of `text` (or the whole string when len < 0). */
bool StrList_Push(StrList *list, const char *text, int len);
/* Never returns NULL for a valid index; returns "" out of range. */
const char *StrList_At(const StrList *list, int index);

/* ---- conversions ----------------------------------------------------- */
/*
 * Both return a heap buffer owned by the caller (free with free()), or NULL on
 * failure.  They are the C replacements for the old std::wstring helpers.
 */
wchar_t *PdkUtf8ToWide(const char *text);
char *PdkWideToUtf8(const wchar_t *text);

#ifdef __cplusplus
}
#endif
