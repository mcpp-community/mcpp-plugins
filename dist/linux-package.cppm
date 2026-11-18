// mcpp.dist.linux_package -- a staged program becomes a deb or an rpm.
//
// ONE MODULE FOR TWO FORMATS, BECAUSE THE TWO DIFFER ONLY IN THE WRITER.
// `dist-deb` and `dist-rpm` place the same files at the same paths; what a
// Debian package and an RPM package disagree about -- the archive layout, the
// control fields' spelling, the architecture's name -- is the packaging
// tool's business, not the member's. The tool is nfpm, which writes both from
// one description, so neither member depends on the host's `dpkg-deb` or
// `rpmbuild` and both run on every host the `xim:nfpm` payload exists for.
//
// THE CLOSURE GOES UNDER A PRIVATE PREFIX. `mcpp pack` stages the program,
// the libraries it loads, and its launcher as one relocatable tree. The
// package installs that tree as it is below `/opt/<package>` and adds
// `/usr/bin/<package>`, which execs the tree's launcher, a desktop entry, and
// an icon. Distribution-style packaging -- one file per FHS directory and
// dependencies on the distribution's own library packages -- needs a mapping
// from each staged library to the package that provides it on each
// distribution, which nothing here can know; the project states the packages
// it relies on in `options::depends` instead.
//
// EVERY FILE IS LISTED, WITH ITS MODE AND TIME STATED. nfpm's `tree` entry
// keeps each source file's mtime and permissions, and measured on nfpm
// 2.49.0 its top-level `mtime` does not reach a tree's files, so a package
// built from a re-staged tree differs byte for byte from the last one. The
// member therefore writes one entry per staged file with an explicit mode and
// `SOURCE_DATE_EPOCH` (or a fixed time), and the same tree gives the same
// package.

export module mcpp.dist.linux_package;

import std;
import mcpp;
import mcpp.plugins;
import mcpp.plugins.tool;

export namespace mcpp::dist::linux_package {

// ─── Options ───────────────────────────────────────────────────────────────

struct options {
    // The program target the package installs. Empty means the package name.
    std::string target;

    // The package's own name, which is also `/opt/<name>`, `/usr/bin/<name>`,
    // and the desktop entry's file name. Empty derives it from `[package]
    // name`: lowercase, with every character a Debian package name cannot
    // carry replaced by `-`.
    std::string name;

    // Control fields. Each empty one comes from `[package]`.
    std::string version;      // default: `[package] version`
    std::string description;  // default: `[package] description`
    std::string maintainer;   // default: the first of `[package] authors`
    std::string license;      // default: `[package] license`
    std::string homepage;     // default: `[package] repo`

    // Packages the machine provides and the program needs, by the name the
    // target distribution gives them, such as `libgtk-4-1` for a deb or
    // `gtk4` for an rpm. The staged closure carries what it vendored; what it
    // left to the machine is listed in the staged `mcpp-package.json`.
    std::vector<std::string> depends;

    // The desktop entry. `desktop = false` installs none, for a program that
    // is not started from a desktop.
    bool desktop = true;
    std::string app_name;                 // `Name=`; default: the package name
    std::vector<std::string> categories;  // default: `Utility`
    bool terminal = false;
    // A PNG or SVG. Empty installs no icon file and the entry names the
    // package, which a desktop resolves to its generic icon.
    std::string icon;

    // The install prefix of the staged tree. Empty means `/opt/<name>`.
    std::string prefix;

    // `nfpm`. Default: the `xim:nfpm` payload this feature declares.
    mcpp::plugins::tool::choice tool;

