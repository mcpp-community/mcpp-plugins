// COMPATIBILITY UNIT
//   keeps:       the 0.16.0 names in `mcpp::deps`: `write_if_changed`,
//                `deploy_entry`, `deployed_file`, `deploy_after`,
//                `compilers` and `program_compilers`
//   since:       0.17.0 (2026-09-28)
//   retires:     2027-03-28
//   replacement: `mcpp::plugins::fs` (module `mcpp.plugins.fs`) for the first
//                four; `mcpp::plugins::toolset::resolve` for the last two
//   note:        "mcpp.plugins: mcpp::deps::program_compilers is kept until
//                2027-03-28; ..." (printed by `program_compilers`; the other
//                names are aliases, which print nothing)
//
// `mcpp.deps` re-exports this module, so a build program that imported
// `mcpp.deps` for these names keeps compiling. Retirement removes this file,
// its entry in `[features.deps]` and the re-export in `deps/deps.cppm`.
// `.github/scripts/check-compat-retirement.sh` fails once the date has passed.

export module mcpp.deps.compat;

import std;
import mcpp;
import mcpp.plugins.fs;

export namespace mcpp::deps {

using deploy_entry  = mcpp::plugins::fs::deploy_entry;
using deployed_file = mcpp::plugins::fs::deployed_file;
using mcpp::plugins::fs::write_if_changed;
using mcpp::plugins::fs::deploy_after;

struct compilers {
    std::string c, cxx;
    explicit operator bool() const { return !cxx.empty(); }
};

// 0.16.0's answer: mcpp's clang on the Linux libc++ row, nothing elsewhere.
// Computed here rather than through `mcpp.plugins.toolset`: `mcpp.deps`
// re-exports this unit, and with the toolset module in that chain GCC 16
// emitted no `std::vector<std::string>::push_back(std::string&&)` for a build
// program calling `mcpp::deps::archive::unpack` (undefined at its link).
inline compilers program_compilers() {
    static bool noted = false;
    if (!noted) {
        noted = true;
        mcpp::report({
            .severity = "note",
            .message  = "mcpp.plugins: mcpp::deps::program_compilers is kept until 2027-03-28",
            .impact   = "none until then; the name is removed afterwards",
            .hint     = "use mcpp::plugins::toolset::resolve({.cc = mcpp::plugins::toolset::compiler::row}), "
                        "which names the row's tools on every row",
        });
    }
    if (std::string_view(mcpp::target_os()) != "linux"
        || std::string_view(mcpp::cxx_stdlib()) != "libc++"
        || std::string_view(mcpp::compiler()) != "clang"
        || std::string_view(mcpp::host()) != std::string_view(mcpp::target())) return {};
    const std::filesystem::path bin = std::filesystem::path(mcpp::toolchain_dir()) / "bin";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(bin / "clang++", ec) || !std::filesystem::is_regular_file(bin / "clang", ec))
        return {};
    return { mcpp::plugins::fs::generic(bin / "clang"), mcpp::plugins::fs::generic(bin / "clang++") };
}

} // namespace mcpp::deps
