// mcpp.plugins: the collection's shared library.
//
// Every member of this package is a module interface unit under rules/ or
// tools/, compiled as a host module of its own when the consumer's feature
// request names it. This unit is the lib root: it is compiled before every
// member, so a member may import it.
//
// IT HOLDS WHAT MORE THAN ONE MEMBER NEEDS, AND ONLY THAT.
//
// The lib root is the only unit every member can import. A second unit beside
// it in `[build] sources` is NOT compiled as a host module ahead of the
// members. Measured: a member importing a second lib-root unit failed with
//
//   mcpp.plugins.surface: error: failed to read compiled module
//   note: imports must be built before being imported
//
// because only the lib root is built first. So shared code lives here rather
// than in a file of its own, and a member that needs it writes
// `import mcpp.plugins;`.
module;
#include <cctype>

export module mcpp.plugins;

import std;
import mcpp;

export namespace mcpp::plugins {

// THIS MUST EQUAL `[package] version` IN mcpp.toml, AND CI CHECKS THAT IT DOES.
//
// It was `0.1.1` while the package was `0.2.6`, which nothing noticed because
// nothing read it. A value that is recorded and never read cannot be wrong in a
// way anyone sees, so the fix is not only to correct it: every rule now states
// it with `mcpp::fact`, which makes a build log answer "which collection
// produced these actions" and makes a stale constant a visible defect rather
// than a dormant one.
//
// One package, one version: the number lives in mcpp.toml, and the CI step
// `the collection states its own version` compares the two.
inline constexpr std::string_view version = "0.3.0";

} // namespace mcpp::plugins

