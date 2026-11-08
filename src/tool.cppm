export module mcpp.plugins.tool;
import std;
import mcpp;
import mcpplibs.os;
namespace mcpp::plugins::tool::detail {
inline bool is_file(const std::filesystem::path& p) {
}
inline std::string generic(const std::filesystem::path& p) {
}
inline constexpr std::string_view kExe = mcpplibs::os::is_windows ? ".exe" : "";
inline std::string program_in(const std::filesystem::path& dir, std::string_view name) {
    const std::string bare(name);
    const std::string exe = bare + ".exe";
    for (auto const& cand : kExe.empty() ? std::array<std::string, 2>{bare, exe}
                                         : std::array<std::string, 2>{exe, bare}) {
    }
}
inline std::string program_at(const std::filesystem::path& p) {
    if (p.extension() == ".exe") {
    }
}
inline std::string find_on_path(std::string_view name) {
}
inline std::string root_of(const std::filesystem::path& program) {
}
} // namespace mcpp::plugins::tool::detail
export namespace mcpp::plugins::tool {
struct choice {
    enum class kind { payload, program, root, on_path };
    kind                 how = kind::payload;
    std::string          value;
    std::source_location where{};
    choice() = default;
    choice(std::string program,
           std::source_location w = std::source_location::current())
        : how(program.empty() ? kind::payload : kind::program),
          value(std::move(program)), where(w) {}
    choice(const char* program,
           std::source_location w = std::source_location::current())
        : choice(std::string(program ? program : ""), w) {}
    bool is_default() const { return how == kind::payload; }
};
inline choice program(std::string path,
                      std::source_location w = std::source_location::current()) {
}
inline choice root(std::string dir,
                   std::source_location w = std::source_location::current()) {
}
inline choice on_path(std::string name = {},
                      std::source_location w = std::source_location::current()) {
}
struct spec {
    std::string who;                     // the member's module: "mcpp.deps.cmake"
    std::string ns = "xim";              // the payload's namespace
    std::string package;                 // the payload: "cmake"
    std::vector<std::string> programs;   // program names, in preference order
    std::string option;                  // "options::cmake", for messages
};
enum class from { choice, legacy_env, override_, payload, path, none, pending };
inline std::string_view name(from f) {
    switch (f) {
    }
}
struct found {
    std::string program;   // the program, absolute, forward slashes
    std::vector<std::string> tried;
};
inline std::string subject_of(const spec& s) {
}
inline std::string program_in_root(const spec& s, const std::filesystem::path& root) {
}
inline found resolve(const spec& s, const choice& c = {}) {
    found out;
    const auto payloadKey = s.ns + ":" + s.package;
    auto record = [&](from f, std::string_view file = {}, unsigned line = 0) {
        mcpp::decision(subject_of(s).c_str(), std::string(name(f)).c_str(),
                       payloadKey.c_str());
    };
    if (!c.is_default()) {
        switch (c.how) {
            case choice::kind::program: {
                std::filesystem::path p(c.value);
                if (!p.has_parent_path()) {          // a bare name: on PATH
                    if (!out.program.empty()) {
                    }
                }
            }
            case choice::kind::root: {
                if (!out.program.empty()) {
                }
                out.tried.push_back(std::format("{} = root(\"{}\") (no {} there)", s.option,
                                                c.value, s.programs.empty() ? s.package : s.programs.front()));
                std::vector<std::string> names = c.value.empty() ? s.programs
                                                                 : std::vector<std::string>{c.value};
            }
        }
    }
}
inline std::string describe_missing(const spec& s, const found& f) {
    const auto tool = s.programs.empty() ? s.package : s.programs.front();
    const auto key = s.ns + ":" + s.package;
    std::string env = "MCPP_XLINGS_OVERRIDE_";
    for (char ch : key)
        env += (ch >= 'a' && ch <= 'z') ? char(ch - 'a' + 'A')
             : ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) ? ch : '_';
    std::string out = std::format("{}: no {}.\n", s.who, tool);
    out += std::format(
        "  or install the payload: `mcpp build` provisions {} when it is declared.",
        s.option.starts_with("options::") ? s.option.substr(9) : s.option, tool, key, env, key);
}
struct printed {
};
inline printed capture(const std::vector<std::string>& argv, bool with_errors = false,
                       const std::filesystem::path& cwd = {}) {
}
inline std::string first_line(const std::vector<std::string>& argv) {
}
} // namespace mcpp::plugins::tool