// THE GENERATED HEADER IS THE FIRST INCLUDE, AND THAT ORDER IS THE ASSERTION.
//
// It declares `const uint32_t scale_comp_spv[]` -- the name is the shader's
// stem, its stage, and `_spv`, as mcpp.rules.spirv documents -- and a header
// that names a type has to bring it. Putting `<cstdint>` above this line would
// satisfy the compiler on behalf of the header and hide whether the header can
// stand on its own; every consumer that had worked did exactly that, with a
// Vulkan header in front, and a sandbox found the state that leaves:
//
//   tri_vert.h:3:7: error: 'uint32_t' does not name a type
//
// in a build the project did not write.
#include "scale_comp.h"

#include <cstdio>

int main() {
    const std::uint32_t magic = scale_comp_spv[0];
    std::printf("magic=%08x words=%zu\n", magic, sizeof scale_comp_spv / sizeof scale_comp_spv[0]);
    return magic == 0x07230203u ? 0 : 1;
}
