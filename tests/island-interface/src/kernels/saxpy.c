/* The island.
 *
 * The signatures live HERE, beside the definitions, and exist once. The marker
 * is what the generator finds them by; the generated header defines it as
 * nothing, so this file compiles with no other arrangement.
 *
 * `scale_device` deliberately wraps across lines: a signature that did not fit
 * on one is the shape a line-oriented scan gets wrong, and the generator
 * matches parentheses rather than reading lines. */
#include "island_interface.kernels.h"

MCPP_EXPORT_C
int saxpy_device(float a, const float* x, const float* y, float* out, unsigned n) {
    for (unsigned i = 0; i < n; ++i) out[i] = a * x[i] + y[i];
    return 0;
}

MCPP_EXPORT_C
int scale_device(float a,
                 float* out,
                 unsigned n) {
    for (unsigned i = 0; i < n; ++i) out[i] = a * out[i];
    return 0;
}

/* Not marked, so it must not appear in the header or the module: an island has
 * internal functions, and exporting them all would make the boundary whatever
 * the file happened to contain. */
static int unexported_helper(int x) { return x + 1; }
int internal_device(int x) { return unexported_helper(x); }
