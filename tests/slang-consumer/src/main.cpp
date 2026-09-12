// Nothing here names a generated file, and nothing here is Slang-specific: the
// same source would compile against `mcpp.rules.spirv` if the shader were GLSL.
import std;
import slang_consumer.shaders;

namespace shaders = slang_consumer::shaders;

namespace {

// Is `name` written somewhere in the module? An OpEntryPoint carries its name
// as a literal string, so the bytes of a SPIR-V module say what the entry point
// was called -- which is how the fixture observes an argument that reached the
// compiler without executing anything.
bool carries(const shaders::payload& p, std::string_view name) {
    const std::string_view bytes(reinterpret_cast<const char*>(p.code), p.size_bytes);
    return bytes.find(name) != std::string_view::npos;
}

bool check(std::string_view label, const shaders::payload& p,
           std::string_view expected, std::string_view forbidden) {
    const bool magic = p.size_bytes >= 4 && p.code[0] == 0x07230203u;
    const bool words = p.size_bytes % 4 == 0;
    const bool named = carries(p, expected);
    const bool clean = !carries(p, forbidden);
    std::cout << std::format("{}: magic={:08x} bytes={} entry={} {}\n",
                             label, p.size_bytes >= 4 ? p.code[0] : 0u, p.size_bytes,
                             named ? expected : "?",
                             magic && words && named && clean ? "ok" : "BAD");
    if (!magic) std::cout << "BAD: not a SPIR-V module\n";
    // The size the compiler stated, not `sizeof` of whatever it wrote. SPIR-V
    // is a sequence of 32-bit words, so a size that is not a multiple of four
    // means the accessor is reporting something other than the module.
    if (!words) std::cout << "BAD: size is not a whole number of SPIR-V words\n";
    if (!named) std::cout << std::format("BAD: the entry point is not `{}`, so an argument did not reach slangc\n", expected);
    if (!clean) std::cout << std::format("BAD: `{}` is present, so a per-file argument reached the wrong file\n", forbidden);
    return magic && words && named && clean;
}

} // namespace

int main() {
    bool ok = true;
    // `-fvk-use-entrypoint-name` for both, `-DOFFSET_ENTRY` for one.
    ok &= check("scale.slang",  shaders::scale(),  "computeMain",       "wrongMain");
    ok &= check("offset.slang", shaders::offset(), "offsetMainDefined", "wrongMain");
    std::cout << (ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
