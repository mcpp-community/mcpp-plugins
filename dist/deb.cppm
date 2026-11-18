// mcpp.dist.deb -- a staged program becomes a Debian package for Debian, Ubuntu, and their derivatives.
//
// The layout and the writer are shared with the other Linux package format in
// `mcpp.dist.linux_package`: the staged closure under `/opt/<name>`, a
// launcher in `/usr/bin`, a desktop entry and an icon, written by nfpm from
// the `xim:nfpm` payload, so the host needs no `dpkg-deb`.

export module mcpp.dist.deb;

import std;
export import mcpp.dist.linux_package;

export namespace mcpp::dist::deb {

using options = mcpp::dist::linux_package::options;
using plan = mcpp::dist::linux_package::plan;

// What `generate` would submit for `mcpp pack --format deb`, without submitting it.
inline plan plan_for(options opt = {}) {
    return mcpp::dist::linux_package::plan_for("deb", std::move(opt));
}

inline bool submit(const plan& p) {
    return mcpp::dist::linux_package::submit(p);
}

// The one call a build program makes: declares the `deb` format in every pass
// and submits the package action when `mcpp pack --format deb` asked for it.
inline bool generate(options opt = {}) {
    return mcpp::dist::linux_package::generate("deb", std::move(opt));
}

} // namespace mcpp::dist::deb
