// mcpp.plugins.binary -- the libraries a compiled file names as dependencies.
//
// A build program sometimes needs to know what a shared library it places
// beside a program loads in turn: `rules-qt` and `rules-qml` read the Qt plugins
// they deploy, whose own dependencies no import table of the program names.
// The three formats a desktop target has, read from the file's bytes:
//
//   ELF     the `DT_NEEDED` entries of the dynamic section
//   PE      the import table, and the delay-load import table
//   Mach-O  the `LC_LOAD_DYLIB` family of load commands, thin or fat
//
// Only the names the file states are returned, in the order it states them.
// The caller decides which of them it accepts: a name read from a file is not
// a path, and `plain_name` says what a caller that joins a name to a directory
// must check first.
//
// The reader is for files nobody vouches for. Every offset and length is
// checked against the file's size before it is read, so a truncated or damaged
// file yields an error, never a read past the buffer; the whole file is held in
// memory, up to a limit (1 GiB). Nothing here includes a header or asks the
// operating system for anything: it is `std` alone.
//
// NOT READ, AND SAID SO: a big-endian ELF or Mach-O, and a Mach-O's
// `LC_ID_DYLIB`, the file's own name rather than a dependency.

export module mcpp.plugins.binary;

import std;

export namespace mcpp::plugins::binary {

struct result {
    std::vector<std::string> names;   // the libraries named, once each, in file order
    std::string error;                // empty when the file was read
    bool ok() const { return error.empty(); }
};

// A name a caller may join to a directory: no separator, no `..`, not empty.
// What the file says is what it says; this is the caller's filter.
inline bool plain_name(std::string_view name) {
    if (name.empty() || name == "." || name == "..") return false;
    return name.find('/') == std::string_view::npos && name.find('\\') == std::string_view::npos;
}

namespace detail {

// A read-only window on the file's bytes. Every accessor checks the range and
// assembles the value byte by byte, so the host's byte order does not matter.
struct view {
    const unsigned char* p;
    std::size_t n;

    bool in(std::uint64_t off, std::uint64_t len) const { return off <= n && len <= n - off; }

