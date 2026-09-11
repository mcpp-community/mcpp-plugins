// mcpp.dist.wix -- an MSI from a linked program, and a definition this member
// renders around it.
//
// WHY THIS IS NEITHER A RULE NOR A TOOL. A rule states how a translation unit
// is compiled by a compiler mcpp does not drive. A tool states something the
// build program needs that no compiler performs, and does it while the
// program runs. This member does neither: it consumes a LINK OUTPUT and
// produces something a user installs. That is the third category `dist/
// appimage.cppm` establishes, and the prefix says which of the three
// questions a member answers -- a consumer reading `rules-wix` would expect a
// compiler and a translation unit, and there is neither.
//
// THE ENGINE HOLDS THE DISPATCH AND NOT THE FORMAT. `mcpp pack --format msi`
// finds the package that declared the name and hands it the same graph
// mechanism every other format uses; nothing about WiX's schema, its
// preprocessor, or its table model is in mcpp. Binding any of it there would
// couple an mcpp release to a release mcpp does not control -- WiX 4, 5 and 6
// are three such releases sharing one schema, and this member already has to
// track which one it is talking to without the engine's help.
//
// NAME THE INPUT; DO NOT HARVEST A DIRECTORY. This is the load-bearing
// decision in this file, and it is measured rather than argued. An earlier
// working implementation of this exact step bound a directory and harvested
// it -- `-bindpath Application=bin` -- and let WiX's own harvester decide what
// went in the `File` table. When that bind path resolved to nothing on
// Windows, wix produced a **valid, empty, 52 KB installer with no diagnostic
// at all**: no missing-file error, no empty-harvest warning, an exit code of
// zero. The rule this produces: a path that RESOLVES TO NOTHING is silent,
// and a NAMED INPUT that is missing is an error. `${mcpp.target_file:<name>}`
// is what gives this member the second shape instead of the first -- a build
// program is told neither the triple nor the fingerprint that produced the
// path, and the engine refuses an unknown target name rather than expanding
// it to an empty string -- so the program arrives as that placeholder and
// never as a directory this member goes looking through.
//
// THIS MEMBER NEVER READS THE STAGED TREE, AND THAT IS THE POINT OF ITS
// SHAPE. `mcpp pack --format msi` reports as the distributable whatever
// artifact action the REQUEST introduced, so a member that names one program
// and no directory needs nothing extra to be recognised. An earlier engine
// revision asked the narrower question -- which action named
// `${mcpp.stage_dir}` -- and refused this member for following section 6's
// guidance, which is why the engine's criterion is presence in the dispatch
// pass rather than a property of the member.
//
// THE PROGRAM'S PATH CROSSES TWO SUBSTITUTION PASSES, AND THAT IS WHY IT IS A
// WiX VARIABLE AND NOT LITERAL TEXT. `${mcpp.stage_dir}` and
// `${mcpp.target_file:<name>}` are interpolated by the ENGINE, and only
// inside an action's own argv -- not inside a file this build program happens
// to write to disk, which is why the `.wxs` below cannot simply spell the
// program's path out. So the path is carried in the argv, where the engine's
// substitution applies (`-d Executable=${mcpp.target_file:<target>}`), as a
// WiX preprocessor variable; the `.wxs` then references `$(Executable)`,
// which is WiX's OWN substitution, run later, when `wix build` itself
// executes. Two passes, chained: the engine resolves the argv token before
// the command runs, and wix resolves its own token while it runs.
//
// WHY PATH DISCOVERY IS ACCEPTABLE HERE AND IS NOT IN `dist/appimage.cppm`.
// appimagetool EMBEDS an asset it owns -- the type-2 runtime stub -- into the
// image it produces, so which build of the tool ran is part of the AppImage's
// own content, and `dist/appimage.cppm` declares that payload rather than
// trust whatever is on PATH. wix has no equivalent: an MSI's content is
// entirely decided by the `.wxs` and the files it names, and a newer or older
// wix compiling the same definition produces the same table rows. What varies
// between wix releases is the SCHEMA it accepts (v4/v5/v6 share the one this
// member targets) and its own diagnostics, neither of which the produced MSI
// carries away. So a host `wix` is a fine default here in a way a host
// `appimagetool` is not.
//
// WHY THERE IS NO PAYLOAD YET, AND WHY THAT IS A GAP RATHER THAN A DECISION.
//
// This member locates `wix` and refuses clearly when nothing is there, naming
// where it looked -- the `msvc@system` shape mcpp already has for the
// platform's own compiler. An earlier revision of this comment justified that
// by saying WiX is "not a redistributable archive this ecosystem can vendor",
// and THAT IS FALSE. Measured 2026-09-11:
//
//   https://api.nuget.org/v3-flatcontainer/wix/6.0.2/wix.6.0.2.nupkg
//     HTTP/2 200, application/octet-stream, 5851349 bytes
//
// anonymously fetchable and immutable, as every NuGet package is. And its own
// licence file, `OSMFEULA.txt` inside that package, says in as many words that
// redistribution is permitted:
//
//   2. "...this does not restrict the User from obtaining or redistributing
//       binaries from other sources or self-compiling them."
//   3. "The Fee is not a license fee. The Software's source code is licensed
//       to User under the OSI License and remains freely distributable..."
//
// The software is under the Microsoft Reciprocal License; the fee is a
// MAINTENANCE fee that applies to revenue-generating use, and section 4
// resolves any conflict in favour of the OSI licence. So `xim:wix` is
// legitimate, and this member should declare it the way `dist-appimage`
// declares `xim:appimagetool` -- with no PATH fallback at all, so the produced
// installer depends on a declaration rather than on a machine.
//
// TWO THINGS ARE NEEDED FOR THAT AND NEITHER IS WRITTEN YET: the package, and
// a `xim:dotnet` dependency, because the payload is `tools/net6.0/any/wix.dll`
// and not a self-contained executable -- it is invoked as `dotnet wix.dll`.
// `pkgs/d/dotnet.lua` already exists in the index, so the dependency edge is
// available. Until the package lands, the lookup below is what there is, and
// the paragraph above records that it is a gap and not the answer.