// mcpp.plugins.surface -- the interface a consumer names for an embedded payload.
//
// WHY THIS IS SHARED RATHER THAN PER-RULE.
//
// `mcpp.rules.spirv` and `mcpp.rules.slang` produce the same kind of thing: a
// block of bytes the program hands to a device API. `mcpp.tools.embed` produces
// it from a file that was already there. The three differ in who produces the
// bytes and disagree about nothing else, so the declaration a consumer reads is
// written once, here, and the shape is identical whichever produced it. A rule
// that wrote its own would be a second copy of a decision, and the two would
// drift the way the two shader compilers' headers drifted before 0.2.6.
//
// THE INTERFACE IS A FUNCTION, NOT A VARIABLE.
//
// A variable cannot keep one shape across the ways bytes can be stored, because
// `constexpr` and `extern` are mutually exclusive: an array compiled into a
// translation unit can be constant-evaluated and one living in a section cannot.
// A function can, so where the bytes live stays an option a project revises
// without touching a consumer.
//
// THE INTERFACE NAMES NO STANDARD-LIBRARY TYPE, AND THAT IS MEASURED.
//
// The same 1 MB payload, reached four ways, compiled with GCC 16.1:
//
//   data in the module (`export inline constexpr`)      BMI 6 282 240 B
//   data in a header                                    source 3 145 728 B
//   data in an object, interface returns std::span      BMI 1 313 968 B
//   data in an object, interface returns a std-free POD  BMI     1 808 B
//
// The third row is 727 times the fourth, and its cost is FIXED rather than
// proportional to the payload: the 1.28 MB is `<span>`'s templates, present
// whether the payload is 16 KB or 16 MB. A consumer that wants a `std::span`
// constructs one from the two members, and `<span>` is then included by the
// consumer that uses it rather than by every consumer that imports this.
//
// There is a second, independent reason for the same decision. `import std;`
// requires `std.gcm` to have been built, which a project with
// `modules = true, import_std = false` has not done. A std-free interface needs
// neither.
//
// WHY THE ACCESSORS HAVE C LANGUAGE LINKAGE.
//
// A function declared in a module interface has module linkage and can only be
// defined by a unit attached to that module. Defining them would therefore need
// a module implementation unit, and the definitions include the generated data
// headers -- which would put the arrays back into the interface's own
// compilation. Declaring the accessors `extern "C"` instead gives them external
// linkage, so an ordinary translation unit defines them, the module interface
// holds two declarations and one inline call per payload, and the bytes are
// never seen by the interface at all.
//
// It also removes a portability question: module implementation units are the
// least exercised corner of every implementation this package supports, and
// nothing here needs them.
//
// ONE COPY OF THE BYTES.
//
// A generated data header declares `static const uint32_t <sym>[]`, so every
// translation unit that includes it gets its own copy. Under this surface
// exactly one translation unit includes it -- the generated implementation --
// and every consumer reaches the same array through the accessor.
export namespace mcpp::plugins::surface {

// How a consumer names the payloads.
//
// `module_` is the default where the project builds C++ modules, and
// `c_header` is what a project without them gets. The choice does not change
// any declaration's shape: the same struct, the same function names, the same
// namespaces. Only the file a consumer reaches them through differs.
enum class kind { module_, c_header };

// The element the accessor hands back. `word32` is what an API taking
// `const uint32_t*` wants -- `vkCreateShaderModule` is the case this package
// has -- and asking for it here is cheaper than a reinterpret_cast at every
// call site, which is undefined behaviour on an under-aligned byte array.
enum class element { byte_, word32 };

// One payload in a group.
struct item {
    // The C++ identifier, already sanitised by the caller: `blur_comp`.
    std::string identifier;
    // Namespace segments BELOW the group's own, from the payload's directory
    // relative to its root: `{"post"}` for `shaders/post/tonemap.frag`. This is
    // what keeps two payloads with one stem from colliding, and what makes the
    // name a consumer writes say where the payload came from.
    std::vector<std::string> name_space;
    // The generated data header this payload's bytes arrive in, AS AN
    // `#include` WRITES IT -- relative to the directory the caller put on the
    // include path, not an absolute path and not a bare file name. The
    // generated tree mirrors the payload tree, so two payloads sharing a stem
    // in different directories reach different headers, and a bare file name
    // could not tell them apart.
    std::string data_header;
    // The array that header declares, qualified if it sits in a namespace.
    std::string data_symbol;
    // The payload's size IN BYTES, as an expression valid where the generated
    // implementation writes it. Empty means `sizeof <data_symbol>`.
    //
    // IT EXISTS BECAUSE `sizeof` IS NOT ALWAYS THE ANSWER. `mcpp.tools.embed`
    // can append a terminating zero byte that its own `_size` constant does not
    // count, so `sizeof` and `_size` differ by one and only one of them is what
    // the payload is. Measured: a group of null-terminated text payloads
    // reported one byte too many and the fixture's comparison failed on a
    // trailing NUL. A caller that knows the difference states it here rather
    // than letting the generator guess.
    std::string data_size_expr;
};

struct options {
    kind        surface = kind::module_;
    element     elem    = element::word32;
    // The module a consumer imports, e.g. `myapp.shaders`. The namespace is
    // this name with `.` replaced by `::`, which is why there is no second
    // field for it: two spellings of one identity drift.
    std::string module_name;
    // Where the generated interface and implementation are written. The caller
    // adds this to the include path when the surface is `c_header`.
    std::string out_dir;
    // A short phrase naming what produced the payloads, for the generated
    // files' first line: "mcpp.rules.spirv".
    std::string produced_by;
};

// What the caller hands back to mcpp.
struct emitted {
    // The interface unit: a `.cppm` under `module_`, a `.h` under `c_header`.
    std::string interface_file;
    // The translation unit defining the accessors. Always a plain `.cpp`.
    std::string impl_file;
    // Non-empty under `module_`: what the interface unit provides, which the
    // caller declares with `mcpp::action::provides` or lets the scan find.
    std::string module_name;
    // Non-empty under `c_header`: the directory to put on the include path.
    std::string include_dir;
};

// ---- internals -------------------------------------------------------------

inline std::vector<std::string> split_module_name(std::string_view name) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i <= name.size();) {
        auto dot = name.find('.', i);
        auto one = dot == std::string_view::npos ? name.substr(i) : name.substr(i, dot - i);
        if (!one.empty()) out.emplace_back(one);
        if (dot == std::string_view::npos) break;
        i = dot + 1;
    }
    return out;
}

// The linker symbol an accessor carries. It is derived from the module name and
// the payload's full namespace path rather than from the identifier alone,
// because two groups in one link unit -- a project with shaders and with
// embedded data -- would otherwise define the same symbol twice and the second
// definition would be the one nobody expected.
inline std::string accessor_base(const options& opt, const item& it) {
    std::string s = "mcpp_embed";
    auto add = [&](std::string_view part) {
        s += '_';
        for (char c : part)
            s += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    };
    for (auto const& seg : split_module_name(opt.module_name)) add(seg);
    for (auto const& seg : it.name_space) add(seg);
    add(it.identifier);
    return s;
}

