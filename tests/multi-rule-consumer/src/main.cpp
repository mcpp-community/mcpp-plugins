// The two backends' products, in one program.
//
// The SPIR-V module arrives as a header the shader rule generated; the CUDA
// kernel arrives as an object the CUDA rule compiled and the engine linked.
// Asserting on both in one binary is the point of this fixture: each rule had
// to find its own inputs in a device source list that held the other's too.
//
// The two are guarded differently because their seams differ. The CUDA island
// has a CPU implementation behind the same `extern "C"` symbol, so `saxpy_device`
// is always callable and the manifest decides which definition is linked. The
// shader has no such fallback: its product is a generated HEADER, and a build
// that names no accelerator does not generate one -- so the reference must be
// compiled out, which is what the define from the manifest is for.
#include <cstdio>
#include "saxpy/saxpy.h"

#ifdef HAVE_SPIRV
#include <cstdint>
#include "scale_comp.h"
#endif

int main() {
#ifdef HAVE_SPIRV
    const std::uint32_t magic = scale_comp_spv[0];
    std::printf("magic=%08x words=%zu\n",
                magic, sizeof scale_comp_spv / sizeof scale_comp_spv[0]);
    if (magic != 0x07230203u) return 1;
#else
    std::printf("magic=(no shader in this build)\n");
#endif

    float x[4] = {1, 2, 3, 4}, y[4] = {10, 20, 30, 40}, out[4] = {};
    if (saxpy_device(2.0f, x, y, out, 4) != 0) return 1;
    std::printf("%g %g %g %g\n", out[0], out[1], out[2], out[3]);
    return 0;
}