module;
#include <cstdio>

export module mcpp.dist.wix;

import std;
import mcpp;
import mcpp.plugins;

// Nothing here uses `std::println`, and that is not a style choice: both of
// its overloads reach into the libc++ dylib for symbols macOS 14 does not
// ship, so a member that printed with it compiled and then failed to link.
// `std::format` is header-only. The full measurement is in `rules/spirv.cppm`.
// This member never runs on macOS, but it is compiled as a host module
// everywhere `mcpp-plugins` is built, `tests/all-rules-compile` included, so
// the same rule applies to it.

export namespace mcpp::dist::wix {

// ─── Options ───────────────────────────────────────────────────────────────

struct options {
    // The program target this MSI wraps. Empty means the package name, which
    // is the target `mcpp pack` itself selects by convention.
    std::string target;

    // `Package/@Name`. Empty means the target name, then the package name,
    // then "app" -- the same fallback chain `dist/appimage.cppm` uses for its
    // own app name, so a project that names nothing still gets one answer
    // rather than an MSI with a blank product name.
    std::string product_name;

    // `Package/@Manufacturer`. Empty means the first of `package_authors()`
    // (';'-separated), then `package_namespace()`, then a literal fallback --
    // see `manufacturer_for` for why a final fallback exists at all: WiX
    // requires this attribute to be non-empty.
    std::string manufacturer;

    // `Package/@Version`, in the project's OWN numbering, used as given with
    // no conversion. Set this when `[package] version` is not already an MSI
    // version WiX will accept (see `msi_version_from` for what mcpp's own
    // four-segment date version needs done to it, which this member does
    // automatically when this option is left empty).
    std::string version;

    // `Package/@UpgradeCode`, a literal `{GUID}`. Empty derives one
    // deterministically from `package_namespace()` and `package_name()`; see
    // `upgrade_code_for` for why determinism is the requirement, not merely a
    // convenience.
    std::string upgrade_code;

