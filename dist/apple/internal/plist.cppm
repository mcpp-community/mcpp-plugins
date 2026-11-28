// mcpp.dist.apple.internal.plist -- Info.plist generation and plist-fragment reading for mcpp.dist.apple.

export module mcpp.dist.apple.internal.plist;

import std;
import mcpp;
import mcpp.plugins;
import mcpp.dist.apple.internal.layout;

namespace mcpp::dist::apple::detail {

inline std::string plist_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;";  break;
            case '>': out += "&gt;";  break;
            default:  out += c;
        }
    }
    return out;
}

// Written as XML text directly rather than by shelling out to `plutil`: the
// content is a handful of string keys this member already holds, and adding
// a second host tool to discover and invoke would buy nothing over
// formatting the lines by hand. `LSMinimumSystemVersion` / `MinimumOSVersion`
// and `CFBundleIconFile` are omitted rather than emitted empty when their
// inputs are empty -- an empty string in either key is a claim as
// unsupported as omitting the key, so omission is the honest one.
//
// `is_ios` AND `is_sim` DECIDE FOUR KEYS, NOT ONE BRANCH. `MinimumOSVersion`
// replaces `LSMinimumSystemVersion`; `CFBundleSupportedPlatforms`,
// `UIDeviceFamily` and `LSRequiresIPhoneOS` are iOS-only and absent from
// every macOS bundle this member has ever written, which is the
// byte-identical property the macOS fixture depends on; `NSHighResolutionCapable`
// is a macOS concept (Retina-aware drawing on a platform that also has
// non-Retina displays) with nothing to opt into on iOS, so it is macOS-only
// in the other direction.
inline std::string plist_document(const std::string& executable, const std::string& bundle_id,
                                  const std::string& name, const std::string& version,
                                  bool is_ios, bool is_sim,
                                  const std::string& min_os_version,
                                  // The project's own entries (0.11.0), already
                                  // formatted, inserted before `</dict>`; and the
                                  // defaulted keys they replace.
                                  const std::string& extra_entries,
                                  const std::vector<std::string>& replaced_keys,
                                  // macOS: the icon FILE's basename, extension
                                  // included, exactly as `CFBundleIconFile` has
                                  // always taken it. iOS: unused (see
                                  // `ios_icon_stems`) and always empty.
                                  const std::string& mac_icon_name,
                                  // iOS: every PNG stem found in
                                  // `options::icon`'s directory. macOS: unused
                                  // and always empty.
                                  const std::vector<std::string>& ios_icon_stems) {
    std::string doc;
    doc += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    doc += "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
    doc += "<plist version=\"1.0\">\n<dict>\n";
    doc += std::format("    <key>CFBundleExecutable</key>\n    <string>{}</string>\n",
                       plist_escape(executable));
    doc += std::format("    <key>CFBundleIdentifier</key>\n    <string>{}</string>\n",
                       plist_escape(bundle_id));
    doc += std::format("    <key>CFBundleName</key>\n    <string>{}</string>\n",
                       plist_escape(name));
    doc += std::format("    <key>CFBundleShortVersionString</key>\n    <string>{}</string>\n",
                       plist_escape(version));
    doc += std::format("    <key>CFBundleVersion</key>\n    <string>{}</string>\n",
                       plist_escape(version));
    doc += "    <key>CFBundlePackageType</key>\n    <string>APPL</string>\n";
    if (!min_os_version.empty())
        doc += std::format("    <key>{}</key>\n    <string>{}</string>\n",
                           is_ios ? "MinimumOSVersion" : "LSMinimumSystemVersion",
                           plist_escape(min_os_version));
    if (is_ios) {
        // `env == "sim"` is the engine's own distinction between the two
        // rows (triple.cppm: "the simulator is deliberately not a row of
        // its own identity... it has its own SDK"); this key is the one
        // place a bundle has to restate it, because `simctl install` and a
        // real device's installer both read it to refuse the other kind.
        doc += std::format(
            "    <key>CFBundleSupportedPlatforms</key>\n    <array>\n"
            "        <string>{}</string>\n    </array>\n",
            is_sim ? "iPhoneSimulator" : "iPhoneOS");
        // `[1, 2]`: iPhone and iPad. Nothing here narrows a project to one
        // idiom -- that is a project decision (a size class, a storyboard)
        // this member has no basis for making.
        const auto replaced = [&](std::string_view k) {
            return std::ranges::find(replaced_keys, k) != replaced_keys.end();
        };
        if (!replaced("UIDeviceFamily"))
            doc += "    <key>UIDeviceFamily</key>\n    <array>\n"
                   "        <integer>1</integer>\n        <integer>2</integer>\n    </array>\n";
        if (!replaced("LSRequiresIPhoneOS"))
            doc += "    <key>LSRequiresIPhoneOS</key>\n    <true/>\n";
        if (!ios_icon_stems.empty()) {
            doc += "    <key>CFBundleIcons</key>\n    <dict>\n"
                   "        <key>CFBundlePrimaryIcon</key>\n        <dict>\n"
                   "            <key>CFBundleIconFiles</key>\n            <array>\n";
            for (auto const& stem : ios_icon_stems)
                doc += std::format("                <string>{}</string>\n", plist_escape(stem));
            doc += "            </array>\n        </dict>\n    </dict>\n";
        }
    } else {
        if (std::ranges::find(replaced_keys, std::string_view("NSHighResolutionCapable")) == replaced_keys.end())
            doc += "    <key>NSHighResolutionCapable</key>\n    <true/>\n";
        // Not in the letter of this member's key list, but added
        // deliberately: without it, an icon file this member went to the
        // trouble of copying into `Contents/Resources/` is inert -- nothing
        // in the bundle would ever reference it, and Finder would show the
        // generic application icon regardless of `options::icon`. Wiring the
        // file up once it exists is what makes the option do what its name
        // says.
        if (!mac_icon_name.empty())
            doc += std::format("    <key>CFBundleIconFile</key>\n    <string>{}</string>\n",
                               plist_escape(mac_icon_name));
    }
    doc += extra_entries;
    doc += "</dict>\n</plist>\n";
    return doc;
}
// The keys `plist_document` derives, which a project's plist may not restate,
// and the keys it only defaults, which a project's plist replaces.
inline const std::vector<std::string>& derived_plist_keys() {
    static const std::vector<std::string> v = {
        "CFBundleExecutable", "CFBundleIdentifier", "CFBundleName", "CFBundleShortVersionString",
        "CFBundleVersion", "CFBundlePackageType", "LSMinimumSystemVersion", "MinimumOSVersion",
        "CFBundleSupportedPlatforms", "CFBundleIconFile", "CFBundleIcons"};
    return v;
}
inline const std::vector<std::string>& defaulted_plist_keys() {
    static const std::vector<std::string> v = {"UIDeviceFamily", "LSRequiresIPhoneOS", "NSHighResolutionCapable"};
    return v;
}

