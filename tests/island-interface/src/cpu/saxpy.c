/* The host half of the same boundary.
 *
 * The signatures here and in `../kernels/saxpy.c` are the same declarations,
 * and that is a property nothing else in the toolchain checks: C language
 * linkage does not mangle, exactly one of these files is in any link, and two
 * that disagreed would each read the arguments its own way.
 *
 * `mcpp::tools::island::scan` is handed both, so it is the one place where both
 * texts exist at once -- and it refuses a disagreement there, with both file
 * names, instead of leaving it to the run.
 *
 * No include here either. The generated boundary header arrives through the
 * compiler's forced-include flag, the same way it reaches the device half. */

MCPP_EXPORT_C
int saxpy_device(float a, const float* x, const float* y, float* out, unsigned n) {
    for (unsigned i = 0; i < n; ++i) out[i] = a * x[i] + y[i];
    return 0;
}

MCPP_EXPORT_C
int scale_device(float a, float* out, unsigned n) {
    for (unsigned i = 0; i < n; ++i) out[i] = a * out[i];
    return 0;
}

/* The host half has its own internals, and they must not reach the boundary
 * any more than the device half's do. */
static int host_only_helper(int x) { return x - 1; }
int internal_device(int x) { return host_only_helper(x); }
