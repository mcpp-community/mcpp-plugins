// mcpp-deps -- the command of every installation a `deps-*` member declares.
//
// WHY A PROGRAM AND NOT THE INSTALLER ITSELF AS THE ACTION'S COMMAND.
//
// An `mcpp::action` is an argument vector and nothing else: it carries no
// environment and no working directory (mcpp's docs/30, "Declaring work instead
// of doing it"). vcpkg is configured through its environment -- `VCPKG_ROOT`
// names the tool's scripts, `VCPKG_DISABLE_METRICS` keeps a build from
// reporting home -- and an installation has three more needs no argument
// vector can state:
//
//   - a LOCK on the installation root, because two workspace members that
//     both use one prefix each declare the installation (the engine orders
//     `blocking` actions per package), and two installers writing one tree at
//     once is a corrupt tree;
//   - SHORT scratch directories for vcpkg's build trees, outside the project,
//     because a port's build nests deep and Windows still enforces MAX_PATH on
//     many of the tools a port runs.
//
// This program sets those up and runs the installer as a child, waiting for it
// and returning its status; the action is a `check`, whose stamp mcpp writes
// when the status is 0. It holds no knowledge of any project: every value
// arrives on its command line from the member that planned the action.
//
// Usage:
//   mcpp-deps vcpkg --vcpkg <exe> --root <VCPKG_ROOT> --manifest-root <dir>
//             --install-root <dir> --triplet <t>
//             [--overlay-triplets <dir>]... [-- <extra vcpkg install args>...]
//   mcpp-deps cmake --cmake <exe> --source <dir> --build <dir> --prefix <dir>
//             [--config <Release>] [--generator <g>]
//             [-- <extra configure args>...]
#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/file.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  include <spawn.h>
extern char** environ;
#endif
#include <cstdlib>

import std;

namespace {

namespace fs = std::filesystem;

[[noreturn]] void usage(std::string_view why) {
    std::cerr << "mcpp-deps: " << why << "\n"
              << "usage: mcpp-deps vcpkg --vcpkg <exe> --root <dir> --manifest-root <dir> "
                 "--install-root <dir> --triplet <t> [--overlay-triplets <dir>]... "
                 "[-- <args>...]\n"
              << "       mcpp-deps cmake --cmake <exe> --source <dir> --build <dir> --prefix <dir> "
                 "[--config <c>] [--generator <g>] [-- <args>...]\n";
    std::exit(2);
}

struct args {
    std::map<std::string, std::vector<std::string>> named;
    std::vector<std::string> rest;   // after `--`