    // A project-supplied `.wxs` that wins over the one this member generates.
    // It must reference `$(Executable)` the same way the generated
    // definition does if it wants the built program named at all -- this
    // member always passes that one variable through `-d`, generated
    // definition or not, because that is the one value this build program
    // cannot bake in as literal text (see the header comment).
    //
    // `$(Executable)`, NOT `$(var.Executable)`, AND THE DIFFERENCE IS A
    // MEASUREMENT RATHER THAN A READING. WiX's own documentation spells a
    // `-d` define as `$(var.Name)`, which is v3's form, and that is what this
    // member first generated. A working implementation that is green on
    // Windows CI uses the bare form -- HuxerUI's
    // `mcpp/huxerui-build-rules/wix/Package.wxs.in`, `<File
    // Source="$(Executable)" ...>`, against the same `-d Executable=` argv
    // this member builds. Neither spelling could be run here, so the one with
    // a measurement behind it is the one shipped.
    std::string wxs;

    // An explicit `wix` wins over discovery. Set it to pin a build other than
    // the one on PATH.
    std::string tool;

    // Where the produced file lands. Empty means
    // `<out_dir>/<product_name>-<arch>.msi`.
    std::string output;
    std::string out_dir = std::string(mcpp::out_dir());
};

// ─── The plan ──────────────────────────────────────────────────────────────

// What this member would submit, without submitting it.
//
// A PLAN AND A SUBMIT, BECAUSE A DISTRIBUTABLE IS THE LAST THING BEFORE A
// USER'S HANDS. It is therefore the part of a build most likely to need a
// project-specific edit -- a second file in the `.wxs`, a UI sequence, a
// launch condition -- and `generate()` being exactly `submit(plan_for())` is
// what keeps such an edit from becoming a reimplementation of this member.
struct plan {
    // False when this build is not `mcpp pack --format msi`, which is every
    // ordinary build. `reason` then says which of the several ways.
    bool                     applies = false;
    std::string              reason;
    std::string              output;      // the .msi path
    std::string              wxs_path;    // generated or project-supplied
    std::string              target_name; // for the opportunistic size probe below
    std::vector<std::string> argv;
    std::vector<std::string> inputs;
    explicit operator bool() const { return applies; }
};

// ─── Internals ─────────────────────────────────────────────────────────────

inline bool is_file(const std::string& p) {
    std::error_code ec;
    return !p.empty() && std::filesystem::is_regular_file(p, ec);
}

// Written only when the bytes differ, for the reason `dist/appimage.cppm`
// gives: rewriting identical bytes moves the mtime, and a moved mtime on a
// declared input is indistinguishable from a changed one -- so a second pack
// of an unchanged project would rebuild the MSI.
inline bool write_if_different(const std::filesystem::path& path,
                               std::string_view bytes) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (std::ifstream in(path, std::ios::binary); in) {
        std::string old((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
        if (old == bytes) return true;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

inline std::string target_for(const options& opt) {
    if (!opt.target.empty()) return opt.target;
    const char* n = mcpp::package_name();
    return (n && *n) ? std::string(n) : std::string();
}

inline std::string product_name_for(const options& opt) {
    if (!opt.product_name.empty()) return opt.product_name;
    if (!opt.target.empty())       return opt.target;
    const char* n = mcpp::package_name();
    return (n && *n) ? std::string(n) : std::string("app");
}

inline std::string first_before(std::string_view s, char sep) {
    auto pos = s.find(sep);
    return std::string(pos == std::string_view::npos ? s : s.substr(0, pos));
}

// A FINAL FALLBACK THAT DOES NOT EXIST FOR appimage's DESKTOP `Comment=`.
//
// An AppImage's desktop entry tolerates an empty `Comment=`; WiX does not
// tolerate an empty `Manufacturer`, so this chain ends in a literal rather
// than in an empty string the way `dist/appimage.cppm`'s optional fields do.
inline std::string manufacturer_for(const options& opt) {
    if (!opt.manufacturer.empty()) return opt.manufacturer;
    if (const char* a = mcpp::package_authors(); a && *a) {
        if (auto first = first_before(a, ';'); !first.empty()) return first;
    }
    if (const char* ns = mcpp::package_namespace(); ns && *ns) return ns;
    return "Unknown";
}

// WiX's own architecture vocabulary, from the target mcpp resolved. Refused
// for anything else rather than guessed: an MSI built for the wrong
// architecture installs and then fails to run, with nothing in this build's
// own log pointing back at the guess that produced it.
inline std::string wix_arch_for(const std::string& arch) {
    if (arch == "x86_64")  return "x64";
    if (arch == "aarch64") return "arm64";
    return {};
}

// Splits "a.b.c.d" into unsigned fields, or an empty vector for anything that
// is not purely digits separated by single dots -- including a leading,
// trailing or doubled dot. A caller that gets an empty vector refuses by
// NAMING the string, rather than this function throwing through a build
// program the engine would then have to diagnose generically.
inline std::vector<unsigned long> numeric_fields(std::string_view s) {
    std::vector<unsigned long> out;
    if (s.empty()) return out;
    std::size_t i = 0;
    while (i <= s.size()) {
        auto dot  = s.find('.', i);
        auto part = s.substr(i, dot == std::string_view::npos ? s.size() - i : dot - i);
        if (part.empty()) return {};
        unsigned long v = 0;
        for (char c : part) {
            if (c < '0' || c > '9') return {};
            v = v * 10 + static_cast<unsigned long>(c - '0');
        }
        out.push_back(v);
        if (dot == std::string_view::npos) break;
        i = dot + 1;
    }
    return out;
}

struct msi_version { std::string text; bool lossy = false; bool ok = false; };

// MSI PACKS ProductVersion INTO Major.Minor.Build WITH HARD PER-FIELD
// CEILINGS -- 255, 255, 65535 -- because Windows Installer stores the three
// fields in a single 32-bit integer (8+8+16 bits). WiX enforces the same
// ceiling at compile time, error CNDL0242 ("Invalid product version"). That
// is documented Windows Installer and WiX behaviour, not something measured
// on this host: wix does not run on Linux, so nothing here has produced or
// rejected a real MSI. A raw calendar year overflows the first field by a
// factor of eight, so mcpp's own `YYYY.M.D.N` convention cannot be copied
// into Major.Minor.Build positionally -- and Windows Installer's comparison
// ignores whatever the fourth field holds regardless, so the release counter
// cannot simply ride along as a fourth field either.
//
// The fields are therefore RE-PACKED rather than truncated:
//
//   Major = year modulo 100        -- wraps every century; always under 255
//   Minor = month                  -- 1..12 for mcpp's own scheme, clamped
//   Build = day * 1000 + counter   -- keeps SAME-DAY releases ordered
//
// This repository has itself shipped 2026.9.10.1 and 2026.9.10.2 -- two
// releases on one calendar day -- so folding the day and the release counter
// into the one field Windows Installer actually compares is not a
// hypothetical: a naive Major.Minor.Build = Year.Month.Day mapping would have
// made those two collide as "the same version", and an upgrade would need
// `AllowSameVersionUpgrades` just to be recognised as one. What this loses:
// the day and the release counter are no longer independently readable from
// the MSI version the way they are from `package_version()`, and a project
// shipping past the year 2125 loses century information this scheme wraps at
// -- 2026 and 2126 both become Major 26.
//
// A version that is not already four numeric dotted fields is assumed to
// already be MSI-shaped -- an ordinary project version such as "1.2.3" -- and
// is passed through with only the same per-field ceiling applied, because
// nothing here knows it is a date, and re-packing it would invent structure
// that is not there.
inline msi_version msi_version_from(std::string_view v) {
    msi_version out;
    auto f = numeric_fields(v);
    if (f.empty()) return out;
    out.ok = true;
    if (f.size() >= 4) {
        unsigned long major = f[0] % 100;
        unsigned long minor = f[1] > 255 ? 255 : f[1];
        unsigned long build = f[2] * 1000 + f[3];
        if (build > 65535) build = 65535;
        out.text  = std::format("{}.{}.{}", major, minor, build);
        out.lossy = true;
    } else {
        static constexpr unsigned long kCaps[3] = {255, 255, 65535};
        bool clamped = false;
        std::string joined;
        for (std::size_t i = 0; i < f.size(); ++i) {
            unsigned long v2 = f[i];
            if (v2 > kCaps[i]) { v2 = kCaps[i]; clamped = true; }
            if (i) joined += '.';
            joined += std::to_string(v2);
        }
        out.text  = joined;
        out.lossy = clamped;
    }
    return out;
}

// FNV-1a, run twice with different seeds to produce 128 bits. Chosen over
// `std::hash<std::string>` because `std::hash` is not specified to produce
// the same digest across standard library implementations, versions, or even
// process runs -- and the one property an UpgradeCode needs for the whole
// life of a product is to be the SAME value every time this identity string
// is hashed, on whatever machine and whatever toolchain builds it. A GUID
// that silently changed because a CI runner's C++ runtime was upgraded would
// present Windows Installer with what looks like an unrelated product, with
// nothing in this build's log pointing at the cause -- exactly the failure
// category the rest of this file exists to refuse.
inline std::uint64_t fnv1a64(std::string_view s, std::uint64_t seed) {
    std::uint64_t h = seed;
    for (unsigned char c : s) { h ^= c; h *= 0x100000001b3ULL; }
    return h;
}

// A hash formatted as a GUID, not an RFC 4122 version-5 UUID -- that
// specifies SHA-1 over a namespace and a name, and nothing here claims that
// stronger guarantee. What is claimed is narrower and is all this needs: the
// same identity string always produces the same 16 bytes. The version and
// variant nibbles below are set anyway, for the benefit of any tool that
// validates a GUID's structure rather than only its punctuation; wix itself
// only parses the punctuation.
inline std::string upgrade_code_for(const std::string& identity) {
    const std::uint64_t hi = fnv1a64(identity, 0x9e3779b97f4a7c15ULL);
    const std::uint64_t lo = fnv1a64(identity, 0xcbf29ce484222325ULL);
    unsigned char b[16];
    for (int i = 0; i < 8; ++i) b[i]     = static_cast<unsigned char>(hi >> (56 - 8 * i));
    for (int i = 0; i < 8; ++i) b[8 + i] = static_cast<unsigned char>(lo >> (56 - 8 * i));
    b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x50); // version nibble
    b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80); // RFC 4122 variant
    static const char* kHex = "0123456789ABCDEF";
    std::string out = "{";
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out += '-';
        out += kHex[b[i] >> 4];
        out += kHex[b[i] & 0x0F];
    }
    out += "}";
    return out;
}

inline std::string xml_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            default:   out += c;
        }
    }
    return out;
}

