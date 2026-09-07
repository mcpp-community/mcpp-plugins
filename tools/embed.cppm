// mcpp.tools.embed - a data file becomes a header the program compiles in.
//
// WHY THIS IS A TOOL AND NOT A RULE. A rule (`mcpp.rules.<x>`) states how a
// translation unit is compiled by a compiler mcpp does not drive: it submits an
// action, the engine schedules it, and the work happens in the build graph. A
// tool states something a build program needs that no compiler performs. This
// one reads bytes and writes a header, so there is no external program to
// schedule and no action to submit; the file is written while `build.mcpp`
// runs, before the engine plans anything.
//
// THE OUTPUT IS A HEADER, FOR THE REASON `mcpp.rules.spirv` GIVES. A data file
// beside the binary makes the program's correctness depend on its working
// directory. A header compiled into the program does not, and `mcpp pack` of
// that program has nothing further to collect.
//
// IT DOES NOT REWRITE AN UNCHANGED HEADER. Writing the same bytes again would
// still move the file's mtime, and every translation unit that includes it
// would rebuild on a build where nothing changed. The comparison is on content
// and is the reason this tool is safe to call unconditionally from a build
// program that runs on every configure.
//
// WHAT IT DELIBERATELY DOES NOT DO. It does not compress, does not chunk a
// large file across several arrays, and does not emit a `std::span` accessor.
// Each is reachable from what it emits, and a tool in this collection earns a
// feature by being needed by more than one consumer.
module;
#include <cctype>
#include <cstdio>

export module mcpp.tools.embed;

import std;
import mcpp;


// WHY NOTHING HERE USES `std::println`, AND WHY THAT IS NOT A STYLE CHOICE.
//
// `std::print` and `std::println` are not header-only. Both of their overloads
// reach into the libc++ DYLIB -- `__is_posix_terminal(FILE*)` for the stdout
// form and `__get_ostream_file(ostream&)` for the stream form -- and those
// symbols were added to that library in a version macOS 14 does not ship. A
// build program's link resolves `-lc++` to the system copy there, so a rule
// that printed with `std::println` compiled and then failed to link:
//
//   ld64.lld: error: undefined symbol: std::__1::__is_posix_terminal(__sFILE*)
//
// naming neither the call that needed it nor the reason. Measured on
// macos-14; macos-15 has the symbol, which is why nothing saw this until a
// rule was first compiled on the older of the two supported releases.
//
// `std::format` is header-only and has no such dependency, so every message in
// this file is formatted and then streamed.

