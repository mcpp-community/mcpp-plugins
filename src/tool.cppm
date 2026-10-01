// mcpp.plugins.tool -- where a tool a plugin runs comes from (L2 of the
// build-plugin architecture; mcpp#755, protocol 15).
//
// A member that runs a program -- cmake, glslangValidator, slangc, nvcc,
// appimagetool -- answers one question before it can plan anything: which
// program. Each member used to answer it its own way: an option here, an
// environment variable there, a silent PATH fallback in two members and a
// refusal of PATH in two others. This module answers it once, in one order:
//
//   1. the build program's choice   `o.cmake = "/usr/bin/cmake"`, or
//                                    `tool::on_path()`, `tool::root(dir)`
//   2. a member's legacy variable   `MCPP_SLANGC`, kept for compatibility
//   3. the engine's override        `[xlings.overrides]`,
//                                    `MCPP_XLINGS_OVERRIDE_<NS>_<NAME>`,
//                                    config.toml -- the payload is then not
//                                    installed at all
//   4. the declared payload         installed before the program runs, or,
//                                    declared `provision = "on-request"`,
//                                    asked for here and installed then
//
// A CHOICE NEVER ASKS FOR THE PAYLOAD. That is what makes "the build program
// names its own tool" mean "the payload is not downloaded": a member whose
// payload is declared on request calls `resolve`, and only the fourth step
// requests it.
//
// EVERY ANSWER IS RECORDED. `resolve` states a `mcpp:decision=` with the
// source and, for a choice, the `build.mcpp` line that made it, so the build
// reports `Using cmake (mcpp.deps.cmake) ← /usr/bin/cmake [program ·
// build.mcpp:9]` and `mcpp why tool cmake` answers from the same record.

module;
#include <cstdlib>

export module mcpp.plugins.tool;

import std;
import mcpp;

namespace mcpp::plugins::tool::detail {

inline bool is_file(const std::filesystem::path& p) {
    std::error_code ec;
    return !p.empty() && std::filesystem::is_regular_file(p, ec);
}

inline std::string generic(const std::filesystem::path& p) {
    return p.lexically_normal().generic_string();
}

#if defined(_WIN32)
inline constexpr char kPathSep = ';';
inline constexpr std::string_view kExe = ".exe";
#else
inline constexpr char kPathSep = ':';
inline constexpr std::string_view kExe = "";
#endif

// `<dir>/<name>`, with and without `.exe`.
//
// BOTH SPELLINGS ON EVERY HOST, and the suffix of the machine doing the
// building is only the preferred one. A payload repacked with the other
// convention is then found instead of silently missed, and the cost is one
// `stat`; it is also what lets a plugin's test state a Windows row and run on
// Linux, which `mcpp.plugins.testing` does.
inline std::string program_in(const std::filesystem::path& dir, std::string_view name) {
    const std::string bare(name);
    const std::string exe = bare + ".exe";
    for (auto const& cand : kExe.empty() ? std::array<std::string, 2>{bare, exe}
                                         : std::array<std::string, 2>{exe, bare}) {
        auto p = dir / cand;
        if (is_file(p)) return generic(p);
    }
    return {};
}

// A STATED PATH, WITH THE SAME SUFFIX RULE AS A DISCOVERED ONE. A build program
// writes the path a shell gave it, and on Windows `command -v cmake` answers
// `C:/Program Files/CMake/bin/cmake` for a `cmake.exe`; process creation there
// appends the suffix itself, so a resolver that insisted on the exact spelling
// refused a program the host would have run (measured in CI: the cmake consumer
// reported `options::cmake = "C:/Program Files/CMake/bin/cmake" (not found)` on a
// runner carrying cmake). Empty when neither spelling is a file.
inline std::string program_at(const std::filesystem::path& p) {
    if (is_file(p)) return generic(p);
    auto with = p;
    with += ".exe";
    if (is_file(with)) return generic(with);
    if (p.extension() == ".exe") {
        auto without = p;
        without.replace_extension();
        if (is_file(without)) return generic(without);
    }
    return {};
}

inline std::string find_on_path(std::string_view name) {
    const char* path = std::getenv("PATH");
    if (!path) return {};
    std::string_view rest(path);
    while (!rest.empty()) {
        auto sep = rest.find(kPathSep);
        auto dir = rest.substr(0, sep);
        if (!dir.empty())
            if (auto p = program_in(std::filesystem::path(dir), name); !p.empty()) return p;
        if (sep == std::string_view::npos) break;
        rest.remove_prefix(sep + 1);
    }
    return {};
}

// The root a program implies: `<root>/bin/<program>`, or its directory.
inline std::string root_of(const std::filesystem::path& program) {
    auto dir = program.parent_path();
    if (dir.filename() == "bin") return generic(dir.parent_path());
    return generic(dir);
}

} // namespace mcpp::plugins::tool::detail