    const std::string& one(const std::string& key) const {
        auto it = named.find(key);
        if (it == named.end() || it->second.empty()) usage("missing --" + key);
        return it->second.back();
    }
    std::string opt(const std::string& key, std::string fallback = {}) const {
        auto it = named.find(key);
        return it == named.end() || it->second.empty() ? fallback : it->second.back();
    }
    std::vector<std::string> all(const std::string& key) const {
        auto it = named.find(key);
        return it == named.end() ? std::vector<std::string>{} : it->second;
    }
};

args parse(int argc, char** argv, int from) {
    args a;
    for (int i = from; i < argc; ++i) {
        std::string_view s = argv[i];
        if (s == "--") {
            for (++i; i < argc; ++i) a.rest.emplace_back(argv[i]);
            break;
        }
        if (!s.starts_with("--") || i + 1 >= argc) usage(std::format("unexpected `{}`", s));
        a.named[std::string(s.substr(2))].emplace_back(argv[++i]);
    }
    return a;
}

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

// ── The platform: environment, a child process, a lock ─────────────────────

#if defined(_WIN32)
std::wstring wide(std::string_view s) {
    if (s.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(std::size_t(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

void set_env(const std::string& name, const std::string& value) {
    ::SetEnvironmentVariableW(wide(name).c_str(), value.empty() ? nullptr : wide(value).c_str());
}

// The quoting `CommandLineToArgvW` and the C runtime both undo: backslashes
// are literal unless they precede a quote, and then they are doubled.
std::wstring quote(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (std::size_t i = 0;; ++i) {
        std::size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') { ++i; ++backslashes; }
        if (i == arg.size()) { out.append(backslashes * 2, L'\\'); break; }
        if (arg[i] == L'"') { out.append(backslashes * 2 + 1, L'\\'); out += L'"'; }
        else { out.append(backslashes, L'\\'); out += arg[i]; }
    }
    return out + L"\"";
}

int run(const std::vector<std::string>& argv) {
    std::wstring line;
    for (auto const& a : argv) {
        if (!line.empty()) line += L' ';
        line += quote(wide(a));
    }
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                          &si, &pi)) {
        std::cerr << std::format("mcpp-deps: cannot start {} (error {})\n", argv.front(),
                                 ::GetLastError());
        return 127;
    }
    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    ::GetExitCodeProcess(pi.hProcess, &code);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return int(code);
}

struct file_lock {
    HANDLE h = INVALID_HANDLE_VALUE;
    explicit file_lock(const fs::path& p) {
        h = ::CreateFileW(p.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        OVERLAPPED o{};
        ::LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &o);
    }
    ~file_lock() {
        if (h == INVALID_HANDLE_VALUE) return;
        OVERLAPPED o{};
        ::UnlockFileEx(h, 0, MAXDWORD, MAXDWORD, &o);
        ::CloseHandle(h);
    }
    bool held() const { return h != INVALID_HANDLE_VALUE; }
};
#else
void set_env(const std::string& name, const std::string& value) {
    if (value.empty()) ::unsetenv(name.c_str());
    else ::setenv(name.c_str(), value.c_str(), 1);
}

int run(const std::vector<std::string>& argv) {
    std::vector<char*> cargv;
    for (auto const& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);
    pid_t pid = 0;
    if (::posix_spawnp(&pid, cargv[0], nullptr, nullptr, cargv.data(), environ) != 0) {
        std::cerr << std::format("mcpp-deps: cannot start {}\n", argv.front());
        return 127;
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {}
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

struct file_lock {
    int fd = -1;
    explicit file_lock(const fs::path& p) {
        fd = ::open(p.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd >= 0) ::flock(fd, LOCK_EX);
    }
    ~file_lock() {
        if (fd < 0) return;
        ::flock(fd, LOCK_UN);
        ::close(fd);
    }
    bool held() const { return fd >= 0; }
};
#endif

// FNV-1a: a stable name for a directory derived from a path. Stable across
// runs and compilers, which `std::hash` does not promise.
std::string short_name(std::string_view text) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : text) { h ^= c; h *= 1099511628211ull; }
    return std::format("{:08x}", std::uint32_t(h ^ (h >> 32)));
}

// vcpkg's own per-user directory: where its default binary cache
// (`archives/`) and registry cache (`registries/`) already live (vcpkg's
// "Default binary cache" documentation). Scratch and downloads go beside them,
// so everything vcpkg keeps for a user is in the place vcpkg's own
// documentation sends them to look.
fs::path vcpkg_user_dir() {
#if defined(_WIN32)
    if (auto v = env("LOCALAPPDATA"); !v.empty()) return fs::path(v) / "vcpkg";
    if (auto v = env("APPDATA"); !v.empty()) return fs::path(v) / "vcpkg";
#else
    if (auto v = env("XDG_CACHE_HOME"); !v.empty()) return fs::path(v) / "vcpkg";
    if (auto v = env("HOME"); !v.empty()) return fs::path(v) / ".cache" / "vcpkg";
#endif
    return fs::temp_directory_path() / "vcpkg";
}

// ── vcpkg ──────────────────────────────────────────────────────────────────

int vcpkg_install(const args& a) {
    const fs::path exe          = a.one("vcpkg");
    const fs::path root         = a.one("root");
    const fs::path manifestRoot = a.one("manifest-root");
    const fs::path installRoot  = a.one("install-root");
    const std::string triplet   = a.one("triplet");

    std::error_code ec;
    fs::create_directories(installRoot, ec);
    file_lock lock(installRoot / ".mcpp-deps.lock");
    if (!lock.held())
        std::cerr << "mcpp-deps: could not lock " << installRoot.string()
                  << "; continuing without the lock\n";

    // THE TOOL'S OWN SCRIPTS, NOT WHATEVER `VCPKG_ROOT` THE SHELL HAS. The
    // root this program is given is the standalone bundle published with the
    // tool, so the scripts a port calls are the ones this tool was released
    // with. An inherited `VCPKG_ROOT` naming a clone at another commit would
    // pair this tool with scripts it was not tested against.
    set_env("VCPKG_ROOT", root.string());
    set_env("VCPKG_DISABLE_METRICS", "1");

    const fs::path user = vcpkg_user_dir();
    const fs::path work = user / "mcpp" / short_name(fs::absolute(installRoot).generic_string());

    std::vector<std::string> cmd{
        exe.string(), "install",
        "--triplet", triplet,
        "--x-manifest-root=" + manifestRoot.string(),
        "--x-install-root=" + installRoot.string(),
        "--x-buildtrees-root=" + (work / "bt").string(),
        "--x-packages-root=" + (work / "pk").string(),
        "--clean-buildtrees-after-build",
        "--clean-packages-after-build",
    };
    // Downloads are shared by every project on the machine; a user who has
    // moved them already (`VCPKG_DOWNLOADS`) keeps that.
    if (env("VCPKG_DOWNLOADS").empty())
        cmd.push_back("--downloads-root=" + (user / "downloads").string());
    for (auto const& d : a.all("overlay-triplets")) cmd.push_back("--overlay-triplets=" + d);
    for (auto const& r : a.rest) cmd.push_back(r);

    std::cerr << "mcpp-deps: vcpkg install --triplet " << triplet << " ("
              << manifestRoot.string() << " -> " << installRoot.string() << ")\n";
    const int code = run(cmd);
    if (code != 0) {
        std::cerr << std::format("mcpp-deps: vcpkg install exited {}\n", code);
        return code;
    }
    return 0;
}

// ── CMake ──────────────────────────────────────────────────────────────────

int cmake_install(const args& a) {
    const fs::path cmake  = a.one("cmake");
    const fs::path source = a.one("source");
    const fs::path build  = a.one("build");
    const fs::path prefix = a.one("prefix");
    const std::string config = a.opt("config", "Release");

    std::error_code ec;
    fs::create_directories(build, ec);
    file_lock lock(build / ".mcpp-deps.lock");

    // CONFIGURE EVERY TIME, AND LET CMAKE DECIDE WHAT THAT COSTS. A configure
    // over an existing cache re-runs only what changed, and the arguments may
    // have changed -- which is why this action ran at all.
    std::vector<std::string> configure{
        cmake.string(), "-S", source.string(), "-B", build.string(),
        "-DCMAKE_INSTALL_PREFIX=" + prefix.string(),
        "-DCMAKE_BUILD_TYPE=" + config,
    };
    if (auto g = a.opt("generator"); !g.empty()) { configure.push_back("-G"); configure.push_back(g); }
    for (auto const& r : a.rest) configure.push_back(r);
    std::cerr << "mcpp-deps: cmake configure " << source.string() << "\n";
    if (int code = run(configure); code != 0) {
        std::cerr << std::format("mcpp-deps: cmake configure exited {}\n", code);
        return code;
    }
    std::vector<std::string> install{
        cmake.string(), "--build", build.string(), "--config", config,
        "--target", "install", "--parallel",
    };
    std::cerr << "mcpp-deps: cmake build and install -> " << prefix.string() << "\n";
    if (int code = run(install); code != 0) {
        std::cerr << std::format("mcpp-deps: cmake --build exited {}\n", code);
        return code;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) usage("no subcommand");
    const std::string_view sub = argv[1];
    const args a = parse(argc, argv, 2);
    if (sub == "vcpkg") return vcpkg_install(a);
    if (sub == "cmake") return cmake_install(a);
    usage(std::format("unknown subcommand `{}`", sub));
}
