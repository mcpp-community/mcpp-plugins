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

// NO `import mcpp;`. That module exists only inside a build program, and this
// unit has to compile into an ordinary binary too: `mcpp.tools.embed`'s
// executable form is what lets a generated `.S` be an ACTION's output, which
// is the only way the payload it embeds can be a declared input of the edge
// that assembles it. Measured before this: an ordinary build of this package
// failed with `mcpp: failed to read compiled module`.
//
// Everything this unit used to read from that module -- the target OS, whether
// the toolchain has a GAS assembler, the package's name -- is now a parameter.
// The members under rules/ still import it; they are feature-gated, so the
// tool's own build never compiles them.

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
inline constexpr std::string_view version = "0.7.0";

} // namespace mcpp::plugins

// mcpp::plugins::names -- the derivations that turn a path into a C++ name.
//
// THESE ARE SHARED BECAUSE THEY WERE COPIED. `common_base_dir` and
// `namespace_of` were written in `rules/spirv.cppm` and written again in
// `rules/slang.cppm`, and `mcpp.tools.island` is the third caller needing the
// same answers. Two copies that agree today are still two copies: a directory
// named `default` or `2d` has to get ONE answer, and one function is how that
// is guaranteed rather than two files that happen to say the same thing.
export namespace mcpp::plugins::names {

// A GENERATED NAME THE C++ COMPILER WILL ACCEPT.
//
// Three transformations, and the third is the one every hand-rolled copy of
// this function was missing. Non-identifier characters become `_`; a leading
// digit gets a `_` in front; and a result that is a KEYWORD gets a trailing `_`.
//
// The keyword case is not hypothetical. The first two rules accept `default`,
// `template`, `operator`, `private` and `union` unchanged -- they are valid
// identifiers to a character filter and reserved to the compiler -- and
// `shaders/default/` is an ordinary name for a shader directory. What it
// produced was `namespace default {` in a generated file, and an error naming a
// line its author never wrote.
//
// TRAILING `_`, not a prefix: `_default` is reserved at namespace scope
// (a leading underscore in the global namespace), and prefixing would trade one
// reserved name for another.
//
// The list is the keywords of the standard this collection targets. A word that
// is contextual rather than reserved (`final`, `override`, `import`, `module`)
// is a legal identifier and is left alone.
inline bool is_cxx_keyword(std::string_view w) {
    static constexpr std::string_view kWords[] = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
        "bool", "break", "case", "catch", "char", "char8_t", "char16_t",
        "char32_t", "class", "compl", "concept", "const", "consteval",
        "constexpr", "constinit", "const_cast", "continue", "co_await",
        "co_return", "co_yield", "decltype", "default", "delete", "do", "double",
        "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false",
        "float", "for", "friend", "goto", "if", "inline", "int", "long",
        "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr",
        "operator", "or", "or_eq", "private", "protected", "public", "register",
        "reinterpret_cast", "requires", "return", "short", "signed", "sizeof",
        "static", "static_assert", "static_cast", "struct", "switch",
        "template", "this", "thread_local", "throw", "true", "try", "typedef",
        "typeid", "typename", "union", "unsigned", "using", "virtual", "void",
        "volatile", "wchar_t", "while", "xor", "xor_eq",
    };
    for (auto k : kWords) if (k == w) return true;
    return false;
}

// `fallback` is used when the input sanitises to nothing, which a file named
// only in punctuation does.
//
// INDEXED RATHER THAN A RANGE-FOR over the string, for the reason recorded in
// `mcpp.tools.island`: iterating a `std::string` inside an exported inline
// function makes GCC 16 instantiate its iterator in this BMI, and a consumer's
// build program then fails to compile on `always_inline` in a header naming
// neither this file nor this loop.
inline std::string identifier(std::string_view raw, std::string_view fallback) {
    std::string s;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const char c = raw[i];
        s += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    }
    if (s.empty()) s = std::string(fallback);
    if (!s.empty() && std::isdigit(static_cast<unsigned char>(s.front())))
        s.insert(s.begin(), '_');
    if (is_cxx_keyword(s)) s += '_';
    return s;
}


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

