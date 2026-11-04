// Atomic latest-state writer for action worker programs.
export module mcpp.plugins.progress.writer;
import std;
export import mcpp.plugins.progress;
import mcpplibs.os.fs;
export namespace mcpp::plugins::progress {
class writer {
public:
    writer() {
        if (const auto* file = std::getenv("MCPP_ACTION_PROGRESS_FILE"); file && *file)
            file_ = mcpplibs::os::fs::from_utf8(file);
    }
    bool enabled() const { return !file_.empty(); }
    // At most five atomic latest-state replacements per second. Reporting
    // failure, missing telemetry, and rate limiting do not affect the work.
    bool report(const snapshot& state) {
        if (!enabled() || !valid_text(state.phase, 128) || state.phase.empty()
            || !valid_text(state.detail, 1024)
            || (state.done && state.total && *state.done > *state.total)) return false;
        const auto now = std::chrono::steady_clock::now();
        if (last_ && now - *last_ < std::chrono::milliseconds(200)) return false;
        std::string text = "{\"v\":1,\"seq\":" + std::to_string(seq_)
                         + ",\"phase\":" + quote(state.phase);
        if (state.done) text += ",\"done\":" + std::to_string(*state.done);
        if (state.total) text += ",\"total\":" + std::to_string(*state.total);
        if (!state.detail.empty()) text += ",\"detail\":" + quote(state.detail);
        text += '}';
        // fs's replacement has the same semantics on Linux, macOS and Windows.
        if (!mcpplibs::os::fs::write_atomic(file_, text)) return false;
        ++seq_; last_ = now;
        return true;
    }
private:
    std::filesystem::path file_;
    std::uint64_t seq_ = 0;
    std::optional<std::chrono::steady_clock::time_point> last_;
};
}