// Discovery, in the order `rules/spirv.cppm` establishes: what the project
// named, what the environment named, then PATH. There is no payload tier
// between them -- see the header comment for why a host `wix` is an
// acceptable default here in a way a host shader compiler or a host
// `appimagetool` is not.
inline std::string discover_tool(const options& opt) {
    if (!opt.tool.empty()) return opt.tool;
    if (const char* e = std::getenv("MCPP_WIX"); e && *e) return e;
#if defined(_WIN32)
    const char* exe = "wix.exe";
#else
    // wix is a .NET tool and therefore Windows-only in practice; this branch
    // exists only so the search compiles and returns nothing on every other
    // host, which is what `tests/all-rules-compile` exercises.
    const char* exe = "wix";
#endif
    const char* path = std::getenv("PATH");
    if (!path || !*path) return {};
#if defined(_WIN32)
    constexpr char kSep = ';';
#else
    constexpr char kSep = ':';
#endif
    std::string_view sv(path);
    for (std::size_t i = 0; i <= sv.size();) {
        auto sep = sv.find(kSep, i);
        auto dir = sv.substr(i, sep == std::string_view::npos ? sv.size() - i : sep - i);
        i = sep == std::string_view::npos ? sv.size() + 1 : sep + 1;
        if (dir.empty()) continue;
        auto candidate = (std::filesystem::path(dir) / exe).string();
        if (is_file(candidate)) return candidate;
    }
    return {};
}

