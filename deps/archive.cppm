// mcpp.deps.archive -- files a program reads at run time, from an archive the
// project keeps.
//
//   import mcpp.deps.archive;
//
//   mcpp::deps::archive::options o;
//   o.archive = "assets/python-embed.zip";
//   o.to      = "runtime";                  // beside the program
//   if (!mcpp::deps::archive::unpack(o)) return 1;
//
// The archive is extracted by an action, and every file it holds is placed
// beside the program (`mcpp::deploy`): `mcpp run` finds the files there, and
// `mcpp pack` carries them. A project that lays out a directory of its own
// takes `result::files`, the extracted copies, as the inputs of its own
// actions.
//
// THE FILE NAMES ARE READ WHEN THE BUILD PROGRAM RUNS, THE CONTENT ARRIVES WHEN
// THE ACTION DOES. A zip archive lists its members in a central directory at
// its end, which is read without extracting anything; the action therefore
// names each file it writes as an output (SPEC-007 R3.2), and each output can
// be deployed. The archive is a file of the project, so reading its listing is
// configuration, not a construction result (R1.3); the listing is read again
// when the archive changes.
//
// Only zip is listed: a compressed tar has no index, and naming its members
// would mean decompressing it while planning. The extraction itself is CMake's
// `-E tar`, from the `xim:cmake` payload this feature declares.

export module mcpp.deps.archive;

import std;
import mcpp;
import mcpp.plugins;
import mcpp.deps;
import mcpp.plugins.fs;

export namespace mcpp::deps::archive {

struct options {
    // The archive, relative to the package root. A `.zip`.
    std::string archive;
    // The directory beside the program the archive's tree is placed in. Empty
    // or `.` is the program's own directory.
    std::string to;
    // Names the action and the directory the archive is extracted into. Empty
    // takes the archive's file name without its extension.
    std::string name;
    // The `cmake` executable (0.19.0: a `tool::choice`, so a path still
    // assigns). Default: the `xim:cmake` payload, or an override of it.
    mcpp::plugins::tool::choice cmake;
};

struct result {
    // One entry per file of the archive: the extracted copy and its path
    // beside the program.
    std::vector<mcpp::plugins::fs::deployed_file> files;
    bool ok = false;
    explicit operator bool() const { return ok; }
};

// The member names of a zip archive, directories left out, or an error. Reads
// the end-of-central-directory record and the central directory, nothing
// else; a ZIP64 archive (4 GiB and beyond, or 65 535 members and beyond) is
// refused by name rather than misread.
inline std::expected<std::vector<std::string>, std::string>
zip_members(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected("cannot be opened");
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::uint64_t>(in.tellg());
    if (size < 22) return std::unexpected("is not a zip archive (too short)");
    const std::uint64_t tail = std::min<std::uint64_t>(size, 22 + 65535);
    std::string buf(tail, '\0');
    in.seekg(static_cast<std::streamoff>(size - tail));
    in.read(buf.data(), static_cast<std::streamsize>(tail));

    auto u16 = [](const std::string& b, std::size_t at) {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(b[at])) |
               static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 1])) << 8;
    };
    auto u32 = [&](const std::string& b, std::size_t at) {
        return u16(b, at) | u16(b, at + 2) << 16;
    };

    std::size_t eocd = std::string::npos;
    for (std::size_t i = tail - 22 + 1; i-- > 0; )
        if (u32(buf, i) == 0x06054b50u) { eocd = i; break; }
    if (eocd == std::string::npos) return std::unexpected("is not a zip archive (no central directory)");

    const std::uint32_t count  = u16(buf, eocd + 10);
    const std::uint32_t cdSize = u32(buf, eocd + 12);
    const std::uint32_t cdOff  = u32(buf, eocd + 16);
    if (count == 0xFFFFu || cdSize == 0xFFFFFFFFu || cdOff == 0xFFFFFFFFu)
        return std::unexpected("is a ZIP64 archive, which this member does not list");
    if (static_cast<std::uint64_t>(cdOff) + cdSize > size)
        return std::unexpected("has a central directory beyond its end");

    std::string cd(cdSize, '\0');
    in.seekg(cdOff);
    in.read(cd.data(), static_cast<std::streamsize>(cdSize));
    if (!in) return std::unexpected("cannot be read");

    std::vector<std::string> names;
    std::size_t at = 0;
    for (std::uint32_t n = 0; n < count; ++n) {
        if (at + 46 > cd.size() || u32(cd, at) != 0x02014b50u)
            return std::unexpected("has a malformed central directory");
        const std::size_t nameLen = u16(cd, at + 28), extraLen = u16(cd, at + 30),
                          commentLen = u16(cd, at + 32);
        if (at + 46 + nameLen > cd.size()) return std::unexpected("has a malformed central directory");
        std::string name = cd.substr(at + 46, nameLen);
        at += 46 + nameLen + extraLen + commentLen;
        for (std::size_t i = 0; i < name.size(); ++i) if (name[i] == '\\') name[i] = '/';
        if (name.empty() || name.back() == '/') continue;   // a directory
        names.push_back(std::move(name));
    }
    return names;
}

