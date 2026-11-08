// Optional action telemetry; useful in worker programs as well as adapters.
export module mcpp.plugins.progress;
import std;

export namespace mcpp::plugins::progress {
struct snapshot {
    std::string phase;
    std::optional<std::uint64_t> done, total;
    std::string detail;
};

// Compile against older mcpp build-program APIs without requiring the field.
// The worker naturally becomes a no-op when the engine supplies no channel.
template<class Action> bool enable(Action& action, bool requested = true) {
    if constexpr (requires(Action& a) { a.progress = true; }) {
        action.progress = requested;
        return requested;
    } else {
        if (requested) std::cerr << "mcpp.plugins.progress: this engine does not support action progress; continuing without telemetry\n";
        return false;
    }
}

inline bool valid_text(std::string_view value, std::size_t limit) {
    return value.size() <= limit && std::ranges::none_of(value, [](unsigned char c) {
        return c < 32 || c == 127;
    });
}
inline std::string quote(std::string_view value) {
    std::string out = "\"";
    for (char c : value) { if (c == '"' || c == '\\') out += '\\'; out += c; }
    return out + '"';
}

}