    // Where the produced package lands. Empty means
    // `<out_dir>/<name>_<version>_<arch>.deb` or
    // `<out_dir>/<name>-<version>-1.<arch>.rpm`.
    std::string output;
    std::string out_dir = std::string(mcpp::out_dir());
};

// What the member would submit, without submitting it: `generate()` is
// exactly `submit(plan_for())`, so a project-specific edit is an edit to the
// plan rather than a copy of this member.
struct plan {
    bool                     applies = false;
    std::string              reason;
    std::string              format;
    std::string              output;
    std::string              config;
    std::vector<std::string> argv;
    std::vector<std::string> inputs;
    explicit operator bool() const { return applies; }
};

// ─── Internals ─────────────────────────────────────────────────────────────

inline bool is_file(const std::string& p) {
    std::error_code ec;
    return !p.empty() && std::filesystem::is_regular_file(p, ec);
}

// Written only when the bytes differ, so an unchanged pack leaves the
// declared inputs' mtimes alone and the action stays up to date.
inline bool write_if_different(const std::filesystem::path& path, std::string_view bytes) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (std::ifstream in(path, std::ios::binary); in) {
        std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (old == bytes) return true;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

inline std::string text_or(const std::string& value, const char* fallback) {
    if (!value.empty()) return value;
    return (fallback && *fallback) ? std::string(fallback) : std::string();
}

inline std::string target_for(const options& opt) {
    return text_or(opt.target, mcpp::package_name());
}

// A Debian package name: lowercase letters, digits, `+`, `-`, `.`, starting
// with a letter or digit. RPM accepts every such name, so one rule serves both.
inline std::string package_name_for(const options& opt) {
    const std::string source = text_or(opt.name, mcpp::package_name());
    std::string name;
    for (char c : source) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) name += static_cast<char>(std::tolower(u));
        else if (c == '+' || c == '.' || c == '-') name += c;
        else name += '-';
    }
    while (!name.empty() && !std::isalnum(static_cast<unsigned char>(name.front()))) name.erase(0, 1);
    return name;
}

inline std::string maintainer_for(const options& opt) {
    if (!opt.maintainer.empty()) return opt.maintainer;
    if (const char* authors = mcpp::package_authors(); authors && *authors) {
        std::string first(authors);
        first = first.substr(0, first.find(';'));
        if (!first.empty()) return first;
    }
    return {};
}

// nfpm's architecture vocabulary is Go's; it renames for each format itself.
inline std::string nfpm_arch_for() {
    const std::string arch = mcpp::target_arch();
    if (arch == "x86_64") return "amd64";
    if (arch == "aarch64") return "arm64";
    return arch;
}

inline std::string file_arch_for(std::string_view format) {
    const std::string arch = mcpp::target_arch();
    if (format == "deb") return arch == "x86_64" ? "amd64" : arch == "aarch64" ? "arm64" : arch;
    return arch;
}

// A YAML double-quoted scalar.
inline std::string yaml_string(std::string_view value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '\\' || c == '"') out += '\\';
        if (c == '\n') { out += "\\n"; continue; }
        out += c;
    }
    return out + "\"";
}

// The time every file in the package carries: `SOURCE_DATE_EPOCH` when the
// build states one, otherwise a fixed time, so two packs of one tree agree.
inline std::string mtime_for() {
    long long seconds = 946684800;  // 2000-01-01T00:00:00Z
    if (const char* epoch = std::getenv("SOURCE_DATE_EPOCH"); epoch && *epoch) {
        long long parsed = 0;
        const std::string_view text(epoch);
        if (std::from_chars(text.data(), text.data() + text.size(), parsed).ec == std::errc{}) seconds = parsed;
    }
    const std::chrono::sys_seconds at{std::chrono::seconds{seconds}};
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", at);
}

inline const mcpp::plugins::tool::spec& nfpm_spec() {
    static const mcpp::plugins::tool::spec s{
        .who = "mcpp.dist.linux_package", .package = "nfpm",
        .programs = {"nfpm"}, .bin_dirs = {"", "bin"}, .option = "options::tool" };
    return s;
}

// The staged tree's launcher, which `/usr/bin/<name>` execs. `mcpp pack`
// names it after the program; under `--mode static` it is the program itself.
inline std::string launcher_in(const std::string& stage, const std::string& target) {
    for (auto candidate : {stage + "/" + target, stage + "/bin/" + target, stage + "/run.sh"})
        if (is_file(candidate)) return candidate;
    return {};
}

inline std::string desktop_entry(const options& opt, const std::string& name) {
    std::string categories;
    if (opt.categories.empty()) categories = "Utility;";
    else for (auto const& c : opt.categories) categories += c + ";";
    const std::string comment = text_or(opt.description, mcpp::package_description());
    std::string text = "[Desktop Entry]\nType=Application\n";
    text += std::format("Name={}\n", opt.app_name.empty() ? name : opt.app_name);
    if (!comment.empty()) text += std::format("Comment={}\n", comment);
    text += std::format("Exec={}\n", name);
    text += std::format("Icon={}\n", name);
    text += std::format("Categories={}\n", categories);
    text += std::format("Terminal={}\n", opt.terminal ? "true" : "false");
    return text;
}

inline std::string file_entry(const std::string& src, const std::string& dst, std::string_view mode,
                              const std::string& mtime) {
    return std::format("  - src: {}\n    dst: {}\n    file_info: {{ mode: {}, mtime: {} }}\n",
                       yaml_string(src), yaml_string(dst), mode, yaml_string(mtime));
}

