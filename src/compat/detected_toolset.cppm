// COMPATIBILITY UNIT
//   keeps:       `toolset = detected` in the deps members: vcpkg and CMake find
//                their own toolset, as in 0.16.0
//   since:       0.17.0 (2026-09-28)
//   retires:     2027-03-28
//   replacement: the engine's toolchain selection; `msvc@system` makes the
//                default `resolved` use that Visual Studio instance
//   note:        "mcpp.plugins: toolset = detected is kept until 2027-03-28; ..."
//
// Retirement removes this file, its entry in `[features.plugins-core]`, the
// import in `src/toolset.cppm` and the `source::detected` branch there.
// `.github/scripts/check-compat-retirement.sh` fails once the date has passed.

export module mcpp.plugins.compat.detected_toolset;

import std;
import mcpp;

export namespace mcpp::plugins::compat {

struct detected_answer {
    // 0.16.0 named mcpp's own clang on the Linux libc++ row, where the host
    // compiler that detection finds uses libstdc++ and the two do not link.
    bool name_compilers = false;
};

inline detected_answer detected_toolset() {
    static bool noted = false;
    if (!noted) {
        noted = true;
        mcpp::report({
            .severity = "note",
            .message  = "mcpp.plugins: `toolset = detected` is kept until 2027-03-28",
            .impact   = "vcpkg and CMake choose their own toolset, which may differ from the one "
                        "mcpp builds the program with",
            .hint     = "remove the option to use the toolset mcpp resolved; select a Visual Studio "
                        "instance with the toolchain `msvc@system`",
        });
    }
    detected_answer a;
    a.name_compilers = std::string_view(mcpp::target_os()) == "linux"
                    && std::string_view(mcpp::cxx_stdlib()) == "libc++"
                    && std::string_view(mcpp::host()) == std::string_view(mcpp::target());
    return a;
}

} // namespace mcpp::plugins::compat
