// Nothing here names a generated file. The shaders arrive through one import,
// and the two that share a stem are told apart by the directory they came from.
//
// `shader_app`, not `spirv_module_consumer`: the module root comes from
// `[package] name`, and this project's directory is named differently on
// purpose so that the two derivations are distinguishable here.
import std;
import shader_app.shaders;

namespace shaders = shader_app::shaders;

namespace {

// The one property that identifies a SPIR-V module without executing it.
constexpr std::uint32_t kSpirvMagic = 0x07230203u;

bool check(std::string_view label, shaders::payload p) {
    const bool ok = p.size_bytes >= 4 && p.code[0] == kSpirvMagic;
    std::cout << std::format("{}: magic={:08x} bytes={} {}\n", label,
                             p.size_bytes >= 4 ? p.code[0] : 0u, p.size_bytes,
                             ok ? "ok" : "BAD");
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok &= check("a/scale.comp", shaders::a::scale_comp());
    ok &= check("b/scale.comp", shaders::b::scale_comp());

    // The two shaders are the same source in two directories, so they must
    // produce identical modules -- and they must be two distinct objects, not
    // one symbol reached twice. Both are properties of the naming, and a build
    // that collapsed them would still print two lines above.
    const auto x = shaders::a::scale_comp();
    const auto y = shaders::b::scale_comp();
    if (x.code == y.code) {
        std::cout << "BAD: both namespaces resolve to one array\n";
        ok = false;
    }
    if (x.size_bytes != y.size_bytes) {
        std::cout << "BAD: identical sources produced different sizes\n";
        ok = false;
    }
    std::cout << (ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
