// mcpp.plugins: the collection's shared library.
//
// Every member of this package is a module interface unit under rules/ or
// tools/, compiled as a host module of its own when the consumer's feature
// request names it. This unit is the lib root: it is compiled before every
// member, so a member may import it.
//
// IT HOLDS WHAT MORE THAN ONE MEMBER NEEDS, AND ONLY THAT.
//
// The lib root is the only unit every member can import. A second unit beside
// it in `[build] sources` is NOT compiled as a host module ahead of the
// members. Measured: a member importing a second lib-root unit failed with
//
//   mcpp.plugins.surface: error: failed to read compiled module
//   note: imports must be built before being imported
//
// because only the lib root is built first. So shared code lives here rather
// than in a file of its own, and a member that needs it writes
// `import mcpp.plugins;`.
export module mcpp.plugins;

import std;

// NO `import mcpp;`. That module exists only inside a build program, and this
// unit has to compile into an ordinary binary too: `mcpp.tools.embed`'s
// executable form is what lets a generated `.S` be an ACTION's output, which
// is the only way the payload it embeds can be a declared input of the edge
// that assembles it. Measured before this: an ordinary build of this package
// failed with `mcpp: failed to read compiled module`.
//
// Everything this unit used to read from that module -- the target OS, whether
// the toolchain has a GAS assembler, the package's name -- is now a parameter.
// The members under rules/ still import it; they are feature-gated, so the
// tool's own build never compiles them.

export namespace mcpp::plugins {

// THIS MUST EQUAL `[package] version` IN mcpp.toml, AND CI CHECKS THAT IT DOES.
//
// It was `0.1.1` while the package was `0.2.6`, which nothing noticed because
// nothing read it. A value that is recorded and never read cannot be wrong in a
// way anyone sees, so the fix is not only to correct it: every rule now states
// it with `mcpp::fact`, which makes a build log answer "which collection
// produced these actions" and makes a stale constant a visible defect rather
// than a dormant one.
//
// One package, one version: the number lives in mcpp.toml, and the CI step
// `the collection states its own version` compares the two.
inline constexpr std::string_view version = "0.27.0";

} // namespace mcpp::plugins


export import :stage;
export import :xml;
export import :json;
export import :graph;
export import :package;
export import :names;
export import :surface;
export import :tree;