inline const char* element_type(element e) {
    return e == element::word32 ? "unsigned int" : "unsigned char";
}

// `unsigned int` is 32 bits on every target this package supports, and the
// generated implementation asserts it rather than assuming it: a target where
// it is not would otherwise hand a Vulkan driver a pointer into misread data,
// and the failure would be a device error naming nothing.
inline const char* element_width_assertion(element e) {
    return e == element::word32
        ? "static_assert(sizeof(unsigned int) == 4,\n"
          "    \"mcpp.plugins.surface: element::word32 assumes a 32-bit `unsigned int`\");\n"
        : "";
}

inline bool write_if_different(const std::string& path, std::string_view text) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (std::ifstream in(path, std::ios::binary); in) {
        std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (old == text) return true;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

// Open and close the namespaces an item sits in, below the group's own.
inline std::string open_namespaces(const std::vector<std::string>& segs) {
    std::string s;
    for (auto const& one : segs) s += "namespace " + one + " {\n";
    return s;
}
inline std::string close_namespaces(const std::vector<std::string>& segs) {
    std::string s;
    for (auto i = segs.rbegin(); i != segs.rend(); ++i) s += "} // namespace " + *i + "\n";
    return s;
}

// The declarations shared by both surfaces. What differs between a module
// interface and a header is the preamble and how the group's namespace is
// opened; the body below is byte-identical, which is what makes switching
// surfaces a change no consumer can observe.
// The accessor declarations, at GLOBAL scope.
//
// C language linkage makes a name the same entity whatever namespace declares
// it, so these would resolve from inside the group's namespace as well. They are
// written outside it because the generated implementation defines them at global
// scope, and a reader comparing the two files should not have to know that rule
// to see that they match.
inline std::string extern_c_declarations(std::span<const item> items, const options& opt) {
    const char* elem = element_type(opt.elem);
    std::string s = "extern \"C\" {\n";
    for (auto const& it : items) {
        const auto base = accessor_base(opt, it);
        s += std::format("const {}* {}_data();\n", elem, base);
        s += std::format("unsigned long {}_size();\n", base);
    }
    s += "}\n";
    return s;
}

inline std::string declarations(std::span<const item> items, const options& opt) {
    const char* elem = element_type(opt.elem);
    std::string s;

    s += std::format(
        "// The payload, as the device API wants it. Free of standard-library\n"
        "// types on purpose: see mcpp.plugins.surface.\n"
        "struct payload {{\n"
        "    const {}* code;        // e.g. VkShaderModuleCreateInfo::pCode\n"
        "    unsigned long size_bytes;  // e.g. VkShaderModuleCreateInfo::codeSize\n"
        "}};\n\n", elem);

    // Grouped by namespace path so a directory's payloads are emitted together
    // and each namespace is opened once.
    std::vector<std::string> openNow;
    auto reopen = [&](const std::vector<std::string>& want) {
        if (openNow == want) return;
        s += close_namespaces(openNow);
        s += open_namespaces(want);
        openNow = want;
    };
    for (auto const& it : items) {
        reopen(it.name_space);
        const auto base = accessor_base(opt, it);
        s += std::format("inline payload {}() {{ return {{ {}_data(), {}_size() }}; }}\n",
                         it.identifier, base, base);
    }
    reopen({});
    return s;
}

// ---- the tool ---------------------------------------------------------------

// Writes the interface and the implementation, and returns what the caller has
// to tell mcpp about them. Nothing here reads a payload's bytes, so it runs at
// plan time and works equally for a payload the graph has not produced yet.
inline std::optional<emitted> emit(std::span<const item> items, const options& opt) {
    if (items.empty()) return emitted{};
    if (opt.module_name.empty()) {
        std::cerr << "mcpp.plugins.surface: options::module_name is required; it names both "
                     "the module a consumer imports and the namespace the declarations sit in\n";
        return std::nullopt;
    }
    if (opt.out_dir.empty()) {
        std::cerr << "mcpp.plugins.surface: options::out_dir is required\n";
        return std::nullopt;
    }

    const auto segs = split_module_name(opt.module_name);
    const auto by   = opt.produced_by.empty() ? std::string("mcpp.plugins.surface")
                                              : opt.produced_by;
    const auto dir  = std::filesystem::path(opt.out_dir);
    const auto body = declarations(items, opt);

    emitted out;

    // ---- interface ----
    std::string iface;
    iface += std::format("// Generated by mcpp.plugins.surface for {}. Do not edit.\n", by);
    if (opt.surface == kind::module_) iface += std::format("export module {};\n\n", opt.module_name);
    else                              iface += "#pragma once\n\n";
    iface += extern_c_declarations(items, opt);
    iface += "\n";
    // `export` on the opening namespace exports everything the block contains,
    // including the nested namespaces a payload's directory produced.
    if (opt.surface == kind::module_) iface += "export ";
    iface += open_namespaces(segs);
    iface += "\n" + body + "\n";
    iface += close_namespaces(segs);

    out.interface_file = (dir / (opt.module_name
                                 + (opt.surface == kind::module_ ? ".cppm" : ".h"))).string();
    if (!write_if_different(out.interface_file, iface)) {
        std::cerr << std::format("mcpp.plugins.surface: cannot write {}\n", out.interface_file);
        return std::nullopt;
    }
    if (opt.surface == kind::module_) out.module_name = opt.module_name;
    else                              out.include_dir = dir.string();

    // ---- implementation ----
    //
    // The one translation unit that includes the data headers. It is a plain
    // `.cpp` under both surfaces, because the accessors have C language linkage
    // and so need no attachment to the module.
    std::string impl;
    impl += std::format("// Generated by mcpp.plugins.surface for {}. Do not edit.\n"
                        "//\n"
                        "// The only translation unit that includes the generated data headers.\n"
                        "// Each declares a `static` array, so this is also the only copy of the\n"
                        "// bytes in the program.\n", by);
    for (auto const& it : items) impl += std::format("#include \"{}\"\n", it.data_header);
    impl += "\n";
    impl += element_width_assertion(opt.elem);
    impl += "\n";
    const char* elem = element_type(opt.elem);
    for (auto const& it : items) {
        const auto base = accessor_base(opt, it);
        const auto size = it.data_size_expr.empty()
                        ? std::format("sizeof {}", it.data_symbol)
                        : it.data_size_expr;
        impl += std::format(
            "extern \"C\" const {0}* {1}_data() {{ return {2}; }}\n"
            "extern \"C\" unsigned long {1}_size() {{ return {3}; }}\n",
            elem, base, it.data_symbol, size);
    }

    out.impl_file = (dir / (opt.module_name + ".impl.cpp")).string();
    if (!write_if_different(out.impl_file, impl)) {
        std::cerr << std::format("mcpp.plugins.surface: cannot write {}\n", out.impl_file);
        return std::nullopt;
    }
    return out;
}

// The surface a project gets when it asks for nothing.
//
// `MCPP_LANGUAGE_MODULES` is set by mcpp from `[language] modules`. An engine
// that does not set it leaves the variable absent, and the fallback is the
// header surface -- which is what every consumer of this package had before
// this module existed. An older engine therefore keeps its behaviour and a
// newer one gets the module surface without any project asking, which is the
// whole of the upgrade path.
inline kind default_surface() {
    const char* v = std::getenv("MCPP_LANGUAGE_MODULES");
    return (v && (*v == '1' || *v == 't' || *v == 'T')) ? kind::module_ : kind::c_header;
}

// `example:my-app` -> `my_app`. The module a rule names by default is this
// followed by the group's own segment, so two packages in one build cannot
// claim the same module.
inline std::string module_root_from_package() {
    std::string s;
    // MCPP_MANIFEST_DIR's leaf is the package directory, which is the closest
    // thing a build program is told about its own name. A rule that knows
    // better passes `options::module_name` and this is not consulted.
    const auto leaf = std::filesystem::path(mcpp::manifest_dir()).filename().string();
    for (char c : leaf)
        s += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    if (s.empty()) s = "app";
    if (std::isdigit(static_cast<unsigned char>(s.front()))) s.insert(s.begin(), '_');
    return s;
}

} // namespace mcpp::plugins::surface
