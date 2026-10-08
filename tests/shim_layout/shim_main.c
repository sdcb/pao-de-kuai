/*
 * Shared entry point for the shim_layout check executable.
 *
 * Every check in this target is a compile-time _Static_assert; the program
 * exists only so the checks become a ctest target that a normal build actually
 * compiles.  See shim_layout_ref.c (MinGW only, compares against the real SDK
 * headers) and shim_vendored_types.c (both toolchains, validates the vendored
 * DirectWrite types).
 */

int main(void)
{
    return 0;
}