export namespace mcpp::tools::embed {

// The element the array is made of. A byte array is the general answer; a
// 32-bit word array is what an API that takes `const uint32_t*` wants -- SPIR-V
// is the case that exists in this repository -- and asking for it here is
// cheaper than a reinterpret_cast at every call site, which is undefined
// behaviour on an under-aligned byte array.
enum class element { byte_, word32 };

struct options {
    // Where the header is written. Empty means `<out_dir>/include/mcpp.tools.embed`,
    // which is added to the include path; a caller that names a directory owns
    // adding it.
    std::string out_dir;
    // The C++ identifier the array is called. Empty derives it from the input
    // file name: every character that is not alphanumeric becomes `_`, and a
    // leading digit is prefixed with `_`.
    std::string identifier;
    // An optional namespace for the two symbols. Nested namespaces are written
    // with `::` and emitted as a C++17 nested definition.
    std::string name_space;
    element     elem = element::byte_;
    // A trailing zero byte, so the array is usable as a C string. It is counted
    // by neither `_size` nor the array's declared bound comment; the array
    // simply has one more element than `_size` says.
    bool        null_terminate = false;
    // Bytes per line in the generated file. Only the file's shape depends on
    // it; no consumer can observe it.
    unsigned    width = 16;
};

// ---- internals -------------------------------------------------------------

inline std::string sanitise(std::string_view stem) {
    std::string s;
    for (char c : stem)
        s += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    if (s.empty()) s = "data";
    if (std::isdigit(static_cast<unsigned char>(s.front()))) s.insert(s.begin(), '_');
    return s;
}

inline std::string default_dir() {
    const char* out = mcpp::out_dir();
    return (std::filesystem::path(out && *out ? out : ".") / "include" / "mcpp.tools.embed").string();
}

inline std::string identifier_for(const std::filesystem::path& input, const options& opt) {
    if (!opt.identifier.empty()) return opt.identifier;
    auto stem = input.filename().string();
    return sanitise(stem);
}

// The header a given input produces, without producing it. A consumer that
// wants to `#include` it by an explicit path rather than by name asks here.
inline std::string header_path(const std::filesystem::path& input, const options& opt = {}) {
    const auto dir = opt.out_dir.empty() ? default_dir() : opt.out_dir;
    return (std::filesystem::path(dir) / (identifier_for(input, opt) + ".h")).string();
}

inline bool write_if_different(const std::filesystem::path& path, std::string_view text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (std::ifstream in(path, std::ios::binary); in) {
        std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (old == text) return true;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

// ---- the tool ---------------------------------------------------------------

// One file. Returns false and explains on stderr when the input cannot be read
// or the header cannot be written; a build program returns that value.
inline bool file(const std::filesystem::path& input, options opt = {}) {
    const std::string root = mcpp::manifest_dir();
    const auto absolute = input.is_absolute()
                        ? input : std::filesystem::path(root) / input;

    std::ifstream in(absolute, std::ios::binary);
    if (!in) {
        std::cerr << std::format("mcpp.tools.embed: cannot read {}", absolute.string()) << '\n';
        return false;
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    if (opt.elem == element::word32 && bytes.size() % 4 != 0) {
        std::cerr << std::format("mcpp.tools.embed: {} is {} bytes, which is not a multiple of 4, and "
            "element::word32 was asked for", absolute.string(), bytes.size()) << '\n';
        return false;
    }

    const auto id  = identifier_for(absolute, opt);
    const auto dir = opt.out_dir.empty() ? default_dir() : opt.out_dir;
    const auto out = std::filesystem::path(dir) / (id + ".h");

    std::string text;
    text += "// Generated by mcpp.tools.embed from ";
    text += absolute.filename().string();
    text += ". Do not edit.\n#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\n";

    std::vector<std::string> opened;
    if (!opt.name_space.empty()) {
        text += "namespace " + opt.name_space + " {\n\n";
    }

    const bool word = opt.elem == element::word32;
    const auto count = word ? bytes.size() / 4 : bytes.size();
    text += word ? "inline constexpr std::uint32_t " : "inline constexpr unsigned char ";
    text += id;
    text += "[] = {";
    const unsigned per_line = opt.width == 0 ? 16 : opt.width;
    for (std::size_t i = 0; i < count; ++i) {
        if (i % per_line == 0) text += "\n    ";
        if (word) {
            const auto b = reinterpret_cast<const unsigned char*>(bytes.data()) + i * 4;
            text += std::format("0x{:08x}u,", static_cast<std::uint32_t>(b[0])
                                            | (static_cast<std::uint32_t>(b[1]) << 8)
                                            | (static_cast<std::uint32_t>(b[2]) << 16)
                                            | (static_cast<std::uint32_t>(b[3]) << 24));
        } else {
            text += std::format("0x{:02x},", static_cast<unsigned>(
                static_cast<unsigned char>(bytes[i])));
        }
        if (i + 1 < count) text += ' ';
    }
    if (opt.null_terminate && !word) {
        if (count % per_line == 0) text += "\n    ";
        text += "0x00,";
    }
    text += "\n};\n\n";
    text += std::format("inline constexpr std::size_t {}_size = {};\n", id, count);
    if (!opt.name_space.empty()) text += "\n} // namespace " + opt.name_space + "\n";

    if (!write_if_different(out, text)) {
        std::cerr << std::format("mcpp.tools.embed: cannot write {}", out.string()) << '\n';
        return false;
    }

    // The build program is cached on its inputs, so a file it reads is a file
    // it must declare: without this, editing the data leaves the header from
    // the previous build in place and the program compiles yesterday's bytes.
    mcpp::rerun_if_changed(absolute.string().c_str());
    if (opt.out_dir.empty()) mcpp::include_dir(dir.c_str());
    return true;
}

// Several files, sharing one set of options. The identifier is derived per
// file, so `options::identifier` is refused here rather than silently applied
// to the first input only.
inline bool files(std::span<const std::string> inputs, options opt = {}) {
    if (!opt.identifier.empty()) {
        std::cerr << std::format("mcpp.tools.embed: options::identifier names one "
                             "symbol and files() writes several; call file() per input") << '\n';
        return false;
    }
    for (auto const& one : inputs)
        if (!file(one, opt)) return false;
    return true;
}

} // namespace mcpp::tools::embed
