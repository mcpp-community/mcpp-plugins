// mcpp.plugins.platform -- what a platform's plugin and a package format's
// provider state (L2 of the build-plugin architecture; feature
// `plugins-platform`; mcpp SPEC-013 §12, SPEC-014 §6).
//
// A PLATFORM PACKAGE STATES ITS TARGETS' CONFIGURATION IN `[platform]`, and a
// plugin (`[platform] plugin = "build.mcpp"`) states the part it computes: the
// link's driver and inputs, the artifacts' names, the seal steps, where the
// runner runs, and the package formats it provides. The program runs in the
// `platform` phase (`mcpp::phase()`), whose directives are exactly these. The
// engine's interface spells each as a line (`mcpp::link_driver`,
// `mcpp::seal(step, token)`, ...); this module states each as one call with
// the value checked where it is written, so a malformed field is refused in the
// plugin, naming the call, rather than by the engine, naming a line.
//
//   import mcpp;
//   import mcpp.plugins.platform;
//   namespace pf = mcpp::plugins::platform;
//
//   int main() {
//       pf::link::driver("lld");
//       pf::artifact_names("{name}.img", "lib{name}.so");
//       pf::seal({ { "signer", "{}" } });
//       pf::run_host("same-arch");
//       pf::provide_format("img", [](const mcpp::plugins::package::info& p, mcpp::action& a) {
//           a.arg("bundle").arg(p.root.c_str());   // ... the format's command
//           return true;
//       });
//       return 0;
//   }
//
// A PACKAGE FORMAT IS PROVIDED IN TWO HALVES, AND `provide_format` IS BOTH.
// The name is declared on every pass (`mcpp::provides_pack_format`), so that
// `mcpp pack --pkg-format <unknown>` names what the graph provides; the action
// is submitted only in the pass that asks for the format, and is made from the
// staged package's `mcpp-package.json` (`mcpp::plugins::package`), which says
// which files are the program, the images and the resources.

export module mcpp.plugins.platform;

import std;
import mcpp;
import mcpp.plugins;

