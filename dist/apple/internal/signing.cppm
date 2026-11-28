// mcpp.dist.apple.internal.signing -- provisioning-profile reading for mcpp.dist.apple.

export module mcpp.dist.apple.internal.signing;

import std;
import mcpp;
import mcpp.plugins;
import mcpp.dist.apple.internal.layout;
import mcpp.dist.apple.internal.plist;

namespace mcpp::dist::apple::detail {

// A provisioning profile is a CMS-signed property list. The plist is stored in
// the signed content as-is, so it is read from between its `<?xml` and
// `</plist>` rather than through `security cms -D`, which exists only on macOS
// and would make a plan made elsewhere unable to check anything. The signature
// is the device's to verify, not this member's.
inline bool read_profile(const std::string& path, const std::string& bundleId,
                         std::string& entitlementsPlist, std::string& message) {
    namespace xml = mcpp::plugins::xml;
    const std::string bytes = read_text(path);
    const auto b = bytes.find("<?xml");
    const auto e = bytes.find("</plist>");
    if (b == std::string::npos || e == std::string::npos || e < b) {
        message = std::format(
            "mcpp.dist.apple: `options::provisioning_profile` ({}) carries no property list, so "
            "it is not a provisioning profile.", path);
        return false;
    }
    xml::node root;
    std::string err;
    if (!xml::parse(std::string_view(bytes).substr(b, e + 8 - b), root, err)) {
        message = std::format("mcpp.dist.apple: the property list in {} cannot be read: {}", path, err);
        return false;
    }
    const xml::node* dict = top_dict(root);
    std::vector<std::pair<std::string, const xml::node*>> pairs;
    if (!dict || !dict_entries(*dict, pairs, err)) {
        message = std::format("mcpp.dist.apple: the property list in {} has no readable top-level <dict>.", path);
        return false;
    }
    const xml::node* entitlements = nullptr;
    for (auto const& [key, value] : pairs) if (key == "Entitlements" && value->name == "dict") entitlements = value;
    std::vector<std::pair<std::string, const xml::node*>> granted;
    if (!entitlements || !dict_entries(*entitlements, granted, err)) {
        message = std::format("mcpp.dist.apple: the provisioning profile {} states no Entitlements dictionary.", path);
        return false;
    }
    std::string appIdentifier;
    for (auto const& [key, value] : granted)
        if (key == "application-identifier" && value->children.size() == 1 && value->children.front().name.empty())
            appIdentifier = xml::trim_copy(value->children.front().text);
    const auto dot = appIdentifier.find('.');
    const std::string pattern = dot == std::string::npos ? std::string() : appIdentifier.substr(dot + 1);
    const bool covers = !pattern.empty() &&
        (pattern == bundleId || pattern == "*" ||
         (pattern.ends_with(".*") && bundleId.starts_with(pattern.substr(0, pattern.size() - 1))));
    if (!covers) {
        message = std::format(
            "mcpp.dist.apple: the provisioning profile {} is for the application identifier '{}', "
            "which does not cover the bundle identifier '{}'. Set `options::bundle_id` to match, "
            "or use the profile made for it.", path,
            appIdentifier.empty() ? std::string("(none)") : appIdentifier, bundleId);
        return false;
    }
    // A WILDCARD PROFILE GRANTS `<team>.*`, AND THE SIGNATURE STATES THE BUNDLE.
    // Xcode signs with the application identifier the bundle is, which the
    // wildcard covers, rather than with the wildcard itself; the other
    // entitlements are the profile's as it states them.
    xml::node signedWith = *entitlements;
    if (pattern != bundleId) {
        const std::string exact = appIdentifier.substr(0, dot + 1) + bundleId;
        for (std::size_t k = 0; k + 1 < signedWith.children.size(); ++k) {
            auto const& key = signedWith.children[k];
            auto& value = signedWith.children[k + 1];
            if (key.name == "key" && key.children.size() == 1 &&
                xml::trim_copy(key.children.front().text) == "application-identifier" &&
                value.name == "string" && value.children.size() == 1 && value.children.front().name.empty())
                value.children.front().text = exact;
        }
    }
    entitlementsPlist = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                        "<plist version=\"1.0\">\n";
    xml::write(signedWith, entitlementsPlist, 0);
    entitlementsPlist += "</plist>\n";
    return true;
}

} // namespace mcpp::dist::apple::detail
