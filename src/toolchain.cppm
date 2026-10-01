// mcpp.plugins.toolchain -- a build toolchain stated by the root build
// program (L2 of the build-plugin architecture; mcpp#755, protocol 15;
// feature `plugins-toolchain`).
//
// A project whose manifest says
//
//   [toolchain]
//   default = { configure = "build.mcpp" }
//
// has its root build program run twice. Its toolchain phase runs first, with
// the bootstrap toolchain, before the dependency graph is resolved; there the
// program states the toolchain that builds the project, and returns. The
// build phase runs as every build program does, with that toolchain.
//
//   import mcpp;
//   import mcpp.plugins.toolchain;
//   namespace tc = mcpp::plugins::toolchain;
//
//   int main() {
//       if (tc::configure([] {
//               auto d = tc::layout(tc::env("ACME_LLVM", "/opt/acme-llvm"));
//               tc::use(tc::with_launcher(d, "ccache"));
//           }))
//           return 0;
//       // the build phase
//   }
//
// A DESCRIPTION IS THE `[toolchain]` TABLE. Its fields are the table's keys,
// and the engine reads both in one place, so a toolchain stated here and one
// named in the manifest behave alike. What this module adds are the builders
// a manifest cannot express: a value read from the environment, a tree found
// by looking, a vendor SDK's environment script, and a tree refined part by part
// (a launcher, a sysroot, a tool named by role).

export module mcpp.plugins.toolchain;

import std;
import mcpp;