export namespace mcpp::plugins::platform {

// A builder's answer: stated, or refused with the reason. A refusal is also
// written to standard error and stated as a warning, so a plugin that goes on
// is still reported; one that stops returns non-zero.
struct statement {
    bool        ok = true;
    std::string reason;
    explicit operator bool() const { return ok; }
};

// What a format's provider makes from the staged package: the action's
// command, inputs and outputs. `false` submits nothing (the provider reports
// why).
using make_action = std::function<bool(const package::info&, mcpp::action&)>;

namespace detail {

// One line of the warning channel: line breaks folded, as the channel is one
// line per directive.
inline statement refuse(std::string what, std::string reason) {
    const std::string message = std::format("mcpp.plugins.platform: {}: {}", what, reason);
    std::cerr << message << '\n';
    std::string folded;
    for (char c : message) folded += (c == '\n' || c == '\r') ? ' ' : c;
    mcpp::warning(folded.c_str());
    return { false, std::move(reason) };
}

// A token of a directive is one line, and not empty.
inline bool one_line(std::string_view t) {
    return !t.empty() && t.find('\n') == std::string_view::npos && t.find('\r') == std::string_view::npos;
}

// `{name}` once, and (an executable's) at the start.
inline bool names_once(std::string_view pattern, bool atStart) {
    const auto at = pattern.find("{name}");
    if (at == std::string_view::npos || pattern.find("{name}", at + 1) != std::string_view::npos) return false;
    return !atStart || at == 0;
}

// The formats the engine writes itself; a provider cannot claim them.
inline bool engine_format(std::string_view n) {
    return n == "tar" || n == "dir" || n == "zip" || n == "single-file";
}

// A relative path is the platform package's own. Spelled on strings: a module
// that instantiates `std::filesystem::path`'s iterator (`lexically_normal`)
// breaks every importer under MSVC 14.52 (docs/plugin-development.md §4).
inline std::string absolute_in_package(std::string_view path) {
    const bool absolute = path.starts_with('/') || path.starts_with('\\')
        || (path.size() > 2 && path[1] == ':' && (path[2] == '/' || path[2] == '\\'));
    if (absolute) return std::string(path);
    std::string dir = mcpp::manifest_dir();
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    std::string rel(path);
    while (rel.starts_with("./")) rel.erase(0, 2);
    return dir.empty() ? rel : dir + "/" + rel;
}

} // namespace detail

namespace link {

// `ld.lld` itself links ("lld"), or the compiler driver does ("compiler").
inline statement driver(std::string_view d) {
    if (d != "lld" && d != "compiler")
        return detail::refuse("link::driver", std::format("'{}' is not a driver; the drivers are "
                                                          "\"lld\" and \"compiler\"", d));
    mcpp::link_driver(std::string(d).c_str());
    return {};
}

// The symbol every executable is entered at (protocol 16).
inline statement start(std::string_view symbol) {
    const bool word = !symbol.empty()
        && std::ranges::all_of(symbol, [](char c) {
               return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$';
           })
        && !std::isdigit(static_cast<unsigned char>(symbol.front()));
    if (!word)
        return detail::refuse("link::start", std::format("'{}' is not a symbol", symbol));
    mcpp::link_start(std::string(symbol).c_str());
    return {};
}

// A file linked into every executable and shared library, assembled first when
// it is a `.S` or `.s`. A relative path is the platform package's own.
inline statement input_file(std::string_view path) {
    if (!detail::one_line(path))
        return detail::refuse("link::input_file", "the path is empty or spans lines");
    mcpp::link_input(detail::absolute_in_package(path).c_str());
    return {};
}

// The standard output of a program, saved as `as` and linked as `input_file`
// links a file; `argv[0]` names the program (a platform tool by its name).
inline statement input_from(std::string_view as, std::span<const std::string> argv) {
    if (!detail::one_line(as) || as.find(':') != std::string_view::npos)
        return detail::refuse("link::input_from", std::format(
            "'{}' is not a file name to save the output as (one line, no ':')", as));
    if (argv.empty() || !std::ranges::all_of(argv, [](auto const& t) { return detail::one_line(t); }))
        return detail::refuse("link::input_from", std::format(
            "the program for '{}' is empty, or a word of it is empty or spans lines", as));
    const std::string a(as);
    for (auto const& token : argv) mcpp::link_input_from(a.c_str(), token.c_str());
    return {};
}

inline statement input_from(std::string_view as, std::initializer_list<std::string_view> argv) {
    std::vector<std::string> words(argv.begin(), argv.end());
    return input_from(as, std::span<const std::string>(words));
}

} // namespace link

// How the artifacts are named: an executable's pattern begins with `{name}`
// ("{name}.oke"), a shared library's names it once ("lib{name}.okso"). An
// empty pattern leaves that kind as the target names it.
inline statement artifact_names(std::string_view exe, std::string_view shared = {}) {
    if (!exe.empty() && (!detail::one_line(exe) || !detail::names_once(exe, true)))
        return detail::refuse("artifact_names", std::format(
            "the executable's pattern '{}' names the artifact as {{name}} once, at its start", exe));
    if (!shared.empty() && (!detail::one_line(shared) || !detail::names_once(shared, false)))
        return detail::refuse("artifact_names", std::format(
            "the shared library's pattern '{}' names the artifact as {{name}} once", shared));
    if (!exe.empty()) mcpp::artifact_name("exe", std::string(exe).c_str());
    if (!shared.empty()) mcpp::artifact_name("shared", std::string(shared).c_str());
    return {};
}

// The seal: steps run in order on each linked artifact (`{}`) before it is
// published, `{needed}` naming the shared artifacts it needs. A step that fails
// keeps the artifact from being published.
inline statement seal(std::span<const std::vector<std::string>> steps) {
    if (steps.empty()) return detail::refuse("seal", "no step");
    for (std::size_t i = 0; i < steps.size(); ++i)
        if (steps[i].empty() || !std::ranges::all_of(steps[i], [](auto const& t) { return detail::one_line(t); }))
            return detail::refuse("seal", std::format(
                "step {} is empty, or a word of it is empty or spans lines", i));
    // Steps then words: each word is one directive line of its step.
    for (std::size_t i = 0; i < steps.size(); ++i)
        for (auto const& token : steps[i]) mcpp::seal(static_cast<int>(i), token.c_str());
    return {};
}

inline statement seal(std::initializer_list<std::initializer_list<std::string_view>> steps) {
    std::vector<std::vector<std::string>> owned;
    for (auto const& step : steps) owned.emplace_back(step.begin(), step.end());
    return seal(std::span<const std::vector<std::string>>(owned));
}

// Where the runner runs an artifact: on a machine of its architecture
// ("same-arch"), or from any machine ("any": an emulator, a probe).
inline statement run_host(std::string_view where) {
    if (where != "same-arch" && where != "any")
        return detail::refuse("run_host", std::format(
            "'{}' is neither \"same-arch\" nor \"any\"", where));
    mcpp::run_host(std::string(where).c_str());
    return {};
}

// A package format this program provides (SPEC-014 §6): declared on every
// pass, made and submitted only in the pass `mcpp pack --pkg-format <name>`
// runs, from the staged package. The action's role is `artifact`, its id and
// description the format's name unless `make` sets them.
inline statement provide_format(std::string_view name, const make_action& make) {
    const std::string format(name);
    if (!detail::one_line(format) || format.find_first_of(" \t:") != std::string::npos)
        return detail::refuse("provide_format", std::format("'{}' is not a format's name", format));
    if (detail::engine_format(format))
        return detail::refuse("provide_format", std::format(
            "'{}' is a format mcpp writes itself (tar, dir, zip, single-file)", format));
    mcpp::provides_pack_format(format.c_str());
    if (std::string_view(mcpp::pack_format()) != format) return {};

    const std::string stage = mcpp::pack_stage_dir();
    if (stage.empty())
        return detail::refuse("provide_format", std::format(
            "the pass for '{}' names no staged tree (mcpp::pack_stage_dir())", format));
    auto pkg = package::read(stage);
    if (!pkg) return detail::refuse("provide_format", pkg.error());
    mcpp::action a;
    a.id          = format.c_str();
    a.role        = mcpp::roles::artifact;
    a.description = format.c_str();
    if (!make(*pkg, a)) return { false, std::format("the provider of '{}' made no action", format) };
    a.submit();
    return {};
}

} // namespace mcpp::plugins::platform