export namespace mcpp::plugins::tool {

// WHAT A BUILD PROGRAM STATES ABOUT ONE TOOL, as a member's option.
//
// A STRING IS A PROGRAM, so every option that was a `std::string` keeps its
// spelling: `o.cmake = "/usr/bin/cmake"` constructs a choice, and an empty
// string is the default. The constructor's default argument records the line
// of the assignment -- `std::source_location::current()` in a default
// argument is evaluated where the call is written, which is the build
// program -- and the line reaches the decision record.
struct choice {
    enum class kind { payload, program, root, on_path };
    kind                 how = kind::payload;
    std::string          value;
    std::source_location where{};

    choice() = default;
    choice(std::string program,
           std::source_location w = std::source_location::current())
        : how(program.empty() ? kind::payload : kind::program),
          value(std::move(program)), where(w) {}
    choice(const char* program,
           std::source_location w = std::source_location::current())
        : choice(std::string(program ? program : ""), w) {}

    // The default: the engine's answer (override or payload).
    bool is_default() const { return how == kind::payload; }
    // The text a member stored before this type, for code that read it.
    const std::string& str() const { return value; }
};

inline choice payload() { return {}; }
// This program. A relative path is relative to the package root.
inline choice program(std::string path,
                      std::source_location w = std::source_location::current()) {
    choice c; c.how = choice::kind::program; c.value = std::move(path); c.where = w; return c;
}
// A directory laid out like the payload (`<root>/bin/<program>`).
inline choice root(std::string dir,
                   std::source_location w = std::source_location::current()) {
    choice c; c.how = choice::kind::root; c.value = std::move(dir); c.where = w; return c;
}
// The first program of the member's list found on PATH, or `name`.
inline choice on_path(std::string name = {},
                      std::source_location w = std::source_location::current()) {
    choice c; c.how = choice::kind::on_path; c.value = std::move(name); c.where = w; return c;
}

// WHAT A MEMBER KNOWS ABOUT ONE OF ITS TOOLS, written once per tool.
struct spec {
    std::string who;                     // the member's module: "mcpp.deps.cmake"
    std::string ns = "xim";              // the payload's namespace
    std::string package;                 // the payload: "cmake"
    std::vector<std::string> programs;   // program names, in preference order
    std::vector<std::string> bin_dirs = {"bin", ""};   // where, under a root
    std::string option;                  // "options::cmake", for messages
    std::string legacy_env;              // a variable the member read before, or empty
    // Ask for a payload declared `provision = "on-request"` when nothing else
    // answers. A member that only looks (to report what is there) passes false.
    bool        request = true;
};

enum class from { choice, legacy_env, override_, payload, path, none, pending };

inline std::string_view name(from f) {
    switch (f) {
        case from::choice:     return "choice";
        case from::legacy_env: return "env";
        case from::override_:  return "override";
        case from::payload:    return "payload";
        case from::path:       return "path";
        case from::pending:    return "pending";
        case from::none:       break;
    }
    return "none";
}

struct found {
    std::string program;   // the program, absolute, forward slashes
    std::string root;      // the root it implies, when one does
    from        source = from::none;
    // What was consulted and what each answered, for `describe_missing`.
    std::vector<std::string> tried;