    bool u8(std::uint64_t off, std::uint8_t& out) const {
        if (!in(off, 1)) return false;
        out = p[off];
        return true;
    }
    bool uint_n(std::uint64_t off, unsigned bytes, bool big, std::uint64_t& out) const {
        if (!in(off, bytes)) return false;
        std::uint64_t v = 0;
        for (unsigned i = 0; i < bytes; ++i) {
            const std::uint64_t b = p[off + (big ? i : bytes - 1 - i)];
            v = (v << 8) | b;
        }
        out = v;
        return true;
    }
    bool u16(std::uint64_t off, std::uint16_t& out, bool big = false) const {
        std::uint64_t v;
        if (!uint_n(off, 2, big, v)) return false;
        out = static_cast<std::uint16_t>(v);
        return true;
    }
    bool u32(std::uint64_t off, std::uint32_t& out, bool big = false) const {
        std::uint64_t v;
        if (!uint_n(off, 4, big, v)) return false;
        out = static_cast<std::uint32_t>(v);
        return true;
    }
    bool u64(std::uint64_t off, std::uint64_t& out, bool big = false) const {
        return uint_n(off, 8, big, out);
    }
    // A NUL-terminated string at `off`, at most `max` bytes.
    bool cstr(std::uint64_t off, std::string& out, std::size_t max = 4096) const {
        out.clear();
        for (std::size_t i = 0; i < max; ++i) {
            if (!in(off + i, 1)) return false;
            const char c = static_cast<char>(p[off + i]);
            if (c == '\0') return true;
            out.push_back(c);
        }
        return false;
    }
};

inline void add(result& r, std::string s) {
    if (s.empty()) return;
    for (std::size_t i = 0; i < r.names.size(); ++i)
        if (r.names[i] == s) return;
    r.names.push_back(std::move(s));
}

inline result fail(std::string what) {
    result r;
    r.error = std::move(what);
    return r;
}

result elf(const view& v) {
    result r;
    std::uint8_t cls = 0, data = 0;
    v.u8(4, cls);
    v.u8(5, data);
    if (cls != 1 && cls != 2) return fail("unknown ELF class");
    if (data != 1) return fail("a big-endian ELF is not read");
    const bool is64 = cls == 2;
    std::uint64_t phoff = 0;
    std::uint16_t phentsize = 0, phnum = 0;
    bool header = false;
    if (is64) {
        header = v.u64(32, phoff) && v.u16(54, phentsize) && v.u16(56, phnum);
    } else {
        std::uint32_t o = 0;
        header = v.u32(28, o) && v.u16(42, phentsize) && v.u16(44, phnum);
        phoff = o;
    }
    if (!header) return fail("truncated ELF header");
    struct segment { std::uint64_t vaddr, off, filesz; };
    std::vector<segment> loads;
    std::uint64_t dynoff = 0, dynsz = 0;
    bool have_dynamic = false;
    for (unsigned i = 0; i < phnum; ++i) {
        const std::uint64_t at = phoff + std::uint64_t(i) * phentsize;
        std::uint32_t type = 0;
        if (!v.u32(at, type)) return fail("truncated program headers");
        std::uint64_t off = 0, vaddr = 0, filesz = 0;
        if (is64) {
            if (!v.u64(at + 8, off) || !v.u64(at + 16, vaddr) || !v.u64(at + 32, filesz))
                return fail("truncated program headers");
        } else {
            std::uint32_t a = 0, b = 0, c = 0;
            if (!v.u32(at + 4, a) || !v.u32(at + 8, b) || !v.u32(at + 16, c))
                return fail("truncated program headers");
            off = a; vaddr = b; filesz = c;
        }
        if (type == 1) loads.push_back({vaddr, off, filesz});
        if (type == 2) { dynoff = off; dynsz = filesz; have_dynamic = true; }
    }
    if (!have_dynamic) return r;                 // statically linked: nothing is loaded
    const std::size_t entry = is64 ? 16 : 8;
    std::uint64_t strtab = 0;
    std::vector<std::uint64_t> needed;
    for (std::uint64_t o = 0; o + entry <= dynsz; o += entry) {
        std::uint64_t tag = 0, val = 0;
        if (is64) {
            if (!v.u64(dynoff + o, tag) || !v.u64(dynoff + o + 8, val)) return fail("truncated dynamic section");
        } else {
            std::uint32_t a = 0, b = 0;
            if (!v.u32(dynoff + o, a) || !v.u32(dynoff + o + 4, b)) return fail("truncated dynamic section");
            tag = a; val = b;
        }
        if (tag == 0) break;                      // DT_NULL
        if (tag == 1) needed.push_back(val);      // DT_NEEDED
        if (tag == 5) strtab = val;               // DT_STRTAB
    }
    if (needed.empty()) return r;
    std::uint64_t strings = 0;
    bool mapped = false;
    for (std::size_t i = 0; i < loads.size() && !mapped; ++i)
        if (strtab >= loads[i].vaddr && strtab - loads[i].vaddr < loads[i].filesz) {
            strings = strtab - loads[i].vaddr + loads[i].off;
            mapped = true;
        }
    if (!mapped) return fail("the dynamic string table is outside the file");
    for (std::size_t i = 0; i < needed.size(); ++i) {
        std::string s;
        if (needed[i] > ~std::uint64_t(0) - strings || !v.cstr(strings + needed[i], s))
            return fail("a dependency name is outside the file");
        add(r, std::move(s));
    }
    return r;
}

result pe(const view& v) {
    result r;
    std::uint32_t lfanew = 0, signature = 0;
    if (!v.u32(0x3c, lfanew) || !v.u32(lfanew, signature) || signature != 0x00004550)
        return fail("not a PE file");
    std::uint16_t sections = 0, optsize = 0, magic = 0;
    if (!v.u16(std::uint64_t(lfanew) + 6, sections) || !v.u16(std::uint64_t(lfanew) + 20, optsize) ||
        !v.u16(std::uint64_t(lfanew) + 24, magic))
        return fail("truncated PE header");
    if (magic != 0x20b && magic != 0x10b) return fail("unknown PE optional header");
    const std::uint64_t opt = std::uint64_t(lfanew) + 24;
    const std::uint64_t dirs = opt + (magic == 0x20b ? 112 : 96);
    struct section { std::uint32_t va, vsize, raw, rawsize; };
    std::vector<section> table;
    const std::uint64_t first = opt + optsize;
    for (unsigned i = 0; i < sections; ++i) {
        const std::uint64_t a = first + std::uint64_t(i) * 40;
        section s{};
        if (!v.u32(a + 8, s.vsize) || !v.u32(a + 12, s.va) || !v.u32(a + 16, s.rawsize) || !v.u32(a + 20, s.raw))
            return fail("truncated section table");
        table.push_back(s);
    }
    auto to_offset = [&](std::uint32_t rva, std::uint64_t& out) {
        for (std::size_t i = 0; i < table.size(); ++i) {
            const std::uint32_t len = std::max(table[i].vsize, table[i].rawsize);
            if (rva >= table[i].va && rva - table[i].va < len) {
                out = std::uint64_t(rva - table[i].va) + table[i].raw;
                return true;
            }
        }
        return false;
    };
    // One import directory: `entry` bytes per descriptor, the name's RVA at
    // `name_at`, the list ended by a descriptor whose name is zero. A directory
    // the file does not have is not an error.
    auto walk = [&](unsigned index, std::size_t entry, std::size_t name_at) -> bool {
        std::uint32_t rva = 0, size = 0;
        const std::uint64_t d = dirs + std::uint64_t(index) * 8;
        if (!v.u32(d, rva) || !v.u32(d + 4, size)) return true;
        if (rva == 0) return true;
        std::uint64_t base = 0;
        if (!to_offset(rva, base)) return false;
        for (std::uint64_t o = 0, count = 0; count < 4096; o += entry, ++count) {
            std::uint32_t name = 0;
            if (!v.u32(base + o + name_at, name)) return false;
            if (name == 0) return true;
            std::uint64_t at = 0;
            std::string s;
            if (!to_offset(name, at) || !v.cstr(at, s, 256)) return false;
            add(r, std::move(s));
        }
        return false;
    };
    if (!walk(1, 20, 12) || !walk(13, 32, 4)) return fail("an import table is outside the file");
    return r;
}

result macho_one(const view& v, std::uint64_t base) {
    result r;
    std::uint32_t magic = 0;
    if (!v.u32(base, magic)) return fail("truncated Mach-O header");
    const bool is64 = magic == 0xfeedfacf;
    if (!is64 && magic != 0xfeedface) return fail("not a little-endian Mach-O");
    std::uint32_t ncmds = 0;
    if (!v.u32(base + 16, ncmds)) return fail("truncated Mach-O header");
    std::uint64_t at = base + (is64 ? 32 : 28);
    for (std::uint32_t i = 0; i < ncmds; ++i) {
        std::uint32_t cmd = 0, size = 0;
        if (!v.u32(at, cmd) || !v.u32(at + 4, size) || size < 8) return fail("truncated load commands");
        // LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB, LC_LAZY_LOAD_DYLIB,
        // LC_LOAD_UPWARD_DYLIB. LC_ID_DYLIB (0xd) is the file's own name.
        if (cmd == 0xc || cmd == 0x80000018 || cmd == 0x8000001f || cmd == 0x20 || cmd == 0x80000023) {
            std::uint32_t name_off = 0;
            std::string s;
            if (!v.u32(at + 8, name_off) || name_off >= size || !v.cstr(at + name_off, s))
                return fail("a dylib command is damaged");
            add(r, std::move(s));
        }
        at += size;
    }
    return r;
}

result macho(const view& v) {
    std::uint32_t magic = 0;
    v.u32(0, magic, true);
    if (magic != 0xcafebabe && magic != 0xcafebabf) return macho_one(v, 0);
    const bool wide = magic == 0xcafebabf;
    std::uint32_t count = 0;
    if (!v.u32(4, count, true) || count > 64) return fail("a fat header is damaged");
    result all;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint64_t a = 8 + std::uint64_t(i) * (wide ? 32 : 20);
        std::uint64_t off = 0;
        if (wide) {
            if (!v.u64(a + 8, off, true)) return fail("a fat header is damaged");
        } else {
            std::uint32_t o = 0;
            if (!v.u32(a + 8, o, true)) return fail("a fat header is damaged");
            off = o;
        }
        result one = macho_one(v, off);
        if (!one.ok()) return one;
        for (std::size_t k = 0; k < one.names.size(); ++k) add(all, one.names[k]);
    }
    return all;
}

} // namespace detail