// A minimal WiX v4/v5/v6 definition: one `Package`, one `Component` carrying
// the single file this member wraps, one `Feature` referencing it. WiX can
// derive a stable Component GUID from the component's own target path by
// itself when `Component/@Guid` is omitted -- that is why only one GUID is
// minted here rather than two: `UpgradeCode` expresses identity across the
// WHOLE product, across every rebuild, which is ecosystem knowledge
// (`package_namespace()` plus `package_name()`) the compiler has no way to
// see on its own.
//
// `Codepage="65001"` (UTF-8) is stated explicitly because `product_name` and
// `manufacturer` come from project metadata that may not be ASCII, and this
// member has no reason to assume WiX's default matches.
inline std::string wxs_document(const std::string& name, const std::string& manufacturer,
                                const std::string& version, const std::string& upgrade_code) {
    return std::format(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<Wix xmlns=\"http://wixtoolset.org/schemas/v4/wxs\">\n"
        "  <!-- Generated by mcpp.dist.wix. A project that needs a different\n"
        "       definition supplies its own through options::wxs; see that\n"
        "       option's comment for the one variable such a file must still\n"
        "       reference. -->\n"
        "  <Package Codepage=\"65001\" Name=\"{0}\" Manufacturer=\"{1}\" "
        "Version=\"{2}\" UpgradeCode=\"{3}\">\n"
        "    <MajorUpgrade DowngradeErrorMessage=\"A newer version of "
        "[ProductName] is already installed.\" />\n"
        "    <StandardDirectory Id=\"ProgramFiles6432Folder\">\n"
        "      <Directory Id=\"INSTALLFOLDER\" Name=\"{0}\">\n"
        "        <Component Id=\"MainExecutable\">\n"
        "          <File Id=\"MainExecutable\" Source=\"$(Executable)\" "
        "KeyPath=\"yes\" />\n"
        "        </Component>\n"
        "      </Directory>\n"
        "    </StandardDirectory>\n"
        "    <Feature Id=\"MainFeature\" Title=\"{0}\" Level=\"1\">\n"
        "      <ComponentRef Id=\"MainExecutable\" />\n"
        "    </Feature>\n"
        "  </Package>\n"
        "</Wix>\n",
        xml_escape(name), xml_escape(manufacturer), xml_escape(version), upgrade_code);
}

