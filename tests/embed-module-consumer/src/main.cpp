// Nothing here names a generated file. Two payloads arrive through one import.
import std;
import embed_module_consumer.assets;

namespace assets = embed_module_consumer::assets;

namespace {

bool check(std::string_view label, assets::payload p, std::string_view want) {
    const std::string_view got{ reinterpret_cast<const char*>(p.code), p.size_bytes };
    const bool ok = got == want;
    std::cout << std::format("{}: bytes={} {}\n", label, p.size_bytes,
                             ok ? "ok" : "BAD");
    if (!ok) std::cout << std::format("  want {:?}\n  got  {:?}\n", want, got);
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok &= check("alpha.txt", assets::alpha_txt(),
                "mcpp.tools.embed group fixture, first payload\n");
    ok &= check("beta.txt", assets::beta_txt(), "and the second\n");

    // Two payloads must be two objects. The shader fixture found this the hard
    // way: two byte-identical generated headers collapse under GCC's
    // `#pragma once`, and both accessors then return one array. These two
    // payloads differ, so the check is cheap insurance rather than a repeat.
    if (assets::alpha_txt().code == assets::beta_txt().code) {
        std::cout << "BAD: both payloads resolve to one array\n";
        ok = false;
    }
    std::cout << (ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
