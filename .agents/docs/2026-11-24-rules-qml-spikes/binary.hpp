// Prototype of mcpp.plugins.binary::needed -- std only. Returns the library names a
// shared library or program names as dependencies, or an error string.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>
namespace binary {
struct result { std::vector<std::string> names; std::string error; bool ok() const { return error.empty(); } };
namespace detail {
struct view {
    const unsigned char* p; std::size_t n;
    bool in(std::uint64_t off, std::uint64_t len) const { return off <= n && len <= n - off; }
    template <class T> bool get(std::uint64_t off, T& out, bool big = false) const {
        if (!in(off, sizeof(T))) return false;
        unsigned char b[sizeof(T)]; std::memcpy(b, p + off, sizeof(T));
        if (big) std::reverse(b, b + sizeof(T));
        std::memcpy(&out, b, sizeof(T)); return true;       // host is little-endian (x86-64, arm64)
    }
    bool cstr(std::uint64_t off, std::string& out, std::size_t max = 4096) const {
        out.clear();
        for (std::size_t i = 0; i < max; ++i) { if (!in(off + i, 1)) return false; char c = char(p[off + i]); if (!c) return true; out += c; }
        return false;
    }
};
inline void add(result& r, std::string s) { if (!s.empty() && std::ranges::find(r.names, s) == r.names.end()) r.names.push_back(std::move(s)); }

inline result elf(const view& v) {
    result r; const bool is64 = v.p[4] == 2;
    if (v.p[5] != 1) { r.error = "big-endian ELF is not read"; return r; }
    std::uint64_t phoff; std::uint16_t phentsize, phnum;
    if (is64) { std::uint64_t o; if (!v.get(32, o)) { r.error = "truncated ELF header"; return r; } phoff = o; v.get(54, phentsize); v.get(56, phnum); }
    else      { std::uint32_t o; if (!v.get(28, o)) { r.error = "truncated ELF header"; return r; } phoff = o; v.get(42, phentsize); v.get(44, phnum); }
    struct seg { std::uint64_t vaddr, off, filesz; };
    std::vector<seg> loads; std::uint64_t dynoff = 0, dynsz = 0; bool haveDyn = false;
    for (unsigned i = 0; i < phnum; ++i) {
        const std::uint64_t at = phoff + std::uint64_t(i) * phentsize; std::uint32_t type;
        if (!v.get(at, type)) { r.error = "truncated program headers"; return r; }
        std::uint64_t off, vaddr, filesz;
        if (is64) { std::uint64_t a, b, c; if (!v.get(at + 8, a) || !v.get(at + 16, b) || !v.get(at + 32, c)) { r.error = "truncated program headers"; return r; } off = a; vaddr = b; filesz = c; }
        else { std::uint32_t a, b, c; if (!v.get(at + 4, a) || !v.get(at + 8, b) || !v.get(at + 16, c)) { r.error = "truncated program headers"; return r; } off = a; vaddr = b; filesz = c; }
        if (type == 1) loads.push_back({vaddr, off, filesz});
        if (type == 2) { dynoff = off; dynsz = filesz; haveDyn = true; }
    }
    if (!haveDyn) return r;                                  // statically linked: no dependencies
    auto to_off = [&](std::uint64_t va, std::uint64_t& out) { for (auto& s : loads) if (va >= s.vaddr && va < s.vaddr + s.filesz) { out = va - s.vaddr + s.off; return true; } return false; };
    std::uint64_t strtab = 0; std::vector<std::uint64_t> needed;
    const std::size_t esz = is64 ? 16 : 8;
    for (std::uint64_t o = 0; o + esz <= dynsz; o += esz) {
        std::uint64_t tag, val;
        if (is64) { if (!v.get(dynoff + o, tag) || !v.get(dynoff + o + 8, val)) { r.error = "truncated dynamic section"; return r; } }
        else { std::uint32_t a, b; if (!v.get(dynoff + o, a) || !v.get(dynoff + o + 4, b)) { r.error = "truncated dynamic section"; return r; } tag = a; val = b; }
        if (tag == 0) break; if (tag == 1) needed.push_back(val); if (tag == 5) strtab = val;
    }
    std::uint64_t soff; if (needed.empty()) return r;
    if (!to_off(strtab, soff)) { r.error = "dynamic string table is outside the file"; return r; }
    for (auto n : needed) { std::string s; if (!v.cstr(soff + n, s)) { r.error = "bad dependency name"; return r; } add(r, s); }
    return r;
}
inline result pe(const view& v) {
    result r; std::uint32_t lfanew, sig;
    if (!v.get(0x3c, lfanew) || !v.get(lfanew, sig) || sig != 0x00004550) { r.error = "not a PE file"; return r; }
    std::uint16_t nsec, optsz, magic;
    if (!v.get(lfanew + 6, nsec) || !v.get(lfanew + 20, optsz) || !v.get(lfanew + 24, magic)) { r.error = "truncated PE header"; return r; }
    const std::uint64_t opt = lfanew + 24, dirs = opt + (magic == 0x20b ? 112 : 96);
    if (magic != 0x20b && magic != 0x10b) { r.error = "unknown PE optional header"; return r; }
    struct sec { std::uint32_t va, vsize, raw, rawsz; }; std::vector<sec> secs; const std::uint64_t st = opt + optsz;
    for (unsigned i = 0; i < nsec; ++i) { sec s; const std::uint64_t a = st + std::uint64_t(i) * 40;
        if (!v.get(a + 8, s.vsize) || !v.get(a + 12, s.va) || !v.get(a + 16, s.rawsz) || !v.get(a + 20, s.raw)) { r.error = "truncated section table"; return r; } secs.push_back(s); }
    auto rva = [&](std::uint32_t x, std::uint64_t& out) { for (auto& s : secs) { const std::uint32_t len = std::max(s.vsize, s.rawsz); if (x >= s.va && x - s.va < len) { out = std::uint64_t(x - s.va) + s.raw; return true; } } return false; };
    auto walk = [&](unsigned index, std::size_t entry, std::size_t nameAt, bool delay) -> bool {
        std::uint32_t dva, dsz; if (!v.get(dirs + std::uint64_t(index) * 8, dva) || !v.get(dirs + std::uint64_t(index) * 8 + 4, dsz)) return true;   // no such directory
        if (!dva) return true; std::uint64_t base; if (!rva(dva, base)) return false;
        for (std::uint64_t o = 0;; o += entry) {
            std::uint32_t nm = 0, probe = 0; if (!v.get(base + o + nameAt, nm) || !v.get(base + o, probe)) return false;
            if (!nm && (!delay ? true : !probe)) break; if (!nm) break;
            std::uint64_t so; std::string s; if (!rva(nm, so) || !v.cstr(so, s, 256)) return false; add(r, s);
        } return true; };
    if (!walk(1, 20, 12, false) || !walk(13, 32, 4, true)) r.error = "import table is outside the file";
    return r;
}
inline result macho_one(const view& v, std::uint64_t base) {
    result r; std::uint32_t magic; if (!v.get(base, magic)) { r.error = "truncated Mach-O header"; return r; }
    const bool is64 = magic == 0xfeedfacf; if (!is64 && magic != 0xfeedface) { r.error = "not a little-endian Mach-O"; return r; }
    std::uint32_t ncmds; if (!v.get(base + 16, ncmds)) { r.error = "truncated Mach-O header"; return r; }
    std::uint64_t at = base + (is64 ? 32 : 28);
    for (std::uint32_t i = 0; i < ncmds; ++i) {
        std::uint32_t cmd, size; if (!v.get(at, cmd) || !v.get(at + 4, size) || size < 8) { r.error = "truncated load commands"; return r; }
        if (cmd == 0xc || cmd == 0x80000018 || cmd == 0x8000001f || cmd == 0x80000023 || cmd == 0x20) {   // not LC_ID_DYLIB (0xd): that is the file's own name
            std::uint32_t nameoff; std::string s;
            if (!v.get(at + 8, nameoff) || nameoff >= size || !v.cstr(at + nameoff, s)) { r.error = "bad dylib command"; return r; } add(r, s); }
        at += size;
    }
    return r;
}
inline result macho(const view& v) {
    std::uint32_t magic; v.get(0, magic, true);
    if (magic != 0xcafebabe && magic != 0xcafebabf) return macho_one(v, 0);
    const bool f64 = magic == 0xcafebabf; std::uint32_t n; if (!v.get(4, n, true) || n > 64) { result r; r.error = "bad fat header"; return r; }
    result all;
    for (std::uint32_t i = 0; i < n; ++i) { const std::uint64_t a = 8 + std::uint64_t(i) * (f64 ? 32 : 20); std::uint64_t off;
        if (f64) { if (!v.get(a + 8, off, true)) { all.error = "truncated fat header"; return all; } } else { std::uint32_t o; if (!v.get(a + 8, o, true)) { all.error = "truncated fat header"; return all; } off = o; }
        auto one = macho_one(v, off); if (!one.ok()) { all.error = one.error; return all; } for (auto& s : one.names) add(all, s); }
    return all;
}
} // namespace detail
inline result needed(const std::string& path) {
    std::ifstream in(path, std::ios::binary); if (!in) return {{}, "cannot open " + path};
    std::string data((std::istreambuf_iterator<char>(in)), {}); detail::view v{reinterpret_cast<const unsigned char*>(data.data()), data.size()};
    if (v.n >= 20 && std::memcmp(v.p, "\x7f""ELF", 4) == 0) return detail::elf(v);
    if (v.n >= 2 && v.p[0] == 'M' && v.p[1] == 'Z') return detail::pe(v);
    if (v.n >= 8) { std::uint32_t m; v.get(0, m); std::uint32_t mb; v.get(0, mb, true);
        if (m == 0xfeedface || m == 0xfeedfacf || mb == 0xcafebabe || mb == 0xcafebabf) return detail::macho(v); }
    return {{}, "unrecognised binary format"};
}
}
