// No build program in this project, and no generated file named here either.
import std;
import spirv_zero_config.shaders;

int main() {
    const auto s = spirv_zero_config::shaders::scale_comp();
    const bool ok = s.size_bytes >= 4 && s.code[0] == 0x07230203u;
    std::cout << std::format("magic={:08x} bytes={} {}\n",
                             s.size_bytes >= 4 ? s.code[0] : 0u, s.size_bytes,
                             ok ? "ok" : "BAD");
    std::cout << (ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