inline result unpack(const options& opt) {
    namespace fs = std::filesystem;
    constexpr std::string_view who = "mcpp.deps.archive";
    mcpp::fact("mcpp.plugins", std::string(mcpp::plugins::version).c_str());

    // THE ARCHIVE. A missing or unreadable one is a mistake in the project,
    // not a state of the machine, so it is refused.
    std::error_code ec;
    const fs::path archive = mcpp::deps::absolute_from_root(opt.archive);
    if (opt.archive.empty() || !fs::is_regular_file(archive, ec)) {
        std::cerr << std::format("{}: options::archive must name a file; got '{}'.\n", who, opt.archive);
        return {};
    }
    mcpp::rerun_if_changed(mcpp::deps::generic(archive).c_str());
    auto members = zip_members(archive);
    if (!members) {
        std::cerr << std::format("{}: {} {}.\n", who, mcpp::deps::generic(archive), members.error());
        return {};
    }
    // A member that would land outside the extraction directory is refused:
    // the path it names is the path it is written to.
    for (auto const& m : *members) {
        const fs::path rel = fs::path(m).lexically_normal();
        if (rel.is_absolute() || rel.has_root_name() || rel.empty() || *rel.begin() == "..") {
            std::cerr << std::format("{}: {} holds '{}', which leaves the directory it is "
                                     "extracted into.\n", who, mcpp::deps::generic(archive), m);
            return {};
        }
    }

    const std::string name = opt.name.empty() ? archive.stem().string() : opt.name;
    const fs::path into = fs::path(mcpp::out_dir()) / "deps-archive" / name;
    const std::string dir = opt.to.empty() ? std::string(".") : opt.to;

    result r;
    r.ok = true;
    for (auto const& m : *members) {
        const fs::path rel = fs::path(m).lexically_normal();
        r.files.push_back({mcpp::deps::generic(into / rel),
                           mcpp::deps::generic(fs::path(dir) / rel)});
    }

    // ── the tool ──
    const auto tool = mcpp::plugins::tool::resolve(mcpp::deps::cmake_spec("mcpp.deps.archive"),
                                                   opt.cmake);
    const std::string cmake = tool.program;
    if (cmake.empty()) {
        // Nothing is extracted, so nothing can be deployed: a deployed file
        // must be an output of some action. A requested payload is installed
        // and this program runs again; anything else is reported.
        if (!tool.pending())
            mcpp::deps::warn(mcpp::plugins::tool::describe_missing(
                mcpp::deps::cmake_spec("mcpp.deps.archive"), tool)
                + std::format("\n  So this plan extracts nothing from {}.",
                              mcpp::deps::generic(archive)));
        r.files.clear();
        return r;
    }
    // ── the extraction, as an edge ──
    // `cmake -P` over a script this program writes: the directory is emptied
    // first -- the action names every member of the archive as an output, and
    // a file left from a previous archive would be one it did not name -- and
    // `ARCHIVE_EXTRACT ... TOUCH` (CMake 3.24+) gives each file the time of
    // extraction, so the outputs are newer than the archive.
    using mcpp::deps::bracket;
    const std::string id     = "deps-archive:" + name;
    const std::string desc   = "UNPACK " + archive.filename().string();
    const std::string arc    = mcpp::deps::generic(archive);
    const std::string dst    = mcpp::deps::generic(into);
    const fs::path    script = fs::path(mcpp::out_dir()) / "deps-archive" / (name + ".cmake");
    mcpp::plugins::fs::write_if_changed(script,
        "# Written by mcpp.deps.archive: extract " + archive.filename().string() + ".\n"
        "file(REMOVE_RECURSE " + bracket(dst) + ")\n"
        "file(MAKE_DIRECTORY " + bracket(dst) + ")\n"
        "file(ARCHIVE_EXTRACT INPUT " + bracket(arc) + " DESTINATION " + bracket(dst) + " TOUCH)\n");
    const std::string scriptS = mcpp::deps::generic(script);
    mcpp::action a;
    a.id          = id.c_str();
    a.role        = mcpp::roles::artifact;
    a.description = desc.c_str();
    a.arg(cmake.c_str()).arg("-P").arg(scriptS.c_str())
     .input(cmake.c_str()).input(scriptS.c_str()).input(arc.c_str());
    for (auto const& f : r.files) a.output(f.path.c_str());
    a.submit();

    // ── each file, beside the program ──
    for (auto const& f : r.files) {
        const std::string parent = fs::path(f.to).parent_path().generic_string();
        mcpp::deploy(f.path.c_str(), parent.empty() ? "." : parent.c_str());
    }
    return r;
}

} // namespace mcpp::deps::archive