// THE BASE DIRECTORY IS DERIVED, NOT ASKED FOR.
//
// A payload's namespace comes from where it sits relative to the tree the
// project globbed, so something has to say where that tree starts. Asking the
// project would put a second spelling of the glob in the manifest, and the two
// would disagree the first time a glob moved. The shallowest directory every
// path shares is the same answer without the second spelling: for
// `shaders/*.comp` it is `shaders` and every namespace is empty; for
// `shaders/a/x.comp` and `shaders/b/y.comp` it is still `shaders`, and the two
// land in `::a` and `::b`.
//
// A single path has no common prefix with anything, so its own directory is the
// base and its namespace is empty -- which is the same answer the general case
// gives once a second file appears beside it.
inline std::string common_base_dir(std::span<const std::string> paths) {
    std::vector<std::string> prefix;
    bool first = true;
    for (auto const& src : paths) {
        std::vector<std::string> segs;
        for (auto const& part : std::filesystem::path(src).parent_path())
            if (auto s = part.string(); !s.empty() && s != ".") segs.push_back(s);
        if (first) { prefix = std::move(segs); first = false; continue; }
        std::size_t keep = 0;
        while (keep < prefix.size() && keep < segs.size() && prefix[keep] == segs[keep]) ++keep;
        prefix.resize(keep);
    }
    std::string out;
    for (auto const& s : prefix) { if (!out.empty()) out += '/'; out += s; }
    return out;
}

// The namespace segments a file sits in, below the group's own: the path from
// the base directory to the file, sanitised one segment at a time.
//
// RELATIVE BY PATH ARITHMETIC, NOT BY STRING SURGERY, AND THAT IS A WINDOWS
// FIX. This trimmed `base` off the front of the directory as a STRING and
// iterated what was left. On Windows the two spellings differ even when the
// two paths are the same: a caller states a root with forward slashes, and
// `directory_iterator` appends with the preferred separator, so the leftover
// was `\image` rather than `image`. Iterating that yields the ROOT DIRECTORY
// as its first component, which sanitised to `_` -- and the payload landed in
// `myapp::shaders::_::image`, a namespace no consumer writes. Measured: the
// island fixture failed to compile on windows-2022 with `no member named
// 'image' in namespace 'island_interface::kernels'`, while the same fixture
// passed on Linux and macOS.
//
// `lexically_relative` compares COMPONENTS, so the separator a caller happened
// to write is not part of the question. A base that is not a prefix yields a
// path starting `..`, which is a caller error rather than a namespace; it
// answers with no segments rather than with the whole absolute path, which is
// what the string form produced.
inline std::vector<std::string> namespace_of(std::string_view src, std::string_view base) {
    std::vector<std::string> out;
    const auto dir = std::filesystem::path(src).parent_path();
    auto rel = dir;
    if (!base.empty()) {
        rel = dir.lexically_relative(std::filesystem::path(base));
        if (rel.empty() || rel.begin()->string() == "..") return out;
    }
    for (auto const& part : rel) {
        auto s = part.string();
        if (s.empty() || s == "." || s == ".." || s == "/" || s == "\\") continue;
        // `shaders/default/` is an ordinary directory name and
        // `namespace default {` is not a namespace.
        out.push_back(identifier(s, "dir"));
    }
    return out;
}

} // namespace mcpp::plugins::names


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

// The name derivations live in `mcpp::plugins::names`. They are reachable under
// this namespace as well, because every rule in this collection already spells
// them this way -- one definition under two spellings is not two definitions.
using names::identifier;
using names::split_module_name;
using names::common_base_dir;
using names::namespace_of;

// How a consumer names the payloads.
//
// `module_` is the default where the project builds C++ modules, and
// `c_header` is what a project without them gets. The choice does not change
// any declaration's shape: the same struct, the same function names, the same
// namespaces. Only the file a consumer reaches them through differs.
enum class kind { module_, c_header };

