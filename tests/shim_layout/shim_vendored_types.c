/*
 * Validates the vendored DirectWrite types on *both* toolchains.
 *
 * MSVC's C mode cannot parse <dwrite.h> at all, so src/graphics/dwrite_c.h
 * carries its own copies of the handful of DWRITE_* types the project uses.
 * Those copies are extracted from the MinGW-w64 header by
 * tools/gen_com_shim.py, and shim_layout_ref.c independently checks that the
 * literals below still match that header.  Together the two files pin the
 * vendored copy from both sides; a mismatch between the MinGW-w64 and the MSVC
 * SDK definitions would still be caught end to end by the five UI screenshot
 * comparisons (plan.md 1.9).
 *
 * Keep the literals in this file in sync with shim_layout_ref.c -- both compare
 * against the same numbers on purpose.
 */

#include <stddef.h>

#include "graphics/d2d_c.h"

#define PDK_STATIC_CHECK(expr, tag) _Static_assert(expr, #tag)

PDK_STATIC_CHECK(DWRITE_FACTORY_TYPE_SHARED == 0, DWRITE_FACTORY_TYPE_SHARED);
PDK_STATIC_CHECK(DWRITE_FACTORY_TYPE_ISOLATED == 1, DWRITE_FACTORY_TYPE_ISOLATED);
PDK_STATIC_CHECK(DWRITE_FONT_WEIGHT_NORMAL == 400, DWRITE_FONT_WEIGHT_NORMAL);
PDK_STATIC_CHECK(DWRITE_FONT_WEIGHT_SEMI_BOLD == 600, DWRITE_FONT_WEIGHT_SEMI_BOLD);
PDK_STATIC_CHECK(DWRITE_FONT_WEIGHT_BOLD == 700, DWRITE_FONT_WEIGHT_BOLD);
PDK_STATIC_CHECK(DWRITE_FONT_STYLE_NORMAL == 0, DWRITE_FONT_STYLE_NORMAL);
PDK_STATIC_CHECK(DWRITE_FONT_STRETCH_NORMAL == 5, DWRITE_FONT_STRETCH_NORMAL);
PDK_STATIC_CHECK(DWRITE_TEXT_ALIGNMENT_LEADING == 0, DWRITE_TEXT_ALIGNMENT_LEADING);
PDK_STATIC_CHECK(DWRITE_TEXT_ALIGNMENT_TRAILING == 1, DWRITE_TEXT_ALIGNMENT_TRAILING);
PDK_STATIC_CHECK(DWRITE_TEXT_ALIGNMENT_CENTER == 2, DWRITE_TEXT_ALIGNMENT_CENTER);
PDK_STATIC_CHECK(DWRITE_PARAGRAPH_ALIGNMENT_NEAR == 0, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
PDK_STATIC_CHECK(DWRITE_PARAGRAPH_ALIGNMENT_CENTER == 2, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
PDK_STATIC_CHECK(DWRITE_WORD_WRAPPING_WRAP == 0, DWRITE_WORD_WRAPPING_WRAP);
PDK_STATIC_CHECK(DWRITE_WORD_WRAPPING_NO_WRAP == 1, DWRITE_WORD_WRAPPING_NO_WRAP);
PDK_STATIC_CHECK(DWRITE_LINE_SPACING_METHOD_DEFAULT == 0, DWRITE_LINE_SPACING_METHOD_DEFAULT);
PDK_STATIC_CHECK(DWRITE_LINE_SPACING_METHOD_UNIFORM == 1, DWRITE_LINE_SPACING_METHOD_UNIFORM);
PDK_STATIC_CHECK(DWRITE_TRIMMING_GRANULARITY_NONE == 0, DWRITE_TRIMMING_GRANULARITY_NONE);
PDK_STATIC_CHECK(DWRITE_TRIMMING_GRANULARITY_CHARACTER == 1, DWRITE_TRIMMING_GRANULARITY_CHARACTER);

/* Struct layouts the UI code reads fields from.  The expected sizes are the
 * same on both toolchains because every field is a 32-bit scalar or a float. */
PDK_STATIC_CHECK(sizeof(DWRITE_TRIMMING) == 12, DWRITE_TRIMMING_size);
PDK_STATIC_CHECK(sizeof(DWRITE_TEXT_RANGE) == 8, DWRITE_TEXT_RANGE_size);
PDK_STATIC_CHECK(sizeof(DWRITE_TEXT_METRICS) == 36, DWRITE_TEXT_METRICS_size);
PDK_STATIC_CHECK(sizeof(DWRITE_HIT_TEST_METRICS) == 36, DWRITE_HIT_TEST_METRICS_size);
PDK_STATIC_CHECK(offsetof(DWRITE_TEXT_METRICS, width) == 8, DWRITE_TEXT_METRICS_width);
PDK_STATIC_CHECK(offsetof(DWRITE_TEXT_METRICS, height) == 16, DWRITE_TEXT_METRICS_height);

/* Every PDK vtable is a flat array of function pointers, so assert slot counts
 * (from plan.md appendix C) rather than byte sizes: pointers are 8 bytes on x64
 * and 4 bytes on x86. */
PDK_STATIC_CHECK(sizeof(PDK_ID2D1FactoryVtbl) == 17 * sizeof(void *), PDK_ID2D1FactoryVtbl_slots);
PDK_STATIC_CHECK(sizeof(PDK_ID2D1RenderTargetVtbl) == 57 * sizeof(void *), PDK_ID2D1RenderTargetVtbl_slots);
PDK_STATIC_CHECK(sizeof(PDK_ID2D1HwndRenderTargetVtbl) == 60 * sizeof(void *), PDK_ID2D1HwndRenderTargetVtbl_slots);
PDK_STATIC_CHECK(sizeof(PDK_ID2D1PathGeometryVtbl) == 21 * sizeof(void *), PDK_ID2D1PathGeometryVtbl_slots);
PDK_STATIC_CHECK(sizeof(PDK_ID2D1GeometrySinkVtbl) == 15 * sizeof(void *), PDK_ID2D1GeometrySinkVtbl_slots);
PDK_STATIC_CHECK(sizeof(PDK_ID2D1SolidColorBrushVtbl) == 10 * sizeof(void *), PDK_ID2D1SolidColorBrushVtbl_slots);
PDK_STATIC_CHECK(sizeof(PDK_IDWriteFactoryVtbl) == 24 * sizeof(void *), PDK_IDWriteFactoryVtbl_slots);
PDK_STATIC_CHECK(sizeof(PDK_IDWriteTextFormatVtbl) == 28 * sizeof(void *), PDK_IDWriteTextFormatVtbl_slots);
PDK_STATIC_CHECK(sizeof(PDK_IDWriteTextLayoutVtbl) == 67 * sizeof(void *), PDK_IDWriteTextLayoutVtbl_slots);
PDK_STATIC_CHECK(sizeof(PDK_IDWriteInlineObjectVtbl) == 7 * sizeof(void *), PDK_IDWriteInlineObjectVtbl_slots);
