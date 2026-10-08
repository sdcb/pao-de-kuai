/*
 * The one translation unit that owns GUID *definitions*.
 *
 * Both toolchains need the same treatment here: MSVC's C mode only declares
 * IID_ID2D1Factory (it lives in an import library we refuse to depend on), and
 * <dwrite.h> cannot be parsed by MSVC's C mode at all, so the DirectWrite IIDs
 * come from our generated header.  `#define INITGUID` before the first SDK
 * header turns every DEFINE_GUID into a definition, which is why this file must
 * not include graphics/d2d_c.h or graphics/dwrite_c.h (they would redeclare the
 * same symbols through the real headers).
 *
 * Verified on MinGW in plan.md revision 4: with INITGUID and no -luuid, every
 * symbol resolves; -luuid alone leaves CLSID_MMDeviceEnumerator,
 * IID_IMMDeviceEnumerator and IID_IDWriteFactory undefined.
 */

#define INITGUID 1

#include <windows.h>

/*
 * <mmdeviceapi.h>, <audioclient.h> and <wincodec.h> are deliberately NOT included here: MinGW's
 * copies use DEFINE_GUID (so INITGUID would define the WASAPI/MMDevice GUIDs) while
 * MSVC's only declare them, and including them on one toolchain only would put the
 * two builds in different places.  graphics/iids_gen.h defines all six instead, so
 * both toolchains get exactly one definition.
 */

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include "graphics/win_compat.h"
#include "graphics/iids_gen.h"