    explicit operator bool() const { return !program.empty(); }
    // The payload was asked for: the engine installs it and runs the program
    // again. The member returns without planning what needs the tool.
    bool pending() const { return source == from::pending; }
};

// The subject of the decision record: `tool:<module>:<name>`.
inline std::string subject_of(const spec& s) {
    return "tool:" + s.who + ":" + (s.programs.empty() ? s.package : s.programs.front());
}

// The program in a root, by the member's names and directories.
inline std::string program_in_root(const spec& s, const std::filesystem::path& root) {
    for (auto const& sub : s.bin_dirs)
        for (auto const& p : s.programs)
            if (auto hit = detail::program_in(sub.empty() ? root : root / sub, p); !hit.empty())
                return hit;
    return {};
}

inline found resolve(const spec& s, const choice& c = {}) {
    found out;
    const auto payloadKey = s.ns + ":" + s.package;
    auto record = [&](from f, std::string_view file = {}, unsigned line = 0) {
        out.source = f;
        mcpp::decision(subject_of(s).c_str(), std::string(name(f)).c_str(),
                       out.program.c_str(), std::string(file).c_str(), line,
                       payloadKey.c_str());
    };
    // 1. The build program's choice.
    if (!c.is_default()) {
        const std::string file = c.where.file_name() ? c.where.file_name() : "";
        const auto line = static_cast<unsigned>(c.where.line());
        std::filesystem::path base = mcpp::manifest_dir();
        switch (c.how) {
            case choice::kind::program: {
                std::filesystem::path p(c.value);
                if (p.is_relative() && p.has_parent_path()) p = base / p;
                if (!p.has_parent_path()) {          // a bare name: on PATH
                    out.program = detail::find_on_path(c.value);
                    if (!out.program.empty()) {
                        out.root = detail::root_of(out.program);
                        record(from::choice, file, line);
                        return out;
                    }
                } else if (auto hit = detail::program_at(p); !hit.empty()) {
                    out.program = std::move(hit);
                    out.root = detail::root_of(out.program);
                    record(from::choice, file, line);
                    return out;
                }
                out.tried.push_back(std::format("{} = \"{}\" (not found)", s.option, c.value));
                return out;   // a stated choice that fails is not replaced by another source
            }
            case choice::kind::root: {
                std::filesystem::path r(c.value);
                if (r.is_relative()) r = base / r;
                out.program = program_in_root(s, r);
                if (!out.program.empty()) {
                    out.root = detail::generic(r);
                    record(from::choice, file, line);
                    return out;
                }
                out.tried.push_back(std::format("{} = root(\"{}\") (no {} there)", s.option,
                                                c.value, s.programs.empty() ? s.package : s.programs.front()));
                return out;
            }
            case choice::kind::on_path: {
                std::vector<std::string> names = c.value.empty() ? s.programs
                                                                 : std::vector<std::string>{c.value};
                for (auto const& n : names)
                    if (auto p = detail::find_on_path(n); !p.empty()) {
                        out.program = p;
                        out.root = detail::root_of(p);
                        record(from::path, file, line);
                        return out;
                    }
                out.tried.push_back(std::format("{} = on_path() (not on PATH)", s.option));
                return out;
            }
            case choice::kind::payload: break;
        }
    }
    out.tried.push_back(std::format("{} (not set)", s.option));
    // 2. The member's legacy variable.
    if (!s.legacy_env.empty()) {
        mcpp::rerun_if_env_changed(s.legacy_env.c_str());
        if (const char* v = std::getenv(s.legacy_env.c_str()); v && *v) {
            out.program = detail::generic(v);
            out.root = detail::root_of(out.program);
            out.source = from::legacy_env;
            mcpp::decision(subject_of(s).c_str(), "env", out.program.c_str(),
                           s.legacy_env.c_str(), 0, payloadKey.c_str());
            return out;
        }
        out.tried.push_back(std::format("{} (not set)", s.legacy_env));
    }
    // 3 and 4. The engine's answer: an override, or the payload.
    const std::string source = mcpp::xpkg_source(s.ns.c_str(), s.package.c_str());
    if (source == "override") {
        out.program = mcpp::xpkg_program(s.ns.c_str(), s.package.c_str());
        const std::string dir = mcpp::xpkg_dir(s.ns.c_str(), s.package.c_str());
        if (out.program.empty() && !dir.empty()) out.program = program_in_root(s, dir);
        out.root = dir;
        if (!out.program.empty()) { record(from::override_); return out; }
        out.tried.push_back(std::format("the override of {} (names no {} under '{}')",
                                        payloadKey, s.programs.empty() ? s.package
                                                                       : s.programs.front(), dir));
        return out;
    }
    if (source == "pending" && s.request) {
        (void)mcpp::xpkg_request(s.ns.c_str(), s.package.c_str());
        out.source = from::pending;
        out.tried.push_back(std::format("payload {} (requested)", payloadKey));
        return out;
    }
    if (const std::string dir = mcpp::xpkg_dir(s.ns.c_str(), s.package.c_str()); !dir.empty()) {
        out.program = program_in_root(s, dir);
        out.root = dir;
        if (!out.program.empty()) { record(from::payload); return out; }
        out.tried.push_back(std::format("payload {} at '{}' (no {} in it)", payloadKey, dir,
                                        s.programs.empty() ? s.package : s.programs.front()));
        return out;
    }
    out.tried.push_back(source == "pending"
        ? std::format("payload {} (declared on request, not requested)", payloadKey)
        : std::format("payload {} (not declared for this build, or not installed)", payloadKey));
    return out;
}

// THE ONE REFUSAL TEXT: what was consulted, and the four ways to name the
// tool. A member prints it with `mcpp::warning` (R1.2) or its own stream.
inline std::string describe_missing(const spec& s, const found& f) {
    const auto tool = s.programs.empty() ? s.package : s.programs.front();
    const auto key = s.ns + ":" + s.package;
    std::string env = "MCPP_XLINGS_OVERRIDE_";
    for (char ch : key)
        env += (ch >= 'a' && ch <= 'z') ? char(ch - 'a' + 'A')
             : ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) ? ch : '_';
    std::string out = std::format("{}: no {}.\n", s.who, tool);
    for (auto const& t : f.tried) out += "  consulted: " + t + "\n";
    out += std::format(
        "  Any one of these names it:\n"
        "    o.{} = \"/path/to/{}\";                  // build.mcpp\n"
        "    [xlings.overrides] \"{}\" = \"/path\"      // mcpp.toml\n"
        "    {}=/path                               // environment\n"
        "  or install the payload: `mcpp build` provisions {} when it is declared.",
        s.option.starts_with("options::") ? s.option.substr(9) : s.option, tool, key, env, key);
    return out;
}

} // namespace mcpp::plugins::tool
