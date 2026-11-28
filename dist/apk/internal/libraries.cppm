// mcpp.dist.apk.internal.libraries -- digests, the Maven lock and AAR unpacking.

export module mcpp.dist.apk.internal.libraries;

import std;
import mcpp;
import mcpp.plugins;
import mcpp.dist.apk.internal.util;

namespace mcpp::dist::apk {

inline std::string sha256_hex(std::string_view data);

inline std::string sha256_file(const std::string& path) {
    return is_file(path) ? sha256_hex(read_file(path)) : std::string();
}

// ─── JSON ─────────────────────────────────────────────────────────────────
//
// coursier's report is read with the collection's shared reader, the one
// `mcpp::plugins::graph` reads the engine's graph document with.
using json_value = mcpp::plugins::json::value;
using mcpp::plugins::json::parse_json;

// ─── The Maven lock ───────────────────────────────────────────────────────

// One line per fact, sorted, so that a diff of a lock says what changed:
//
//   request <group:artifact:version>        what the project asked for
//   repository <url>                        where resolution looked, in order
//   artifact <coordinate> <aar|jar> <path> <sha256>
//
// `<path>` is relative to the cache root, which is where coursier keeps the
// file under `<scheme>/<host>/<path>` (`--cache` is always passed, so the root
// this member computes and the one coursier writes to are the same directory).
struct maven_artifact {
    std::string coordinate;
    std::string type;
    std::string path;
    std::string sha256;
};

struct maven_lock {
    bool                        found = false;
    std::vector<std::string>    requests;
    std::vector<std::string>    repositories;
    std::vector<maven_artifact> artifacts;
};

inline maven_lock read_maven_lock(const std::string& path) {
    maven_lock lock;
    if (!is_file(path)) return lock;
    lock.found = true;
    std::istringstream in(read_file(path));
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        std::string kind;
        fields >> kind;
        if (kind == "request")    { std::string v; fields >> v; lock.requests.push_back(v); }
        else if (kind == "repository") { std::string v; fields >> v; lock.repositories.push_back(v); }
        else if (kind == "artifact") {
            maven_artifact a;
            fields >> a.coordinate >> a.type >> a.path >> a.sha256;
            lock.artifacts.push_back(std::move(a));
        }
    }
    return lock;
}

inline std::string render_maven_lock(const maven_lock& lock) {
    std::string out =
        "# mcpp.dist.apk Maven lock. Written by MCPP_DIST_APK_MAVEN=update; commit it.\n"
        "# An ordinary build reads only these artifacts, from the cache, and checks each digest.\n";
    for (auto const& r : lock.requests)     out += "request " + r + "\n";
    for (auto const& r : lock.repositories) out += "repository " + r + "\n";
    for (auto const& a : lock.artifacts)
        out += "artifact " + a.coordinate + " " + a.type + " " + a.path + " " + a.sha256 + "\n";
    return out;
}

// Where coursier's cache is: the option, then `COURSIER_CACHE`, then coursier's
// own default on this host, so a developer's `cs` and this member share one.
inline std::string maven_cache_root(const std::string& option) {
    if (!option.empty()) return option;
    if (const char* env = std::getenv("COURSIER_CACHE"); env && *env) return env;
    const char* home = std::getenv("HOME");
    const std::string h = home ? home : "";
    if (std::string(mcpp::host()).find("apple") != std::string::npos ||
        std::string(mcpp::host()).find("macos") != std::string::npos)
        return h + "/Library/Caches/Coursier/v1";
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) return std::string(xdg) + "/coursier/v1";
    return h + "/.cache/coursier/v1";
}

// `group:artifact:version`, three non-empty parts and a fixed version: a range
// would make the lock the only place the version is decided.
inline bool plain_coordinate(const std::string& c) {
    int colons = 0;
    for (std::size_t i = 0; i < c.size(); ++i) {
        if (c[i] == ':') ++colons;
        if (c[i] == '[' || c[i] == '(' || c[i] == ',' || c[i] == '+' || c[i] == ' ') return false;
    }
    return colons == 2 && !c.starts_with(":") && !c.ends_with(":") && c.find("::") == std::string::npos;
}

// coursier names an AAR-packaged module `group:artifact:aar:version`; the lock
// records the coordinate a project writes.
inline std::string plain_form(const std::string& coord) {
    return replace_all_copy(coord, ":aar:", ":");
}