// The top-level `<dict>` of a property list, or null. A document whose root is
// `<dict>` itself is accepted as well as a full `<plist>`.
inline const mcpp::plugins::xml::node* top_dict(const mcpp::plugins::xml::node& root) {
    if (root.name == "dict") return &root;
    if (root.name != "plist") return nullptr;
    for (auto const& c : root.children) if (c.name == "dict") return &c;
    return nullptr;
}

// The value element after each `<key>` of a `<dict>`, as (key, value) pairs.
inline bool dict_entries(const mcpp::plugins::xml::node& dict,
                         std::vector<std::pair<std::string, const mcpp::plugins::xml::node*>>& out,
                         std::string& error) {
    namespace xml = mcpp::plugins::xml;
    for (std::size_t i = 0; i < dict.children.size(); ++i) {
        const auto& k = dict.children[i];
        if (k.name.empty()) continue;
        if (k.name != "key") { error = "<" + k.name + "> appears where a <key> is expected"; return false; }
        const std::string key = k.children.size() == 1 && k.children.front().name.empty()
            ? xml::trim_copy(k.children.front().text) : std::string();
        std::size_t j = i + 1;
        while (j < dict.children.size() && dict.children[j].name.empty()) ++j;
        if (key.empty() || j >= dict.children.size()) { error = "a <key> without a value"; return false; }
        out.emplace_back(key, &dict.children[j]);
        i = j;
    }
    return true;
}

// One Info.plist entry a fragment states: its key and the plist text of the
// `<key>` and its value, as `plist_document` inserts it.
struct plist_entry {
    std::string key;
    std::string text;
};

// Reads one plist fragment's entries. `who` is how a diagnostic names the
// fragment (`options::info_plist`, or a package's [package.metadata.dist-apple]).
inline bool read_plist_entries(const std::string& path, const std::string& who,
                               std::vector<plist_entry>& out, std::string& message) {
    namespace xml = mcpp::plugins::xml;
    xml::node root;
    std::string err;
    if (!xml::parse(read_text(path), root, err)) {
        message = std::format("mcpp.dist.apple: {} ({}) cannot be read: {}", who, path, err);
        return false;
    }
    const xml::node* dict = top_dict(root);
    if (!dict) {
        message = std::format("mcpp.dist.apple: {} ({}) has no top-level <dict>.", who, path);
        return false;
    }
    std::vector<std::pair<std::string, const xml::node*>> pairs;
    if (!dict_entries(*dict, pairs, err)) {
        message = std::format("mcpp.dist.apple: {} ({}): {}", who, path, err);
        return false;
    }
    std::vector<std::string> seen;
    for (auto const& [key, value] : pairs) {
        if (std::ranges::find(derived_plist_keys(), key) != derived_plist_keys().end()) {
            message = std::format(
                "mcpp.dist.apple: {} ({}) sets {}, which this member derives "
                "from its options and the engine; set the option instead.", who, path, key);
            return false;
        }
        if (std::ranges::find(seen, key) != seen.end()) {
            message = std::format("mcpp.dist.apple: {} ({}) sets {} twice.", who, path, key);
            return false;
        }
        seen.push_back(key);
        plist_entry e;
        e.key  = key;
        e.text = "    <key>" + key + "</key>\n";
        xml::write(*value, e.text, 1);
        out.push_back(std::move(e));
    }
    return true;
}

} // namespace mcpp::dist::apple::detail

module :private;

namespace mcpp::dist::apple::detail {

inline std::string plist_document(const std::string& executable, const std::string& bundle_id,
                                  const std::string& name, const std::string& version,
                                  bool is_ios, bool is_sim,
                                  const std::string& min_os_version,
                                  const std::string& mac_icon_name,
                                  const std::vector<std::string>& ios_icon_stems) {
    return plist_document(executable, bundle_id, name, version, is_ios, is_sim, min_os_version,
                          std::string(), std::vector<std::string>(), mac_icon_name, ios_icon_stems);
}

} // namespace mcpp::dist::apple::detail
