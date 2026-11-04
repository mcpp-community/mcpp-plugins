// Shared reading of the engine's accelerator requests. Targets are opaque;
// each rule interprets only the member whose backend name it owns.
module;
#include <cctype>
export module mcpp.plugins.accel;
import std;

export namespace mcpp::plugins::accel {
struct request {
    std::string backend;
    std::string version;
    std::vector<std::string> archs;
    std::string portable_floor;
};
std::expected<std::vector<request>, std::string> parse(std::string_view text);
} // namespace mcpp::plugins::accel

namespace mcpp::plugins::accel {
namespace {

std::string_view trim_sv(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

std::expected<std::vector<std::string>, std::string>
read_request_targets(std::string_view s, std::size_t& pos) {
    std::vector<std::string> targets;
    // Empty is the existing unconstrained set, also emitted for a bare name.
    if (pos < s.size() && s[pos] == '}') return targets;
    while (true) {
        const auto begin = pos;
        while (pos < s.size() && s[pos] != ',' && s[pos] != '}') {
            if (s[pos] == '{' || s[pos] == '+') return std::unexpected(std::format(
                "invalid accelerator request at byte {}: invalid target-set delimiter", pos + 1));
            ++pos;
        }
        const auto value = trim_sv(s.substr(begin, pos - begin));
        if (value.empty() || std::ranges::any_of(value, [](unsigned char c) { return std::isspace(c); }))
            return std::unexpected(std::format(
                "invalid accelerator request at byte {}: expected one nonempty target token", begin + 1));
        targets.emplace_back(value);
        if (pos == s.size() || s[pos] == '}') return targets;
        ++pos;
    }
}

} // namespace

std::expected<std::vector<request>, std::string> parse(std::string_view s) {
    std::size_t pos = 0;
    auto spaces = [&] {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) ++pos;
    };
    auto error = [&](std::string_view why) {
        return std::unexpected(std::format("invalid accelerator request at byte {}: {}",
                                           pos + 1, why));
    };
    spaces();
    if (pos == s.size() || trim_sv(s.substr(pos)) == "(none)") return std::vector<request>{};

    std::vector<request> out;
    std::set<std::string> names;
    while (pos < s.size()) {
        spaces();
        const auto begin = pos;
        while (pos < s.size() && std::isalpha(static_cast<unsigned char>(s[pos]))) ++pos;
        if (pos == begin) return error("expected an implementation name");
        request a;
        a.backend = std::string(s.substr(begin, pos - begin));
        if (!names.insert(a.backend).second)
            return error(std::format("implementation '{}' appears more than once; combine its targets in one set",
                                     a.backend));
        const auto version = pos;
        if (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
            while (pos < s.size()) {
                const auto c = static_cast<unsigned char>(s[pos]);
                if (!std::isalnum(c) && c != '.' && c != '-' && c != '_') break;
                ++pos;
            }
        }
        a.version = std::string(s.substr(version, pos - version));
        spaces();
        if (pos < s.size() && s[pos] == '+') {
            ++pos;
            spaces();
            if (pos == s.size() || s[pos] != '{') return error("expected '{' after '+'");
            ++pos;
            spaces();
            auto targets = read_request_targets(s, pos);
            if (!targets) return std::unexpected(targets.error());
            a.archs = std::move(*targets);
            if (pos == s.size() || s[pos] != '}') return error("expected '}' to close the target set");
            ++pos;
            spaces();
        }
        const auto tail = s.substr(pos);
        const std::size_t floorPrefix = tail.starts_with("floor>=") ? 7 : tail.starts_with("ptx>=") ? 5 : 0;
        if (floorPrefix) {
            pos += floorPrefix;
            spaces();
            const auto floor = pos;
            while (pos < s.size() && s[pos] != ',' && s[pos] != ' ' && s[pos] != '\t') {
                if (s[pos] == '{' || s[pos] == '}' || s[pos] == '+') return error("invalid portable-form floor");
                ++pos;
            }
            if (pos == floor) return error("expected a portable-form floor");
            a.portable_floor = std::string(s.substr(floor, pos - floor));
            spaces();
        }
        if (pos < s.size() && s[pos] != ',') return error("unexpected text after the implementation request");
        std::ranges::sort(a.archs);
        a.archs.erase(std::unique(a.archs.begin(), a.archs.end()), a.archs.end());
        out.push_back(std::move(a));
        if (pos == s.size()) break;
        ++pos;
        spaces();
        if (pos == s.size()) return error("expected an implementation after ','");
    }
    std::ranges::sort(out, {}, &request::backend);
    return out;
}


} // namespace mcpp::plugins::accel