// ─── Plan ──────────────────────────────────────────────────────────────────

inline plan plan_for(options opt = {}) {
    plan p;

    // NOT THIS PASS. Every ordinary build lands here, and the empty
    // `pack_format()` is what says so -- see `generate` for why the
    // DECLARATION must not be gated the same way.
    const std::string requested = mcpp::pack_format();
    if (requested != "msi") {
        p.reason = requested.empty()
            ? "this build is not packaging"
            : std::format("--format {} was requested, not msi", requested);
        return p;
    }

    // Windows only, and this is a refusal rather than a silent skip: a user
    // who typed `--format msi` on Linux asked for something that does not
    // exist there, and the engine has already accepted the value because the
    // graph declared it.
    if (const std::string os = mcpp::target_os(); os != "windows") {
        std::cerr << std::format(
            "mcpp.dist.wix: an MSI is a Windows format, and this build targets "
            "'{}'.\n"
            "  use: --format tar, or build for a Windows target",
            os.empty() ? "unknown" : os) << '\n';
        p.reason = "not a Windows target";
        return p;
    }

    const std::string target = target_for(opt);
    if (target.empty()) {
        std::cerr << "mcpp.dist.wix: no target to package. Set "
                     "`options::target` to the program target's name.\n";
        p.reason = "no target";
        return p;
    }

    const std::string tool = discover_tool(opt);
    if (tool.empty()) {
        std::cerr << std::format(
            "mcpp.dist.wix: the wix CLI was not found.\n"
            "  looked for: options::tool, then $MCPP_WIX, then `wix` on PATH.\n"
            "  WiX is a .NET tool this ecosystem does not redistribute; "
            "install it with:\n"
            "    dotnet tool install --global wix\n"
            "  or set `options::tool` to name one explicitly.") << '\n';
        p.reason = "wix not found";
        return p;
    }

    const std::string hostArch = mcpp::target_arch();
    const std::string arch     = wix_arch_for(hostArch);
    if (arch.empty()) {
        std::cerr << std::format(
            "mcpp.dist.wix: WiX has no architecture spelling this member "
            "knows for '{}'. Known: x86_64 -> x64, aarch64 -> arm64.",
            hostArch.empty() ? "unknown" : hostArch) << '\n';
        p.reason = "unknown architecture";
        return p;
    }

    const std::string name = product_name_for(opt);
    std::string wxsPath;
    if (!opt.wxs.empty()) {
        if (!is_file(opt.wxs)) {
            std::cerr << std::format(
                "mcpp.dist.wix: the definition file {} was not found", opt.wxs) << '\n';
            p.reason = "definition file not found";
            return p;
        }
        wxsPath = opt.wxs;
    } else {
        const char* pv = mcpp::package_version();
        const std::string rawVersion = !opt.version.empty() ? opt.version
                                      : (pv && *pv ? std::string(pv) : std::string());
        if (rawVersion.empty()) {
            std::cerr << "mcpp.dist.wix: no version to state. Set "
                         "`[package] version` or `options::version`.\n";
            p.reason = "no version";
            return p;
        }
        const auto mv = msi_version_from(rawVersion);
        if (!mv.ok) {
            std::cerr << std::format(
                "mcpp.dist.wix: '{}' is not a purely numeric, dot-separated "
                "version, which is what an MSI's Version attribute requires.",
                rawVersion) << '\n';
            p.reason = "version not numeric";
            return p;
        }
        const std::string manufacturer = manufacturer_for(opt);
        const char* nsC = mcpp::package_namespace();
        const char* nmC = mcpp::package_name();
        const std::string identity = (nsC && *nsC ? std::string(nsC) : std::string())
                                    + "/" + (nmC && *nmC ? std::string(nmC) : std::string());
        const std::string upgradeCode = !opt.upgrade_code.empty() ? opt.upgrade_code
                                       : upgrade_code_for(identity);
        wxsPath = (std::filesystem::path(opt.out_dir) / (name + ".wxs")).string();
        if (!write_if_different(wxsPath, wxs_document(name, manufacturer, mv.text, upgradeCode))) {
            std::cerr << std::format("mcpp.dist.wix: cannot write {}", wxsPath) << '\n';
            p.reason = "cannot write definition";
            return p;
        }
    }

    p.output = !opt.output.empty() ? opt.output
             : (std::filesystem::path(opt.out_dir)
                / std::format("{}-{}.msi", name, arch)).string();
    p.wxs_path     = wxsPath;
    p.target_name  = target;

    const std::string targetFile = std::format("${{mcpp.target_file:{}}}", target);
    p.argv = {
        tool, "build",
        "-arch", arch,
        // The one value this build program cannot bake in as literal text --
        // see the header comment for why it crosses two substitution passes
        // instead.
        "-d", "Executable=" + targetFile,
        "-o", p.output,
        wxsPath,
    };
    // TWO INPUTS, AND NEITHER IS THE STAGED TREE. The program the MSI carries
    // and the definition that describes it are the whole of what this action
    // reads, so editing the `.wxs` rebuilds the MSI and a dependency's shared
    // library -- which the MSI's one `File` row never names -- does not.
    p.inputs = { targetFile, wxsPath };
    p.applies = true;
    return p;
}