// WHICH HALF OF THE SURFACE A CALL WRITES.
//
// The two are separate because their INPUTS are. The interface is a function of
// the item list: names, namespaces, the accessor declarations. The body is a
// function of the item list AND, under `storage::object`, of the payloads
// themselves -- the assembly names each one in `.incbin`, and the object it
// produces is those bytes.
//
// Declaring them as one action would make a payload change rewrite the
// interface too, and rebuild every BMI that imports it. Declaring them
// separately states what is true and costs a consumer nothing.
enum class half { interface_, body };

// The element the accessor hands back. `word32` is what an API taking
// `const uint32_t*` wants -- `vkCreateShaderModule` is the case this package
// has -- and asking for it here is cheaper than a reinterpret_cast at every
// call site, which is undefined behaviour on an under-aligned byte array.
enum class element { byte_, word32 };

// WHERE THE BYTES LIVE. Orthogonal to `kind`, which decides how a consumer
// NAMES them: the declarations are identical under all three, so a project
// changes this and no consumer changes.
//
// WHICH ONE TO USE IS A MEASUREMENT, NOT A PREFERENCE. With GCC 16.1 on this
// project's own fixtures, 100 payloads of 16 KB each:
//
//   header route   compile 0.64s + link 0.44s                = 1.10s
//   object route   convert 1.28s + compile 0.44s + link 0.46s = 2.17s
//
// The header route is faster, because at a real shader's size neither route has
// a measurable marginal cost and the total is decided by how many processes
// start -- and one compiler invocation absorbs many headers. The crossover is
// the TOTAL embedded byte count, not the payload count: below about 1 MB the
// header route wins, and above about 4 MB the compiler's slightly superlinear
// curve loses by an order of magnitude (2.31s against 0.116s at 4 MB). Source
// expansion is a constant 2.75x, which is the second half of the same reason.
//
// So `header` is the default and `object` is what a project reaches for when it
// has more payload than that, not what it reaches for because objects sound
// tidier.
enum class storage {
    // The payload is a C array in generated source, compiled into the program.
    // No assembler involved, so it works on every toolchain including MSVC.
    header,
    // The payload is a section in an object, reached through `.incbin` in a
    // generated `.S`. The bytes never pass through the C++ compiler.
    //
    // Requires a GAS-capable assembler. Every gcc and clang toolchain has one
    // on all three platforms; MSVC does not, and mcpp refuses `.S` under it, so
    // the emitter falls back to `header` there and says so once.
    object,
    // The payload is written beside the artifact and read at run time. The
    // program's correctness then depends on its working directory, which is the
    // reason this is not the default -- but it is what shader hot-reload needs,
    // and what a payload too large to link needs.
    sidecar,
};

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
    // Unused under `storage::object`, where the linker symbol is derived from
    // the accessor name instead, and under `storage::sidecar`.
    std::string data_symbol;
    // The payload file itself, as an absolute path. Required by `object`, whose
    // generated `.S` names it in `.incbin`, and by `sidecar`, which copies it.
    // The file need not exist yet: `.incbin` resolves its argument at assembly
    // time, which is after the action that writes it has run, and that is what
    // lets the whole surface be written at plan time.
    std::string payload_path;
    // Where a sidecar payload is found at run time, relative to the artifact.
    std::string sidecar_name;
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
    storage     store   = storage::header;
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

    // ── What this generator is NOT allowed to find out for itself ───────────
    //
    // THE TWO FIELDS BELOW ARE WHY THIS MODULE IMPORTS ONLY `std`.
    //
    // They were `mcpp::target_os()` and `mcpp::compiler()`, read here. That
    // made the generator unusable anywhere except inside a build program --
    // which is the one place it must NOT be the only usable form, because a
    // payload dependency can only enter the build graph if the generation is
    // an ACTION, and an action's command is a binary. A binary cannot import
    // the build-program module: measured, an ordinary build of this package
    // fails with "mcpp: failed to read compiled module".
    //
    // So they are parameters. The caller has both answers already, and a
    // generator that takes its inputs rather than reading its environment is
    // the same code in a build program and in a tool.

    // The target's operating system, as mcpp spells it -- what decides the
    // assembly dialect: the section directive and whether a symbol carries a
    // leading underscore. Required under `storage::object`; ignored otherwise.
    std::string target_os;

    // Whether a GAS-compatible assembler exists for this toolchain. False
    // under MSVC, which has none, and where mcpp refuses `.S` outright. It is
    // the CALLER's answer because the caller knows the compiler; this
    // generator degrades `object` to `header` when it is false and reports
    // that through `emitted::store` rather than by printing.
    bool has_gas_assembler = true;
};

