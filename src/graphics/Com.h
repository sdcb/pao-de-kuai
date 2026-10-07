#pragma once

/*
 * COM lifetime and vtable-call helpers, plus the PDK replacement for
 * IUnknown.  There is no ComPtr<T> template any more: C has no templates and
 * no RAII, so owners call PDK_RELEASE explicitly and every allocation has a
 * matching release point (see plan.md 2 and 7).
 *
 * The PDK_* interface vtables in d2d_c.h / dwrite_c.h are *flat*: every slot of
 * the whole inheritance chain appears in order.  MinGW's C-mode headers nest
 * their `Base` members and the MSVC C++ ABI flattens them, but `Base` is the
 * first member, so both layouts are byte-identical -- that is what lets a
 * single generated header serve both compilers (plan.md 3.1).
 */

#include "graphics/win_compat.h"

typedef struct PDK_IUnknown PDK_IUnknown;
typedef struct PDK_IUnknownVtbl PDK_IUnknownVtbl;

struct PDK_IUnknown {
    const PDK_IUnknownVtbl *lpVtbl;
};

struct PDK_IUnknownVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(PDK_IUnknown *This,
                                                REFIID riid,
                                                void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(PDK_IUnknown *This);
    ULONG (STDMETHODCALLTYPE *Release)(PDK_IUnknown *This);
};

/* Cast a real SDK interface pointer to its PDK mirror.  The cast is a no-op at
 * the ABI level; it only exists so the compiler can type-check the call. */
#define PDK_AS(type, p) ((PDK_##type *)(p))
#define PDK_AS_CONST(type, p) ((const PDK_##type *)(p))

/* At least one argument after the method name. */
#define PDK_CALL(p, method, ...) ((p)->lpVtbl->method((p), __VA_ARGS__))

/* Zero arguments after the method name.  `, ##__VA_ARGS__` is a GCC extension
 * and an empty `(...)` argument list is only valid in C23, so the two arities
 * get two macros instead of one clever one. */
#define PDK_CALL0(p, method) ((p)->lpVtbl->method((p)))

#define PDK_ADDREF(p) (PDK_CALL0(PDK_AS(IUnknown, (p)), AddRef))

#define PDK_QUERY(p, riid, out) \
    (PDK_CALL(PDK_AS(IUnknown, (p)), QueryInterface, (riid), (void **)(out)))

/* Releases an lvalue pointer and clears it; safe on NULL. */
#define PDK_RELEASE(p)                                  \
    do {                                                \
        if ((p) != NULL) {                              \
            PDK_CALL0(PDK_AS(IUnknown, (p)), Release);  \
            (p) = NULL;                                 \
        }                                               \
    } while (0)

/* Releases without touching the variable (for `if (x) Release(x);` cleanup
 * paths where the pointer is not an lvalue). */
#define PDK_RELEASE_VALUE(p) \
    do {                     \
        if ((p) != NULL) {   \
            PDK_CALL0(PDK_AS(IUnknown, (p)), Release); \
        }                    \
    } while (0)
