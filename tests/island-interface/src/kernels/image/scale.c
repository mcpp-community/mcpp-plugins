/* A second island, one directory deeper.
 *
 * The directory is what puts this entry point in
 * `island_interface::kernels::image` while the file above it is in
 * `island_interface::kernels`. Nothing about the file name reaches the name:
 * an island holds zero, one or many marked entry points and each carries its
 * own.
 *
 * The signature deliberately wraps across lines: one that did not fit on a
 * single line is the shape a line-oriented scan gets wrong, and the generator
 * matches parentheses rather than reading lines. */

MCPP_EXPORT_C
int island_scale(float a,
                 float* out,
                 unsigned n) {
    for (unsigned i = 0; i < n; ++i) out[i] = a * out[i];
    return 0;
}