// What the caller hands back to mcpp.
struct emitted {
    // The interface unit: a `.cppm` under `module_`, a `.h` under `c_header`.
    std::string interface_file;
    // The generated `.S` under `storage::object`, empty otherwise. The caller
    // adds it to the build the same way it adds the implementation.
    std::string assembly_file;
    // The storage actually used. Differs from what was asked for when the
    // toolchain cannot assemble: MSVC has no GAS, so `object` degrades to
    // `header` and this says so rather than leaving the caller to assume.
    storage store = storage::header;
    // The translation unit defining the accessors. Always a plain `.cpp`.
    std::string impl_file;
    // Non-empty under `module_`: what the interface unit provides, which the
    // caller declares with `mcpp::action::provides` or lets the scan find.
    std::string module_name;
    // Non-empty under `c_header`: the directory to put on the include path.
    std::string include_dir;
};

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
        // THROUGH `identifier`, HERE RATHER THAN IN EACH PRODUCER. This is the
        // one line that turns `item::identifier` into something a compiler
        // parses, and a rule that builds the field from a file stem cannot know
        // it has produced `my-shader_comp` or `default` until it gets here.
        s += std::format("inline payload {}() {{ return {{ {}_data(), {}_size() }}; }}\n",
                         identifier(it.identifier, "payload"), base, base);
    }
    reopen({});
    return s;
}

// ONE OBJECT FORMAT PER PLATFORM, AND THE DIFFERENCES ARE NOT COSMETIC.
//
// `.incbin` is the portable part: gas and clang's integrated assembler both
// accept it everywhere, and it resolves its argument at ASSEMBLY time, which is
// why this file can be written before the payload exists. What is not portable
// is the section directive and whether a C symbol carries a leading underscore.
//
//   ELF     `.section .rodata`,       symbol as written
//   Mach-O  `.section __TEXT,__const`, symbol PREFIXED with `_`
//   COFF    `.section .rdata,"dr"`,   symbol as written on x86_64
//
// Mach-O's underscore is the one that fails quietly in the other direction: an
// assembly label without it defines a symbol the C++ side never resolves, and
// the link error names the accessor rather than the missing prefix.
struct asm_dialect {
    std::string_view section;
    std::string_view symbol_prefix;
};

inline asm_dialect dialect_for(std::string_view targetOs) {
    if (targetOs == "macos" || targetOs == "macosx" || targetOs == "darwin")
        return { ".section __TEXT,__const", "_" };
    if (targetOs == "windows") return { ".section .rdata,\"dr\"", "" };
    return { ".section .rodata", "" };
}

// `.balign 4` rather than nothing: `VkShaderModuleCreateInfo::pCode` requires
// four-byte alignment, and a section directive alone does not promise it. The
// header route gets alignment from the array's element type; this route has to
// ask for it.
inline std::string assembly_for(std::span<const item> items, const options& opt,
                                std::string_view targetOs) {
    const auto d = dialect_for(targetOs);
    std::string s =
        "// Generated by mcpp.plugins.surface. Do not edit.\n"
        "//\n"
        "// The payloads, as sections rather than as C arrays. `.incbin` resolves\n"
        "// its argument when this file is assembled, which is after the actions\n"
        "// that write those files have run.\n";
    for (auto const& it : items) {
        const auto base = accessor_base(opt, it);
        s += std::format("\n    {}\n"
                         "    .globl {}{}_begin\n"
                         "    .balign 4\n"
                         "{}{}_begin:\n"
                         "    .incbin \"{}\"\n"
                         "    .globl {}{}_end\n"
                         "{}{}_end:\n",
                         d.section,
                         d.symbol_prefix, base,
                         d.symbol_prefix, base,
                         it.payload_path,
                         d.symbol_prefix, base,
                         d.symbol_prefix, base);
    }
    return s;
}