// ─── An AAR, unpacked ─────────────────────────────────────────────────────

// What an Android library archive contributes: its manifest (merged), its
// `res/` (linked under its own package), its classes (`classes.jar`, and any
// `libs/*.jar` it carries), its native libraries (`jni/<abi>/`) and its
// `assets/`. `R.txt`, `proguard.txt`, `lint.jar` and `public.txt` are not
// read: this member does not shrink, lint or compute non-transitive R classes.
struct library_input {
    std::string              label;
    std::string              package;
    std::string              resources;
    std::string              manifest;
    std::string              assets;
    std::string              jni;
    std::vector<std::string> java;
    std::vector<std::string> kotlin;
    std::vector<std::string> jars;
};

// Unpacked with the JDK's `jar` at plan time, once per archive content: a stamp
// beside the tree records the archive's digest, and a matching stamp skips the
// work on the next plan.
inline bool unpack_aar(const std::string& aar, const fs::path& dest, const std::string& jarTool,
                       library_input& out, std::string& message) {
    const std::string digest = sha256_file(aar);
    if (digest.empty()) {
        message = std::format("mcpp.dist.apk: the AAR {} was not found.", aar);
        return false;
    }
    const fs::path stamp = dest / ".mcpp-aar-sha256";
    if (read_file(stamp.string()) != digest) {
        std::error_code ec;
        fs::remove_all(dest, ec);
        fs::create_directories(dest, ec);
        std::string output;
        const int rc = run_capture({jarTool, "xf", aar}, output, dest);
        if (rc != 0) {
            message = std::format("mcpp.dist.apk: unpacking {} with {} failed ({}): {}", aar, jarTool, rc, output);
            return false;
        }
        write_if_different(stamp, digest);
    }
    const std::string manifest = (dest / "AndroidManifest.xml").string();
    if (!is_file(manifest)) {
        message = std::format("mcpp.dist.apk: {} carries no AndroidManifest.xml, so it is not an AAR.", aar);
        return false;
    }
    xml::node m;
    std::string err;
    if (!xml::parse(read_file(manifest), m, err) || m.name != "manifest") {
        message = std::format("mcpp.dist.apk: the manifest of {} cannot be read: {}", aar, err);
        return false;
    }
    out.package  = xml::attr_of(m, "package");
    out.manifest = manifest;
    if (is_dir((dest / "res").string()) && dir_has_files((dest / "res").string()))       out.resources = (dest / "res").string();
    if (is_dir((dest / "assets").string()) && dir_has_files((dest / "assets").string())) out.assets = (dest / "assets").string();
    if (is_dir((dest / "jni").string()))                                                  out.jni = (dest / "jni").string();
    if (is_file((dest / "classes.jar").string())) out.jars.push_back((dest / "classes.jar").string());
    for (auto const& j : files_with_extension((dest / "libs").string(), ".jar")) out.jars.push_back(j);
    return true;
}
} // namespace mcpp::dist::apk

module :private;

namespace mcpp::dist::apk {
// ─── SHA-256 ───────────────────────────────────────────────────────────────

// FIPS 180-4, over bytes held in memory. Here for one purpose: a Maven lock
// records each artifact's digest, and a build that reads a cached artifact
// checks it against the lock before the file reaches a package. The inputs are
// libraries of a few megabytes, so reading a file whole is the simple choice.
inline std::string sha256_hex(std::string_view data) {
    static constexpr std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string msg(data);
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8u;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) msg.push_back('\0');
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<char>((bits >> (i * 8)) & 0xffu));
    const auto rotr = [](std::uint32_t x, int n) -> std::uint32_t { return (x >> n) | (x << (32 - n)); };
    for (std::size_t off = 0; off < msg.size(); off += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            const auto byte = [&](int j) {
                return static_cast<std::uint32_t>(static_cast<unsigned char>(msg[off + static_cast<std::size_t>(i * 4 + j)]));
            };
            w[i] = (byte(0) << 24) | (byte(1) << 16) | (byte(2) << 8) | byte(3);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::string hex;
    hex.reserve(64);
    for (int i = 0; i < 8; ++i) hex += std::format("{:08x}", h[i]);
    return hex;
}
} // namespace mcpp::dist::apk

