// Identical in shape to the header-storage consumer: the storage is not
// something a consumer can see.
import std;
import spirv_object_storage.shaders;

int main() {
    const auto s = spirv_object_storage::shaders::scale_comp();
    const bool ok = s.size_bytes >= 4 && s.code[0] == 0x07230203u;
    std::cout << std::format("magic={:08x} bytes={} {}\n",
                             s.size_bytes >= 4 ? s.code[0] : 0u, s.size_bytes,
                             ok ? "ok" : "BAD");

    // SPIR-V is a sequence of 32-bit words. Under object storage the size comes
    // from subtracting two linker symbols rather than from `sizeof`, so a
    // section the assembler padded would show up here and nowhere else.
    if (s.size_bytes % 4 != 0) {
        std::cout << "BAD: size is not a whole number of SPIR-V words\n";
        return 1;
    }
    std::cout << (ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
