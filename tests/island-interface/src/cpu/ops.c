/* The host half of the same boundary, and the demonstration that a root which
 * does not supply the shape may be organised however it likes.
 *
 * ONE FLAT FILE against a layout root organised by subject. Both entry points
 * are implemented here, and neither takes its namespace from this file's
 * directory: the shape comes from `src/kernels`, so `island_scale` stays in
 * `::image` even though nothing here is under a directory of that name. Moving
 * this file deeper changes no name a consumer wrote.
 *
 * The signatures here and in the layout root are the same declarations, and
 * that is a property nothing else in the toolchain checks: C language linkage
 * does not mangle, exactly one of these implementations is in any link, and two
 * that disagreed would each read the arguments its own way. `scan` is the one
 * place where both texts exist at once, and it refuses a disagreement there.
 *
 * No include here either. The generated boundary header arrives through the
 * compiler's forced-include flag, the same way it reaches the device half. */

MCPP_EXPORT_C
int island_saxpy(float a, const float* x, const float* y, float* out, unsigned n) {
    for (unsigned i = 0; i < n; ++i) out[i] = a * x[i] + y[i];
    return 0;
}

MCPP_EXPORT_C
int island_scale(float a, float* out, unsigned n) {
    for (unsigned i = 0; i < n; ++i) out[i] = a * out[i];
    return 0;
}

/* The host half has its own internals, and they must not reach the boundary
 * any more than the device half's do. */
static int host_only_helper(int x) { return x - 1; }
int internal_device(int x) { return host_only_helper(x); }