constexpr std::uint64_t max_file_size = std::uint64_t(1) << 30;   // 1 GiB

// The dependencies a file's bytes name. The format is told by the file's
// magic, never by its name.
result needed_from_bytes(std::string_view bytes) {
    const detail::view v{ reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size() };
    if (v.n >= 20 && bytes.substr(0, 4) == std::string_view("\x7f" "ELF", 4)) return detail::elf(v);
    if (v.n >= 2 && bytes[0] == 'M' && bytes[1] == 'Z') return detail::pe(v);
    if (v.n >= 8) {
        std::uint32_t little = 0, big = 0;
        v.u32(0, little);
        v.u32(0, big, true);
        if (little == 0xfeedface || little == 0xfeedfacf || big == 0xcafebabe || big == 0xcafebabf)
            return detail::macho(v);
    }
    return detail::fail("an unrecognised binary format");
}

// The dependencies of the file at `path`. Opened through `std::filesystem::path`,
// so a non-ASCII name opens on Windows as well.
result needed(const std::filesystem::path& path) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) return detail::fail("cannot read " + path.generic_string());
    if (size > max_file_size) return detail::fail(path.generic_string() + " is larger than 1 GiB");
    std::ifstream in(path, std::ios::binary);
    if (!in) return detail::fail("cannot open " + path.generic_string());
    std::string data(static_cast<std::size_t>(size), '\0');
    in.read(data.data(), static_cast<std::streamsize>(size));
    if (static_cast<std::uintmax_t>(in.gcount()) != size) return detail::fail("cannot read " + path.generic_string());
    return needed_from_bytes(data);
}

} // namespace mcpp::plugins::binary
