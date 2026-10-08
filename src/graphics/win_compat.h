#pragma once

/*
 * The single place in the project where a toolchain difference is allowed to
 * exist.  Everything here is deliberately shared by MSVC and MinGW-w64: if a
 * file outside this header ever needs to know which compiler is running, the
 * fix belongs here instead (see plan.md 3.4).
 *
 * Rules for adding anything below:
 *   1. Say *why* it is needed and *when* it can be deleted.
 *   2. Keep the MSVC and MinGW paths semantically identical.
 *   3. Never put a vtable definition here -- those live in d2d_c.h /
 *      dwrite_c.h, which are generated from the MinGW C-mode headers.
 *
 * Include order requirements for SDK headers (verified on both toolchains):
 *   - <imm.h>         must come after <windows.h>
 *   - <mfapi.h> and <mfidl.h> must come before <mfreadwrite.h>
 *   - a TU that wants GUID *definitions* must `#define INITGUID` before the
 *     first SDK header; that is only src/graphics/iids.c
 */

#include <windows.h>

#include <objbase.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(_MSC_VER)
#define PDK_COMPILER_MSVC 1
#elif defined(__MINGW32__)
#define PDK_COMPILER_MINGW 1
#else
#error "PaoDeKuai is built with MSVC or MinGW-w64 only."
#endif

/* --- small language helpers ------------------------------------------- */

#ifndef PDK_ARRAY_COUNT
#define PDK_ARRAY_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#endif

#ifndef PDK_UNUSED
#define PDK_UNUSED(x) ((void)(x))
#endif

#ifndef PDK_MIN
#define PDK_MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef PDK_MAX
#define PDK_MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef PDK_CLAMP
#define PDK_CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#endif

/* C has no `nullptr`; keep the code readable without pulling in <stddef.h>
 * everywhere. */
#ifndef PDK_NULL
#define PDK_NULL ((void *)0)
#endif

/* --- shlwapi ---------------------------------------------------------- */
/*
 * Neither toolchain includes <shlwapi.h>:
 *   - MinGW C mode fails to parse it ("unknown type name 'IQueryAssociations'"
 *     at shlwapi.h:981), which makes it unusable for a C translation unit.
 *   - MSVC can parse it, but using it on one side only would be a divergence.
 * Only the entry points below are used, so declare them by hand.  When adding
 * one, copy the exact prototype from the SDK header.
 */
#ifdef __cplusplus
extern "C" {
#endif

/* shlwapi.h: SHCreateMemStream (used by src/audio/AudioDecoder.c). */
IStream *WINAPI SHCreateMemStream(const BYTE *pInit, UINT cbInit);

#ifdef __cplusplus
}
#endif

/* COM vtable plumbing lives in graphics/Com.h, which includes this header. */