// ─── Submit ────────────────────────────────────────────────────────────────

inline bool submit(const plan& p) {
    if (!p.applies) return true;
    mcpp::action a;
    a.id          = "mcpp.dist.wix";
    a.role        = "artifact";
    a.description = "MSI";
    for (auto const& tok : p.argv)   a.arg(tok.c_str());
    for (auto const& in  : p.inputs) a.input(in.c_str());
    a.output(p.output.c_str());
    a.submit();

    // A FLOOR ON THIS MEMBER'S OWN OUTPUT, ON THE SUCCESS PATH -- IN TWO
    // HALVES, BECAUSE ONE THING IS ALWAYS MEASURABLE AND THE OTHER IS NOT.
    //
    // The MSI itself does not exist when this program runs -- wix has not
    // been invoked yet, only declared -- so nothing here can open it. What
    // this member CAN see: the definition it just wrote or was handed, and,
    // opportunistically, a copy of the program inside whatever `mcpp pack`
    // staged. Both are best-effort in different ways: the first is exact but
    // narrow (it catches an empty definition, not a broken one); the second
    // depends on a staging convention this member does not otherwise rely on
    // -- see `plan_for`, which never reads the staged tree -- so it is
    // skipped rather than refused when nothing recognisable turns up there.
    // Neither half can catch a wix invocation that fails, a signature that
    // does not verify, or an identity the machine does not have -- those run
    // after this program has already exited.
    std::string text;
    if (std::ifstream in(p.wxs_path, std::ios::binary); in)
        text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.find("<File") == std::string::npos) {
        static char msg[512];
        std::snprintf(msg, sizeof msg,
            "mcpp.dist.wix: the definition at %s declares no <File> element; "
            "the MSI wix builds from it will install nothing",
            p.wxs_path.c_str());
        mcpp::warning(msg);
    }

    if (const std::string stage = mcpp::pack_stage_dir(); !stage.empty()) {
        std::string found;
        for (auto candidate : { stage + "/" + p.target_name,
                                stage + "/bin/" + p.target_name,
                                stage + "/" + p.target_name + ".exe",
                                stage + "/bin/" + p.target_name + ".exe" })
            if (is_file(candidate)) { found = candidate; break; }
        if (!found.empty()) {
            std::error_code ec;
            std::uintmax_t bytes = std::filesystem::file_size(found, ec);
            // A SIZE BOUND IS DEFENSIBLE HERE AND WAS NOT IN
            // `dist/appimage.cppm`, which is worth stating because the two
            // look alike. That member measured a whole staged TREE and 16 KB
            // was wrong on its first fixture -- a stripped hello-world stages
            // at 14999 bytes -- so it counts files instead. This measures ONE
            // LINKED PROGRAM, and no linked program is under a kilobyte on any
            // platform this member serves. It exists to catch "nothing was
            // linked", not to police a size budget.
            if (!ec && bytes < 1024u) {
                static char msg2[512];
                std::snprintf(msg2, sizeof msg2,
                    "mcpp.dist.wix: %s is %llu bytes, which is implausibly "
                    "small for a linked program; the MSI may be carrying a "
                    "stub",
                    found.c_str(), static_cast<unsigned long long>(bytes));
                mcpp::warning(msg2);
            }
        }
    }
    return true;
}

// ─── The one call a consumer makes ─────────────────────────────────────────

// DECLARE UNCONDITIONALLY, SUBMIT CONDITIONALLY -- and both halves are here so
// a consumer cannot do one without the other.
//
// The declaration is what lets the engine answer a question the requesting
// build cannot: `mcpp pack --format bogus` names what is available, and
// `--help` says "any format the resolved graph provides". Both read the set
// collected from a pass that asked for nothing. A member that declared only
// when asked still works for its author -- they always pass their own
// format -- and makes the set unknowable for everyone else.
inline bool generate(options opt = {}) {
    mcpp::provides_pack_format("msi");
    return submit(plan_for(std::move(opt)));
}

} // namespace mcpp::dist::wix
