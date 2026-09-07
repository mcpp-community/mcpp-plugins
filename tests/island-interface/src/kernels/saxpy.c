/* The island. It includes the GENERATED header, which is the only place the
   entry points' signatures exist. */
#include "island_interface.kernels.h"

int saxpy_device(float a, const float* x, const float* y, float* out, unsigned n) {
    for (unsigned i = 0; i < n; ++i) out[i] = a * x[i] + y[i];
    return 0;
}

int scale_device(float a, float* out, unsigned n) {
    for (unsigned i = 0; i < n; ++i) out[i] = a * out[i];
    return 0;
}
