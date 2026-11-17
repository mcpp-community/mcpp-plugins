// mcpp.dist.rpm -- a staged program becomes an RPM package for Fedora, RHEL, openSUSE, and their derivatives.
//
// The layout and the writer are shared with the other Linux package format in
// `mcpp.dist.linux_package`: the staged closure under `/opt/<name>`, a
// launcher in `/usr/bin`, a desktop entry and an icon, written by nfpm from
// the `xim:nfpm` payload, so the host needs no `rpmbuild`.

export module mcpp.dist.rpm;

import std;
export import mcpp.dist.linux_package;

export namespace mcpp::dist::rpm {

using options = mcpp::dist::linux_package::options;
using plan = mcpp::dist::linux_package::plan;

// What `generate` would submit for `mcpp pack --format rpm`, without submitting it.
inline plan plan_for(options opt = {}) {
    return mcpp::dist::linux_package::plan_for("rpm", std::move(opt));
}

inline bool submit(const plan& p) {
    return mcpp::dist::linux_package::submit(p);
}

// The one call a build program makes: declares the `rpm` format in every pass
// and submits the package action when `mcpp pack --format rpm` asked for it.
inline bool generate(options opt = {}) {
    return mcpp::dist::linux_package::generate("rpm", std::move(opt));
}

} // namespace mcpp::dist::rpm