// ---- the tool ---------------------------------------------------------------

// EVERY FILE THIS SURFACE WILL PRODUCE, WITHOUT PRODUCING ANY OF THEM.
//
// A rule declares these as an action's outputs, and mcpp requires an output to
// be NAMED before the graph is built even though its content arrives later. So
// the names cannot come from having written the files -- which is the coupling
// that forced generation to happen at plan time, and with it the whole
// staleness this round removes.
//
// Pure: no items, no filesystem, no environment. The one decision it makes is
// the MSVC degradation, which is a property of the toolchain rather than of any
// payload, and `emitted::store` is where the caller reads the verdict.
inline emitted outputs(const options& opt) {
    emitted out;
    out.store = opt.store;
    // MSVC HAS NO GAS, AND mcpp REFUSES `.S` UNDER IT. `src/build/prepare.cppm`
    // says so outright: "GAS assembly sources (.S/.s) are not supported by the
    // MSVC toolchain". Object storage degrades to header storage there rather
    // than producing a file the build will refuse, and the SURFACE does not
    // change -- the declarations are identical under both.
    if (out.store == storage::object && !opt.has_gas_assembler)
        out.store = storage::header;

    const auto dir = std::filesystem::path(opt.out_dir);
    out.interface_file = (dir / (opt.module_name
                                 + (opt.surface == kind::module_ ? ".cppm" : ".h"))).string();
    out.impl_file      = (dir / (opt.module_name + ".impl.cpp")).string();
    // Named only under the storage that produces one, so a caller can test the
    // string rather than having to test the storage a second time.
    if (out.store == storage::object)
        out.assembly_file = (dir / (opt.module_name + ".payload.S")).string();
    if (opt.surface == kind::module_) out.module_name = opt.module_name;
    else                              out.include_dir = dir.string();
    return out;
}