// ─── Plan ──────────────────────────────────────────────────────────────────

inline plan plan_for(std::string_view format, options opt = {}) {
    plan p;
    p.format = std::string(format);
    const std::string who = std::format("mcpp.dist.{}", format);

    const std::string requested = mcpp::pack_format();
    if (requested != format) {
        p.reason = requested.empty() ? "this build is not packaging"
                                     : std::format("--format {} was requested, not {}", requested, format);
        return p;
    }
    if (const std::string os = mcpp::target_os(); os != "linux") {
        std::cerr << std::format("{}: a {} package installs on Linux, and this build targets '{}'.\n"
                                 "  use: --format tar, or build for a Linux target",
                                 who, format, os.empty() ? "unknown" : os) << '\n';
        p.reason = "not a Linux target";
        return p;
    }
    const std::string stage = mcpp::pack_stage_dir();
    if (stage.empty()) {
        std::cerr << who << ": mcpp reported no staged tree.\n";
        p.reason = "no staged tree";
        return p;
    }
    const std::string target = target_for(opt);
    const std::string name = package_name_for(opt);
    if (target.empty() || name.empty()) {
        std::cerr << who << ": no package name. Set `options::target` and `options::name`.\n";
        p.reason = "no name";
        return p;
    }
    const std::string version = text_or(opt.version, mcpp::package_version());
    if (version.empty() || !std::isdigit(static_cast<unsigned char>(version.front()))) {
        std::cerr << std::format("{}: the package version '{}' does not start with a digit, which both formats "
                                 "require. Set `[package] version` or `options::version`.", who, version) << '\n';
        p.reason = "version";
        return p;
    }
    const std::string maintainer = maintainer_for(opt);
    if (maintainer.empty()) {
        std::cerr << who << ": no maintainer. Set `[package] authors` or `options::maintainer`; a deb requires "
                            "the field and a guessed one would publish someone else's name.\n";
        p.reason = "no maintainer";
        return p;
    }

    const auto found = mcpp::plugins::tool::resolve(nfpm_spec(), opt.tool);
    if (found.program.empty()) {
        if (!found.pending()) std::cerr << mcpp::plugins::tool::describe_missing(nfpm_spec(), found) << '\n';
        p.reason = found.pending() ? "nfpm requested" : "nfpm not found";
        return p;
    }

    const std::string launcher = launcher_in(stage, target);
    if (launcher.empty()) {
        std::cerr << std::format("{0}: the staged tree at {1} carries no launcher for target '{2}'.\n"
                                 "  expected one of: {1}/{2}, {1}/bin/{2}, {1}/run.sh",
                                 who, stage, target) << '\n';
        p.reason = "no launcher in the staged tree";
        return p;
    }
    const std::string prefix = opt.prefix.empty() ? "/opt/" + name : opt.prefix;
    const std::string mtime = mtime_for();
    const auto work = std::filesystem::path(opt.out_dir) / "linux-package" / p.format;

    // The staged tree, one entry per file, in a stable order.
    std::vector<std::string> staged;
    std::error_code ec;
    for (auto const& entry : std::filesystem::recursive_directory_iterator(stage, ec)) {
        if (ec) break;
        if (entry.is_regular_file(ec) || entry.is_symlink(ec)) staged.push_back(entry.path().string());
    }
    std::ranges::sort(staged);
    if (staged.empty()) {
        std::cerr << std::format("{}: the staged tree at {} is empty", who, stage) << '\n';
        p.reason = "empty staged tree";
        return p;
    }

    std::string contents;
    for (auto const& file : staged) {
        const std::string relative = mcpp::plugins::names::relative_to(file, stage);
        const std::string destination = prefix + "/" + relative;
        if (std::filesystem::is_symlink(file, ec)) {
            contents += std::format("  - src: {}\n    dst: {}\n    type: symlink\n",
                                    yaml_string(std::filesystem::read_symlink(file, ec).string()), yaml_string(destination));
            continue;
        }
        const auto perms = std::filesystem::status(file, ec).permissions();
        const bool executable = (perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
        contents += file_entry(file, destination, executable ? "0755" : "0644", mtime);
    }

    const auto launcher_file = work / "launcher";
    const std::string launcher_rel = mcpp::plugins::names::relative_to(launcher, stage);
    bool wrote = write_if_different(launcher_file,
        std::format("#!/bin/sh\n# Generated by mcpp.dist.{}. Do not edit.\nexec {} \"$@\"\n",
                    format, yaml_string(prefix + "/" + launcher_rel)));
    contents += file_entry(launcher_file.string(), "/usr/bin/" + name, "0755", mtime);
    p.inputs.push_back(launcher_file.string());

    if (opt.desktop) {
        const auto desktop_file = work / (name + ".desktop");
        wrote = wrote && write_if_different(desktop_file, desktop_entry(opt, name));
        contents += file_entry(desktop_file.string(), "/usr/share/applications/" + name + ".desktop", "0644", mtime);
        p.inputs.push_back(desktop_file.string());
        if (!opt.icon.empty()) {
            std::string extension = std::filesystem::path(opt.icon).extension().string();
            for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (extension != ".png" && extension != ".svg") {
                const std::string message = std::format(
                    "{}: the icon {} is neither a .png nor an .svg, the two formats a desktop reads.", who, opt.icon);
                std::cerr << message << '\n';
                mcpp::warning(message.c_str());
                p.reason = "icon format";
                return p;
            }
            if (!is_file(opt.icon)) {
                std::cerr << std::format("{}: cannot read the icon {}", who, opt.icon) << '\n';
                p.reason = "icon unreadable";
                return p;
            }
            // An SVG scales; a PNG of unknown size belongs in the unsized location.
            const std::string destination = extension == ".svg"
                ? "/usr/share/icons/hicolor/scalable/apps/" + name + ".svg"
                : "/usr/share/pixmaps/" + name + ".png";
            contents += file_entry(std::filesystem::absolute(opt.icon, ec).string(), destination, "0644", mtime);
            p.inputs.push_back(opt.icon);
        }
    }

    std::string config = std::format(
        "# Generated by mcpp.dist.{}. Do not edit.\n"
        "name: {}\narch: {}\nplatform: linux\nversion: {}\nrelease: \"1\"\nmaintainer: {}\ndescription: {}\n",
        format, yaml_string(name), yaml_string(nfpm_arch_for()), yaml_string(version), yaml_string(maintainer),
        yaml_string(text_or(opt.description, mcpp::package_description())));
    if (const std::string license = text_or(opt.license, mcpp::package_license()); !license.empty())
        config += std::format("license: {}\n", yaml_string(license));
    if (const std::string homepage = text_or(opt.homepage, mcpp::package_repo()); !homepage.empty())
        config += std::format("homepage: {}\n", yaml_string(homepage));
    if (!opt.depends.empty()) {
        config += "depends:\n";
        for (auto const& dependency : opt.depends) config += std::format("  - {}\n", yaml_string(dependency));
    }
    config += std::format("mtime: {}\ncontents:\n", yaml_string(mtime)) + contents;

    const auto config_file = work / "nfpm.yaml";
    if (!(wrote && write_if_different(config_file, config))) {
        std::cerr << std::format("{}: cannot write the package description into {}", who, work.string()) << '\n';
        p.reason = "cannot write the package description";
        return p;
    }
    std::filesystem::permissions(launcher_file,
        std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add, ec);

    p.output = !opt.output.empty() ? opt.output
             : (std::filesystem::path(opt.out_dir) /
                (format == "deb" ? std::format("{}_{}_{}.deb", name, version, file_arch_for(format))
                                 : std::format("{}-{}-1.{}.rpm", name, version, file_arch_for(format)))).string();
    p.config = config_file.string();
    p.argv = {found.program, "package", "--config", p.config, "--packager", p.format, "--target", p.output};
    p.inputs.push_back(std::format("${{mcpp.target_file:{}}}", target));
    p.inputs.push_back(p.config);
    p.applies = true;
    return p;
}

// ─── Submit ────────────────────────────────────────────────────────────────

inline bool submit(const plan& p) {
    if (!p.applies) return true;
    mcpp::action a;
    const std::string id = "mcpp.dist." + p.format;
    std::string description = p.format;
    for (char& c : description) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    a.id          = id.c_str();
    a.role        = "artifact";
    a.description = description.c_str();
    for (auto const& token : p.argv) a.arg(token.c_str());
    for (auto const& input : p.inputs) a.input(input.c_str());
    a.output(p.output.c_str());
    a.submit();
    return true;
}

// Declare unconditionally, submit conditionally: the declaration is what lets
// `mcpp pack --format` name the formats a graph provides.
inline bool generate(std::string_view format, options opt = {}) {
    mcpp::provides_pack_format(std::string(format).c_str());
    return submit(plan_for(format, std::move(opt)));
}

} // namespace mcpp::dist::linux_package
