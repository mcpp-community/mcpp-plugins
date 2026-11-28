export module mcpp.plugins:names;

import std;


// mcpp::plugins::names -- the derivations that turn a path into a C++ name.
//
// THESE ARE SHARED BECAUSE THEY WERE COPIED. `common_base_dir` and
// `namespace_of` were written in `rules/spirv.cppm` and written again in
// `rules/slang.cppm`, and `mcpp.tools.island` is the third caller needing the
// same answers. Two copies that agree today are still two copies: a directory
// named `default` or `2d` has to get ONE answer, and one function is how that
// is guaranteed rather than two files that happen to say the same thing.
export namespace mcpp::plugins::names {

// `a` made relative to the directory `b`, as strings: separators unified, and
// the prefix stripped when `a` lies under `b`; `a` unchanged otherwise.
//
// NOTHING IN THIS PACKAGE INSTANTIATES `std::filesystem::path`'s ITERATOR --
// see `components()` below for the compiler that refuses it. The members'
// relative-path arithmetic is this string function, defined once here.
inline std::string relative_to(std::string a, std::string b) {
    for (auto& c : a) if (c == '\\') c = '/';
    for (auto& c : b) if (c == '\\') c = '/';
    while (!b.empty() && b.back() == '/') b.pop_back();
    if (!b.empty() && a.starts_with(b + "/")) return a.substr(b.size() + 1);
    return a;
}

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
// A path's components as strings: both separators split, empty and `.`
// components dropped. This is the one place the lib root reads a path apart,
// and it does so WITHOUT `std::filesystem::path`'s iterator on purpose:
//
// A MODULE THAT INSTANTIATES `_Path_iterator` POISONS ITS IMPORTERS UNDER MSVC
// 14.52. Measured on xrgui's CI (14.52.36629 and .36725): this unit compiled
// while it iterated paths, and every member importing it that then touched
// `std::filesystem` at all failed inside the STL --
//
//   include\filesystem(1572): error C2801: '..._Path_iterator<...>::operator =='
//   must be a non-static member
//
// -- the iterator's hidden-friend comparison, refused when the importer meets
// it both through `import std` and through this module's BMI. Removing the
// calls from the members changed nothing; the instantiation had to leave the
// lib root. Component comparison is what `lexically_relative` bought in 0.5.2
// (a Windows separator bug), and splitting on both separators keeps that.
inline std::vector<std::string> components(std::string_view path) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] { if (!cur.empty() && cur != ".") out.push_back(cur); cur.clear(); };
    for (char c : path) { if (c == '/' || c == '\\') flush(); else cur += c; }
    flush();
    return out;
}

// The directory part of a path, as written: everything before the last
// separator, or empty when there is none.
inline std::string_view parent_of(std::string_view path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash);
}

inline std::string common_base_dir(std::span<const std::string> paths) {
    std::vector<std::string> prefix;
    bool first = true;
    for (auto const& src : paths) {
        auto segs = components(parent_of(src));
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
// COMPONENTS are compared, so the separator a caller happened to write is not
// part of the question -- `components()` splits on both. A base that is not a
// prefix is a caller error rather than a namespace; it answers with no
// segments rather than with the whole absolute path, which is what the first
// string form produced. (Through `components()` rather than
// `lexically_relative` for the reason stated on it.)
inline std::vector<std::string> namespace_of(std::string_view src, std::string_view base) {
    std::vector<std::string> out;
    auto dir = components(parent_of(src));
    if (!base.empty()) {
        const auto b = components(base);
        if (b.size() > dir.size()) return out;
        for (std::size_t i = 0; i < b.size(); ++i) if (b[i] != dir[i]) return out;
        dir.erase(dir.begin(), dir.begin() + static_cast<std::ptrdiff_t>(b.size()));
    }
    for (auto const& s : dir) {
        if (s == "..") continue;
        // `shaders/default/` is an ordinary directory name and
        // `namespace default {` is not a namespace.
        out.push_back(identifier(s, "dir"));
    }
    return out;
}
} // namespace mcpp::plugins::names