// Writes ONE half of the surface. The names come from `outputs`, which the
// caller has already asked; this produces the content.
//
// Nothing here reads a payload's BYTES. Under object storage the assembly names
// each payload in `.incbin` and the assembler reads it later, which is why this
// can be an action whose command runs before, after or independently of the
// payload's own producer -- what matters is that the payload is a declared
// INPUT of that action, and the graph then holds the edge.
inline bool write(std::span<const item> items, const options& opt, half which) {
    if (items.empty()) return true;
    if (opt.module_name.empty()) {
        std::cerr << "mcpp.plugins.surface: options::module_name is required; it names both "
                     "the module a consumer imports and the namespace the declarations sit in\n";
        return false;
    }
    if (opt.out_dir.empty()) {
        std::cerr << "mcpp.plugins.surface: options::out_dir is required\n";
        return false;
    }

    const auto segs = split_module_name(opt.module_name);
    // REFUSED HERE, NOT IN THE GENERATED FILE. The module name is the one part
    // of this surface a project writes itself, and each of its segments becomes
    // a namespace. A segment that is not an identifier -- empty, starting with
    // a digit, carrying a `-`, or reserved -- produces a generated file that
    // does not parse, and the error then names a line nobody wrote.
    for (std::size_t i = 0; i < segs.size(); ++i) {
        if (segs[i] == identifier(segs[i], "")) continue;
        std::cerr << std::format(
            "mcpp.plugins.surface: `{}` is not a usable module name: the segment `{}` "
            "is not a C++ identifier.\n  Each segment becomes a namespace, so it has "
            "to be one; `{}` would work.\n",
            opt.module_name, segs[i], identifier(segs[i], "part"));
        return false;
    }
    const auto by   = opt.produced_by.empty() ? std::string("mcpp.plugins.surface")
                                              : opt.produced_by;

    // ONE derivation of the names, shared with the caller. Recomputing them
    // here would be the second copy of a decision that this package has paid
    // for before.
    const emitted out  = outputs(opt);
    const auto    dir  = std::filesystem::path(opt.out_dir);
    const auto    body = declarations(items, opt);

    // ---- interface ----
    if (which == half::interface_) {
        std::string iface;
        iface += std::format("// Generated by mcpp.plugins.surface for {}. Do not edit.\n", by);
        if (opt.surface == kind::module_)
            iface += std::format("export module {};\n\n", opt.module_name);
        else
            iface += "#pragma once\n\n";
        iface += extern_c_declarations(items, opt);
        iface += "\n";
        // `export` on the opening namespace exports everything the block
        // contains, including the nested namespaces a payload's directory
        // produced.
        if (opt.surface == kind::module_) iface += "export ";
        iface += open_namespaces(segs);
        iface += "\n" + body + "\n";
        iface += close_namespaces(segs);
        if (!write_if_different(out.interface_file, iface)) {
            std::cerr << std::format("mcpp.plugins.surface: cannot write {}\n",
                                     out.interface_file);
            return false;
        }
        return true;
    }

    // ---- implementation ---- (half::body from here down)
    //
    // The one translation unit that defines the accessors. A plain `.cpp` under
    // every surface and every storage, because the accessors have C language
    // linkage and so need no attachment to the module. What differs between the
    // three storages is only where it reads the bytes from.
    const char* elem = element_type(opt.elem);
    std::string impl;
    impl += std::format("// Generated by mcpp.plugins.surface for {}. Do not edit.\n", by);

    if (out.store == storage::header) {
        impl += "//\n"
                "// The only translation unit that includes the generated data headers.\n"
                "// Each declares a `static` array, so this is also the only copy of the\n"
                "// bytes in the program.\n";
        for (auto const& it : items) impl += std::format("#include \"{}\"\n", it.data_header);
        impl += "\n";
        impl += element_width_assertion(opt.elem);
        impl += "\n";
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
    } else if (out.store == storage::object) {
        impl += "//\n"
                "// The bytes are in a section written by the generated `.S`. This file\n"
                "// only names its two boundary symbols, so nothing here parses a payload\n"
                "// and the C++ compiler never sees one.\n"
                "//\n"
                "// The symbols are declared as arrays of the element type rather than as\n"
                "// `char`: the assembly aligned the section to four bytes, and a\n"
                "// declaration that said `char` would let a consumer reach it through an\n"
                "// under-aligned pointer with nothing to notice.\n";
        impl += "\n";
        impl += element_width_assertion(opt.elem);
        impl += "\nextern \"C\" {\n";
        for (auto const& it : items) {
            const auto base = accessor_base(opt, it);
            impl += std::format("extern const {0} {1}_begin[];\n"
                                "extern const {0} {1}_end[];\n", elem, base);
        }
        impl += "}\n\n";
        for (auto const& it : items) {
            const auto base = accessor_base(opt, it);
            impl += std::format(
                "extern \"C\" const {0}* {1}_data() {{ return {1}_begin; }}\n"
                "extern \"C\" unsigned long {1}_size() {{\n"
                "    return static_cast<unsigned long>(({1}_end - {1}_begin) * sizeof({0}));\n"
                "}}\n", elem, base);
        }
    } else {
        impl += "//\n"
                "// The payloads are files beside the artifact, read on first use. The\n"
                "// path is resolved against the WORKING DIRECTORY, which is the property\n"
                "// that makes this storage the one a project opts into rather than the\n"
                "// default: a program started from elsewhere finds nothing.\n"
                "//\n"
                "// Read once and kept: an accessor that reloaded would hand two callers\n"
                "// two different pointers to the same payload, and a device API given\n"
                "// the second after the first was freed is a defect with no message.\n";
        impl += "#include <cstdio>\n#include <cstdlib>\n#include <cstring>\n\n";
        impl += element_width_assertion(opt.elem);
        impl += std::format(R"IMPL(
namespace {{

struct blob {{ {0}* data = nullptr; unsigned long size = 0; bool tried = false; }};

blob& load(const char* path, blob& b) {{
    if (b.tried) return b;
    b.tried = true;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {{
        std::fprintf(stderr, "mcpp.plugins.surface: cannot open %s\n", path);
        return b;
    }}
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {{
        b.data = static_cast<{0}*>(std::malloc(static_cast<unsigned long>(n)));
        if (b.data && std::fread(b.data, 1, static_cast<unsigned long>(n), f)
                      == static_cast<unsigned long>(n))
            b.size = static_cast<unsigned long>(n);
        else {{ std::free(b.data); b.data = nullptr; }}
    }}
    std::fclose(f);
    return b;
}}

}} // namespace
)IMPL", elem);
        impl += "\n";
        for (auto const& it : items) {
            const auto base = accessor_base(opt, it);
            impl += std::format(
                "static blob {0}_blob;\n"
                "extern \"C\" const {1}* {0}_data() {{\n"
                "    return load(\"{2}\", {0}_blob).data;\n"
                "}}\n"
                "extern \"C\" unsigned long {0}_size() {{\n"
                "    return load(\"{2}\", {0}_blob).size;\n"
                "}}\n", base, elem, it.sidecar_name);
        }
    }

    if (!write_if_different(out.impl_file, impl)) {
        std::cerr << std::format("mcpp.plugins.surface: cannot write {}\n", out.impl_file);
        return false;
    }

    if (out.store == storage::object) {
        // A CALLER THAT ASKS FOR THIS STORAGE MUST HAVE SAID WHERE THE BYTES
        // ARE, AND THE REFUSAL IS WHAT KEEPS THAT TRUE.
        //
        // `item::payload_path` is documented as required under `object`, and
        // today only `mcpp.rules.spirv` reaches this storage and only it sets
        // the field. That is a COINCIDENCE, not a guarantee: the first caller
        // to give `rules-slang` or `tools-embed` a storage option would get an
        // `.incbin ""` in a generated `.S`, and the declaration below would
        // then name an empty dependency -- so the build would be wrong in
        // exactly the silent way this whole change exists to remove.
        //
        // A constraint written only in a comment has nothing enforcing it,
        // which is a shape this project has recorded before. This is the
        // enforcement.
        for (auto const& it : items) {
            if (!it.payload_path.empty()) continue;
            std::cerr << std::format(
                "mcpp.plugins.surface: `{}` was given storage::object with no "
                "payload_path.\n"
                "  Under this storage the generated assembly names the payload "
                "in `.incbin`, so\n"
                "  the caller has to say where it is. Set `item::payload_path`, "
                "or use storage::header.\n",
                it.identifier);
            return false;
        }
        if (!write_if_different(out.assembly_file,
                                assembly_for(items, opt, opt.target_os))) {
            std::cerr << std::format("mcpp.plugins.surface: cannot write {}\n",
                                     out.assembly_file);
            return false;
        }
    }
    // `storage::sidecar` needs no counterpart, and that was checked rather than
    // assumed. Its payload is never read by a compile: the accessor opens the
    // file at RUN time, so a rebuilt payload is picked up by the next run with
    // nothing in the build graph to keep current.
    return true;
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
// The default module root for a package that named none: its PACKAGE NAME,
// sanitised into an identifier.
//
// THE PACKAGE'S NAME. NOT ITS DIRECTORY'S. These are different questions and
// they give different answers whenever a project lays a package out under a
// generic directory. The first version asked the only question mcpp could
// answer -- the leaf of MCPP_MANIFEST_DIR -- so a package named `vulkan-saxpy`
// laid out as `vulkan/app/` generated `app.shaders`, and every
// `<something>/app/` in a workspace claimed that same module.
//
// The name ARRIVES rather than being read, for the reason `options::target_os`
// records: this module must compile into a plain binary as well as into a
// build program, so it may not reach for `mcpp::package_name()` itself. The
// caller passes what mcpp told it.
//
// `fallback` covers the impossible case -- a manifest without a `[package]
// name` does not load, so an empty first argument would mean the contract
// changed underneath. A rule that wants neither passes `options::module_name`,
// and this is not consulted.
inline std::string module_root_for(std::string_view package_name,
                                   std::string_view fallback = {}) {
    std::string leaf{package_name};
    if (leaf.empty()) leaf = std::string(fallback);
    return identifier(leaf, "app");
}

} // namespace mcpp::plugins::surface
