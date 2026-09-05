// mcpp.plugins: the identity unit of the collection.
//
// Every member of this package is a module interface unit under rules/ or
// tools/, compiled as a host module of its own when the consumer's feature
// request names it. This unit is the lib root. It is compiled before every
// member, so a member may import it, and it states the one fact a member may
// want to report about itself: the version of the collection it belongs to.
export module mcpp.plugins;

import std;

export namespace mcpp::plugins {

inline constexpr std::string_view version = "0.1.1";

} // namespace mcpp::plugins