export namespace mcpp::plugins::toolchain {

struct description {
    // A managed toolchain (`llvm@23.1.3`), or empty for one named by path.
    std::string spec;
    // A toolchain named by path: its root and the rest of a `[toolchain]` table.
    std::string path, prefix, sysroot, family, launcher;
    std::vector<std::pair<std::string, std::string>> tools;   // role -> program
    // The statement that produced it, for the source the build reports.
    std::source_location where{};
};

// A managed toolchain the engine installs and drives, as `[toolchain]`
// would name it. Reported as a pinned source, stated by the build program.
inline description managed(std::string spec,
                           std::source_location w = std::source_location::current()) {
    description d; d.spec = std::move(spec); d.where = w; return d;
}
// A tree in the normalized layout: `<root>/bin/clang++` or `<root>/bin/g++`.
inline description layout(std::string root,
                          std::source_location w = std::source_location::current()) {
    description d; d.path = std::move(root); d.where = w; return d;
}
// A cross toolchain whose drivers and tools carry a prefix
// (`aarch64-none-linux-gnu-g++`).
inline description prefixed(std::string root, std::string prefix,
                            std::source_location w = std::source_location::current()) {
    description d; d.path = std::move(root); d.prefix = std::move(prefix); d.where = w; return d;
}
inline description with_launcher(description d, std::string launcher) {
    d.launcher = std::move(launcher); return d;
}
inline description with_sysroot(description d, std::string sysroot) {
    d.sysroot = std::move(sysroot); return d;
}
inline description with_family(description d, std::string family) {
    d.family = std::move(family); return d;
}
// A tool by role (`ld`, `ar`, `cxx`, ...), where the tree does not have it.
inline description with_tool(description d, std::string role, std::string program) {
    for (auto& [r, p] : d.tools) if (r == role) { p = std::move(program); return d; }
    d.tools.emplace_back(std::move(role), std::move(program));
    return d;
}

// The value of an environment variable, or `fallback`; the build program
// runs again when the variable changes.
inline std::string env(const char* name, std::string fallback = {}) {
    mcpp::rerun_if_env_changed(name);
    const char* v = std::getenv(name);
    return v && *v ? std::string(v) : std::move(fallback);
}

// The newest directory under `parent` whose name starts with `stem`, by name
// (`/opt/acme-llvm-23`, `/opt/acme-llvm-24` -> the second); empty when none.
inline std::string newest_under(const std::filesystem::path& parent, std::string_view stem) {
    std::error_code ec;
    std::string best;
    for (auto const& e : std::filesystem::directory_iterator(parent, ec)) {
        if (!e.is_directory(ec)) continue;
        const auto n = e.path().filename().string();
        if (!n.starts_with(stem)) continue;
        if (best.empty() || n > std::filesystem::path(best).filename().string())
            best = e.path().lexically_normal().generic_string();
    }
    mcpp::rerun_if_changed(parent.generic_string().c_str());
    return best;
}

// A VENDOR SDK'S ENVIRONMENT SCRIPT (Yocto's `environment-setup-<target>`,
// and SDKs shaped like it): `export NAME="value"` lines, read without running
// a shell. The driver is the first word of `CXX`, found on the script's own
// `PATH`; its directory's parent is the root, the part of its name before
// `g++`/`clang++` the prefix, and `--sysroot=` in `CXX` (or `SDKTARGETSYSROOT`)
// the sysroot. Empty `path` when the script does not describe a compiler.
inline description from_env_script(const std::filesystem::path& script,
                                   std::source_location w = std::source_location::current()) {
    description d; d.where = w;
    mcpp::rerun_if_changed(script.generic_string().c_str());
    std::ifstream in(script);
    std::map<std::string, std::string> vars;
    auto expand = [&](std::string v) {
        std::string out;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (v[i] != '$') { out += v[i]; continue; }
            std::size_t j = i + 1;
            const bool brace = j < v.size() && v[j] == '{';
            if (brace) ++j;
            std::size_t k = j;
            while (k < v.size() && (std::isalnum(static_cast<unsigned char>(v[k])) || v[k] == '_')) ++k;
            const auto name = v.substr(j, k - j);
            if (auto it = vars.find(name); it != vars.end()) out += it->second;
            else if (const char* e = std::getenv(name.c_str())) out += e;
            i = (brace && k < v.size() && v[k] == '}') ? k : k - 1;
        }
        return out;
    };
    for (std::string line; std::getline(in, line);) {
        std::string_view l(line);
        while (!l.empty() && (l.front() == ' ' || l.front() == '\t')) l.remove_prefix(1);
        if (!l.starts_with("export ")) continue;
        l.remove_prefix(7);
        const auto eq = l.find('=');
        if (eq == std::string_view::npos) continue;
        std::string name(l.substr(0, eq));
        std::string value(l.substr(eq + 1));
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'')
            && value.back() == value.front())
            value = value.substr(1, value.size() - 2);
        vars[name] = expand(value);
    }
    auto cxx = vars["CXX"];
    if (cxx.empty()) return d;
    const auto driver = cxx.substr(0, cxx.find(' '));
    if (auto at = cxx.find("--sysroot="); at != std::string::npos) {
        auto end = cxx.find(' ', at);
        d.sysroot = cxx.substr(at + 10, end == std::string::npos ? std::string::npos : end - at - 10);
    } else if (auto it = vars.find("SDKTARGETSYSROOT"); it != vars.end()) {
        d.sysroot = it->second;
    }
    std::string_view rest(vars["PATH"]);
    while (!rest.empty()) {
        auto sep = rest.find(':');
        std::filesystem::path dir(rest.substr(0, sep));
        std::error_code ec;
        if (!dir.empty() && std::filesystem::exists(dir / driver, ec)) {
            d.path = dir.parent_path().lexically_normal().generic_string();
            for (auto suffix : {std::string_view("clang++"), std::string_view("g++")})
                if (std::string_view(driver).ends_with(suffix))
                    d.prefix = driver.substr(0, driver.size() - suffix.size());
            break;
        }
        if (sep == std::string_view::npos) break;
        rest.remove_prefix(sep + 1);
    }
    return d;
}

// States the build toolchain. Only meaningful in the toolchain phase.
inline void use(const description& d) {
    if (!d.spec.empty()) mcpp::toolchain("spec", d.spec.c_str());
    if (!d.path.empty()) mcpp::toolchain("path", d.path.c_str());
    if (!d.prefix.empty()) mcpp::toolchain("prefix", d.prefix.c_str());
    if (!d.sysroot.empty()) mcpp::toolchain("sysroot", d.sysroot.c_str());
    if (!d.family.empty()) mcpp::toolchain("family", d.family.c_str());
    if (!d.launcher.empty()) mcpp::toolchain("launcher", d.launcher.c_str());
    for (auto const& [role, p] : d.tools)
        mcpp::toolchain(("tool." + role).c_str(), p.c_str());
    if (d.where.file_name() && *d.where.file_name())
        mcpp::toolchain("origin", std::format("{}:{}", d.where.file_name(), d.where.line()).c_str());
}

// Runs `state` in the toolchain phase and answers true: `main` then returns.
// Answers false in the build phase, without running it.
template <class F>
bool configure(F&& state) {
    if (std::string_view(mcpp::phase()) != "toolchain") return false;
    std::forward<F>(state)();
    return true;
}

} // namespace mcpp::plugins::toolchain
