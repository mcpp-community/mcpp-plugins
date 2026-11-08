export module mcpp.rules.metal;

import std;
import mcpp.plugins.tool;

export namespace mcpp::rules::metal {
struct shader { std::string source; std::string name; std::vector<std::string> defines; };
inline std::string sdk_path(const std::string& sdk) {
    return mcpp::plugins::tool::first_line({"/usr/bin/xcrun", "--sdk", sdk, "--show-sdk-path"});
}
inline bool compile(std::span<const shader> shaders = {}) { return shaders.empty() || !sdk_path("macosx").empty(); }
}
