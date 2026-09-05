// The generated header declares `const uint32_t scale_comp_spv[]`; the name
// is the shader's stem, its stage, and `_spv`, as mcpp.rules.spirv documents.
#include <cstdint>
#include <cstdio>
#include "scale_comp.h"

int main() {
    const std::uint32_t magic = scale_comp_spv[0];
    std::printf("magic=%08x words=%zu\n", magic, sizeof scale_comp_spv / sizeof scale_comp_spv[0]);
    return magic == 0x07230203u ? 0 : 1;
}
