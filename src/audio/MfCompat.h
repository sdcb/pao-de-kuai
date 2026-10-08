#pragma once

/*
 * Media Foundation header compatibility.
 *
 * Include order matters and is toolchain-verified (see plan.md 7):
 *   <mfapi.h> and <mfidl.h> must come before <mfreadwrite.h>.
 *
 * MinGW-w64's headers declare MFCreateMFByteStreamOnStreamEx but omit
 * MFCreateMFByteStreamOnStream, even though libmfplat.a exports it and the
 * Windows SDK declares it in <mfreadwrite.h>.  Redeclaring a function is legal
 * in C, so the prototype is emitted unconditionally and simply agrees with the
 * SDK on MSVC.  When MinGW fixes its header this block can be deleted.
 */

#include "graphics/win_compat.h"

#include <mfobjects.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#ifdef __cplusplus
extern "C" {
#endif

/* mfreadwrite.h: STDAPI MFCreateMFByteStreamOnStream(IStream*, IMFByteStream**); */
STDAPI MFCreateMFByteStreamOnStream(IStream *pStream, IMFByteStream **ppByteStream);

#ifdef __cplusplus
}
#endif
