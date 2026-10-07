/*
 * MinGW-only ABI gate for the COM shim (plan.md 3.3).
 *
 * MinGW-w64's C mode declares Direct2D and DirectWrite vtables, MSVC's does
 * not, so MinGW is the machine-checkable reference for the flat PDK_* vtables
 * that both toolchains call through.  This translation unit puts the real SDK
 * types and the generated PDK_* types side by side and asserts that they have
 * the same size and that every method declared in an interface's own block sits
 * at the same offset.
 *
 * `sizeof` equality is the strong assertion: the left side is built by the
 * compiler from <d2d1.h>/<dwrite.h> while the right side comes from
 * tools/gen_com_shim.py's parse of those same headers, so a single missed,
 * reordered or invented method fails the build.  During the original
 * investigation this mechanism caught two real errors -- ID2D1Factory was
 * assumed to derive from ID2D1Resource (it derives from IUnknown) and
 * IDWriteFactory was missing its eight leading methods.
 *
 * PDK_SKIP_DWRITE_TYPES is required because dwrite_c.h normally supplies its own
 * copies of the DWRITE_* names, which would collide with <dwrite.h>.
 */

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>

#define PDK_SKIP_DWRITE_TYPES 1
#include "graphics/d2d_c.h"

#include "ShimLayoutChecks.h"

/* The real <dwrite.h> must agree with the literal values the generator copied
 * into the vendored DirectWrite types.  A failure here means the MinGW headers
 * moved under us: regenerate with tools/gen_com_shim.py. */
_Static_assert(DWRITE_FACTORY_TYPE_SHARED == 0, "DWRITE_FACTORY_TYPE_SHARED");
_Static_assert(DWRITE_FACTORY_TYPE_ISOLATED == 1, "DWRITE_FACTORY_TYPE_ISOLATED");
_Static_assert(DWRITE_FONT_WEIGHT_NORMAL == 400, "DWRITE_FONT_WEIGHT_NORMAL");
_Static_assert(DWRITE_FONT_WEIGHT_SEMI_BOLD == 600, "DWRITE_FONT_WEIGHT_SEMI_BOLD");
_Static_assert(DWRITE_FONT_WEIGHT_BOLD == 700, "DWRITE_FONT_WEIGHT_BOLD");
_Static_assert(DWRITE_FONT_STYLE_NORMAL == 0, "DWRITE_FONT_STYLE_NORMAL");
_Static_assert(DWRITE_FONT_STRETCH_NORMAL == 5, "DWRITE_FONT_STRETCH_NORMAL");
_Static_assert(DWRITE_TEXT_ALIGNMENT_LEADING == 0, "DWRITE_TEXT_ALIGNMENT_LEADING");
_Static_assert(DWRITE_TEXT_ALIGNMENT_TRAILING == 1, "DWRITE_TEXT_ALIGNMENT_TRAILING");
_Static_assert(DWRITE_TEXT_ALIGNMENT_CENTER == 2, "DWRITE_TEXT_ALIGNMENT_CENTER");
_Static_assert(DWRITE_PARAGRAPH_ALIGNMENT_NEAR == 0, "DWRITE_PARAGRAPH_ALIGNMENT_NEAR");
_Static_assert(DWRITE_PARAGRAPH_ALIGNMENT_CENTER == 2, "DWRITE_PARAGRAPH_ALIGNMENT_CENTER");
_Static_assert(DWRITE_WORD_WRAPPING_WRAP == 0, "DWRITE_WORD_WRAPPING_WRAP");
_Static_assert(DWRITE_WORD_WRAPPING_NO_WRAP == 1, "DWRITE_WORD_WRAPPING_NO_WRAP");
_Static_assert(DWRITE_LINE_SPACING_METHOD_DEFAULT == 0, "DWRITE_LINE_SPACING_METHOD_DEFAULT");
_Static_assert(DWRITE_LINE_SPACING_METHOD_UNIFORM == 1, "DWRITE_LINE_SPACING_METHOD_UNIFORM");
_Static_assert(DWRITE_TRIMMING_GRANULARITY_NONE == 0, "DWRITE_TRIMMING_GRANULARITY_NONE");
_Static_assert(DWRITE_TRIMMING_GRANULARITY_CHARACTER == 1, "DWRITE_TRIMMING_GRANULARITY_CHARACTER");
