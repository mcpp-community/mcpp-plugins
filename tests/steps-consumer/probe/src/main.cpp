// steps-probe -- the program the step programs of tests/steps-consumer run, and
// the check of what they leave.
//
//   steps-probe args <file> <argument>...      writes each argument as `<bytes>:<argument>\n`
//   steps-probe cwd <file>                     writes the directory it runs in
//   steps-probe exit <status>                  exits with the status
//   steps-probe mode <octal> <path>...         sets the permissions of each path
//   steps-probe verify <root> <report> <runs>  checks the tree under <root>; when
//                                              every check holds, writes <report>
//                                              and appends a line to <runs>
//
// Its arguments are read in UTF-8 through mcpplibs.os, so on Windows it records
// what the runner passed rather than the code page's rendering of it.
import std;
import mcpplibs.os;

namespace {

namespace os = mcpplibs::os;
namespace fs = std::filesystem;

fs::path path_of(std::string_view utf8) { return os::fs::from_utf8(utf8); }

void say(std::string_view text) { (void)os::stdio::write_error(text); }

std::string counted(const std::vector<std::string>& values) {
    std::string out;
    for (auto const& v : values) out += std::format("{}:{}\n", v.size(), v);
    return out;
}

// What build.mcpp passes, stated here again on purpose: an expectation that
// travelled the channel it checks would agree with whatever arrived. `ü 漢` is
// written as its UTF-8 bytes, so the source's encoding cannot alter it.
const std::string han = "\xC3\xBC \xE6\xBC\xA2";
const std::vector<std::string> fixed = { "a b", "c;d", "e\\f", "g h\\", "\"q\"", "", han };

int verify(const std::string& root, const std::string& report, const std::string& runs) {
    int checks = 0;
    std::vector<std::string> failed;
    const auto expect = [&](bool holds, std::string what) {
        ++checks;
        if (!holds) failed.push_back(std::move(what));
    };
    const auto at = [&](std::string_view rel) { return path_of(root + "/" + std::string(rel)); };
    const auto file = [&](std::string_view rel) { std::error_code ec; return fs::is_regular_file(at(rel), ec); };
    const auto dir = [&](std::string_view rel) { std::error_code ec; return fs::is_directory(at(rel), ec); };
    const auto absent = [&](std::string_view rel) {
        std::error_code ec;
        return !fs::exists(fs::symlink_status(at(rel), ec));
    };
    const auto text = [&](std::string_view rel) {
        const auto r = os::fs::read(at(rel));
        return r ? *r : std::string("(unreadable)");
    };
    const auto time = [&](std::string_view rel) {
        std::error_code ec;
        return fs::last_write_time(at(rel), ec);
    };
    const auto mode = [&](std::string_view rel) {
        std::error_code ec;
        return fs::symlink_status(at(rel), ec).permissions();
    };
    const auto link = [&](std::string_view rel) {
        std::error_code ec;
        return fs::is_symlink(fs::symlink_status(at(rel), ec));
    };

    for (const char* rel : { "src/a.txt", "src/b.txt", "src/c.log", "src/sub/d.txt", "src/platform/ios/App/e.txt" })
        expect(file(rel), std::format("touch did not create {}", rel));
    expect(dir("src/empty"), "mkdir did not create src/empty");
    expect(absent("doomed"), "remove left the tree doomed/, whose directory inner/ and file are read-only");

    // glob (sorted, spelled from the pattern's directory), append, and run
    // with a list after its own arguments: every value as it was passed. A
    // flat pattern passes over the link src/linked.txt, to a directory; a
    // recursive one matches it by its name and does not enter it.
    std::vector<std::string> flat = fixed;
    flat.push_back(root + "/src/a.txt");
    flat.push_back(root + "/src/b.txt");
    expect(text("args-flat.txt") == counted(flat), "run did not pass its arguments and the list 'flat' verbatim:\n"
           + text("args-flat.txt"));
    const std::vector<std::string> deep = { "a.txt", "b.txt", "linked.txt", "ro/f.txt", "sub/d.txt", "x y;z", han };
    expect(text("args-deep.txt") == counted(deep), "a recursive, relative glob with an exclusion, then append, "
           "did not give the list 'deep':\n" + text("args-deep.txt"));
    expect(text("args-picked.txt") == counted({ root + "/src/b.txt" }),
           "a negated set, `?` and a range did not pick src/b.txt alone:\n" + text("args-picked.txt"));
    // A name is matched without case where the host compares names without it,
    // and is spelled as the directory holds it.
    const std::vector<std::string> cased = os::conventions().case_insensitive_names
        ? std::vector<std::string>{ root + "/src/a.txt" } : std::vector<std::string>{};
    expect(text("args-cased.txt") == counted(cased), "A.TXT was not matched as this host compares names:\n"
           + text("args-cased.txt"));
    {
        std::string cwd = text("src/sub/cwd.txt");
        std::error_code ec;
        expect(fs::equivalent(path_of(cwd), at("src/sub"), ec), "run did not start in its working_directory: " + cwd);
    }

    expect(file("copy/deep/args.txt") && text("copy/deep/args.txt") == text("args-flat.txt"),
           "copy_file did not copy args-flat.txt into a directory it made");
    expect(text("copy/ro.txt") == text("args-flat.txt") && mode("copy/ro.txt") == mode("args-flat.txt"),
           "copy_file did not replace a read-only file, or did not give it its source's permissions");
    expect(file("tree/c.log") && file("tree/sub/d.txt") && file("tree/platform/ios/App/e.txt") && dir("tree/empty"),
           "copy_tree did not copy the contents of src/");
    expect(time("tree/c.log") == time("src/c.log"), "copy_tree did not keep a file's time");
    expect(file("tree/ro/f.txt") && mode("tree/ro/f.txt") == mode("src/ro/f.txt") && mode("tree/ro") == mode("src/ro"),
           "copy_tree, twice, did not keep the permissions of src/ro/ and of its read-only file");
    expect(link("tree/linked.txt"), "copy_tree did not keep src/linked.txt as a link, or remove_glob of tree/*.txt "
           "removed that link, to a directory, which a flat pattern passes over");
    expect(absent("tree/a.txt") && absent("tree/b.txt") && file("tree/sub/d.txt"),
           "remove_glob did not remove tree/*.txt alone");
    expect(absent("tree2"), "copy_tree with if_exists copied a source that is not there");
    expect(file("tree3/d.txt"), "copy_tree with if_exists skipped a source that is there");
    expect(file("globbed/renamed.txt") && file("globbed/b.txt"), "copy_glob and rename did not give globbed/");
    expect(absent("globbed/a.txt") && absent("globbed/c.log") && absent("globbed/d.txt") && absent("globbed/linked.txt"),
           "copy_glob copied what its pattern does not match, or rename left its source");
    expect(file("link/a.txt"), "symlink did not make a link to src/");

    if (!failed.empty()) {
        for (auto const& f : failed) say("steps-probe: " + f + "\n");
        return 1;
    }
    const auto written = os::fs::write(path_of(report), std::format("verified {} checks\n", checks));
    const auto counted_run = os::fs::append(path_of(runs), "run\n");
    if (!written || !counted_run) {
        say("steps-probe: cannot write the report\n");
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    const auto args = os::env::arguments();
    if (args.size() >= 3 && args[1] == "args") {
        const std::vector<std::string> passed(args.begin() + 3, args.end());
        return os::fs::write(path_of(args[2]), counted(passed)) ? 0 : 1;
    }
    if (args.size() == 3 && args[1] == "cwd")
        return os::fs::write(path_of(args[2]), os::fs::generic_utf8(fs::current_path())) ? 0 : 1;
    if (args.size() == 3 && args[1] == "exit") return std::atoi(args[2].c_str());
    if (args.size() >= 4 && args[1] == "mode") {
        const auto bits = static_cast<fs::perms>(std::stoi(args[2], nullptr, 8));
        for (std::size_t i = 3; i < args.size(); ++i) {
            std::error_code ec;
            fs::permissions(path_of(args[i]), bits, ec);
            if (ec) {
                say("steps-probe: cannot set the permissions of " + args[i] + ": " + ec.message() + "\n");
                return 1;
            }
        }
        return 0;
    }
    if (args.size() == 5 && args[1] == "verify") return verify(args[2], args[3], args[4]);
    say("usage: steps-probe args <file> <argument>... | cwd <file> | exit <status> | mode <octal> <path>... "
        "| verify <root> <report> <runs>\n");
    return 2;
}
