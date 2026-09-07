// Nothing here names a generated file, and nothing here is Slang-specific: the
// same source would compile against `mcpp.rules.spirv` if the shader were GLSL.
import std;
import slang_consumer.shaders;

namespace shaders = slang_consumer::shaders;

int main() {
    const auto s = shaders::scale();
    const bool ok = s.size_bytes >= 4 && s.code[0] == 0x07230203u;

    std::cout << std::format("scale.slang: magic={:08x} bytes={} {}\n",
                             s.size_bytes >= 4 ? s.code[0] : 0u, s.size_bytes,
                             ok ? "ok" : "BAD");

    // The size the compiler stated, not `sizeof` of whatever it wrote. SPIR-V
    // is a sequence of 32-bit words, so a size that is not a multiple of four
    // means the accessor is reporting something other than the module.
    if (s.size_bytes % 4 != 0) {
        std::cout << "BAD: size is not a whole number of SPIR-V words\n";
        return 1;
    }
    std::cout << (ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
