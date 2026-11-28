// mcpp.dist.apk.internal.manifest -- manifest templating, rendering and library-manifest merging.

export module mcpp.dist.apk.internal.manifest;

import std;
import mcpp.dist.apk.internal.util;

namespace mcpp::dist::apk {

inline const std::vector<std::string>& manifest_tokens();
inline std::vector<std::string> tokens_in(const std::string& text);
inline std::vector<std::string> required_manifest_tokens(bool has_code, bool native_activity);
inline std::string manifest_key(const xml::node& n);
inline void strip_tools(xml::node& n);
inline std::string canonical_xml(xml::node n);
inline bool substitute_placeholders(xml::node& n, const std::string& appId,
                                    const std::string& label, std::string& message);
inline xml::node* find_child(xml::node& parent, std::string_view name);
inline void drop_removed(xml::node& n);

// `android:versionCode` from a version string: major * 1000000 + minor * 1000
// + patch over the first three dot-separated numeric segments, so that every
// release a project cuts orders after the one before it, and "1" when the
// string starts with no number at all -- Android refuses 0 and a manifest
// without the attribute installs but never updates.
inline std::string version_code_for(const std::string& version) {
    long long code = 0, seg = 0;
    int segments = 0;
    bool digits = false;
    const long long weights[3] = {1000000, 1000, 1};
    for (std::size_t i = 0; i <= version.size() && segments < 3; ++i) {
        const char c = i < version.size() ? version[i] : '\0';
        if (c >= '0' && c <= '9') { seg = seg * 10 + (c - '0'); digits = true; continue; }
        if (!digits) break;
        code += std::min<long long>(seg, 999) * weights[segments] ;
        ++segments; seg = 0; digits = false;
        if (c != '.') break;
    }
    if (segments == 0) return "1";
    if (code <= 0) return "1";
    return std::to_string(code);
}

// THE BUILT-IN DEFAULT, EXPRESSED WITH THE TOKENS. This is `manifest_xml`'s
// 0.8.0 output verbatim, with every literal value it used to compute
// replaced by the token that value now comes through -- so level 0 with no
// project template renders byte-identical to what 0.8.0 wrote
// (`tests/apk-consumer`). `{{activity}}` carries the level-0 constant
// (`android.app.NativeActivity`) as well as a level-1 project's own class,
// because the run sidecar needs the activity name at both levels and the
// required-token check reads the TEMPLATE TEXT, not the level -- so the same
// token has to appear on both of this function's two branches.
// `{{lib_name}}`'s `<meta-data>` element exists only at level 0: it
// announces which shared object `NativeActivity` should load, and a
// Java-hosted activity finds its own native library another way.
//
// NO XML COMMENT MARKS THE THREE REQUIRED TOKENS IN THIS STRING, ON PURPOSE:
// this exact text is compared byte-for-byte against 0.8.0's output, which
// carried none, and a template with no author to read a comment gains
// nothing from one. The design record's "mark the required tokens" is done
// here instead, in the `REQUIRED` labels on the C++ lines that build them.
// A NATIVE ACTIVITY MAY STILL CARRY CODE (0.11.0): a level-0 application whose
// libraries or archives bring classes keeps `android.app.NativeActivity` and its
// `lib_name` element, with `android:hasCode="true"` so that the dex is loaded.
inline std::string default_manifest_template(bool has_code, bool native_activity) {
    std::string a =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
        "    package=\"{{application_id}}\">\n"                     // REQUIRED
        "    <uses-sdk android:minSdkVersion=\"{{min_sdk}}\" "
        "android:targetSdkVersion=\"{{target_sdk}}\"/>\n"
        "    <application android:label=\"{{label}}\" android:hasCode=\"" +
        std::string(has_code ? "true" : "false") + "\">\n"
        "        <activity android:name=\"{{activity}}\" "          // REQUIRED
        "android:exported=\"true\">\n";
    if (native_activity) {
        a += "            <meta-data android:name=\"android.app.lib_name\" "
             "android:value=\"{{lib_name}}\"/>\n";                  // REQUIRED at level 0
    }
    a += "            <intent-filter>\n"
         "                <action android:name=\"android.intent.action.MAIN\"/>\n"
         "                <category android:name=\"android.intent.category.LAUNCHER\"/>\n"
         "            </intent-filter>\n"
         "        </activity>\n"
         "    </application>\n"
         "</manifest>\n";
    return a;
}

// Checks a manifest template and substitutes it, or refuses (returning
// `false` with `reason` and `message` set) naming exactly what is wrong; the
// caller reports `message` (see `refuse`).
//
// THIS CHECK IS `dist-apk`'S OWN, DELIBERATELY NOT `dist-web`'S. A manifest
// has a closed, six-token vocabulary this member itself defines; a web page
// template may legitimately carry `{{ }}` for a front-end framework (Vue,
// Mustache, ...) this member never reads, so `dist-web` leaves an unknown
// token literal for that project's own tooling to read. An unknown token
// here would otherwise reach `aapt2` unsubstituted and fail there with a
// worse message, and the refusal belongs where the name is known -- the same
// rule read against two different facts, not an inconsistency between the
// two members.
inline bool render_manifest(const std::string& templateText, bool has_code, bool native_activity,
                            const std::string& appId, const std::string& label,
                            const std::string& activityName, const std::string& libName,
                            const std::string& minSdk, const std::string& targetSdk,
                            const std::string& versionName, const std::string& versionCode,
                            std::string& out, std::string& reason,
                            std::string& message) {
    for (auto const& tok : tokens_in(templateText)) {
        if (std::ranges::find(manifest_tokens(), tok) == manifest_tokens().end()) {
            message = "mcpp.dist.apk: the manifest template names an unknown "
                      "token '{{" + tok + "}}'; expected one of application_id, "
                      "label, activity, lib_name, min_sdk, target_sdk, "
                      "version_name, version_code.";
            reason = "unknown manifest template token '" + tok + "'";
            return false;
        }
    }
    for (auto const& tok : required_manifest_tokens(has_code, native_activity)) {
        if (templateText.find("{{" + tok + "}}") == std::string::npos) {
            message = "mcpp.dist.apk: the manifest template does not use '{{" + tok
                    + "}}', and assets/mcpp-run.json, which adb-run starts the "
                      "application from, is written from the same value: add {{"
                    + tok + "}} to the template.";
            reason = "manifest template missing required token '" + tok + "'";
            return false;
        }
    }
    out = templateText;
    out = replace_all_copy(std::move(out), "{{application_id}}", appId);
    out = replace_all_copy(std::move(out), "{{label}}", label);
    out = replace_all_copy(std::move(out), "{{activity}}", activityName);
    out = replace_all_copy(std::move(out), "{{lib_name}}", libName);
    out = replace_all_copy(std::move(out), "{{min_sdk}}", minSdk);
    out = replace_all_copy(std::move(out), "{{target_sdk}}", targetSdk);
    out = replace_all_copy(std::move(out), "{{version_name}}", versionName);
    out = replace_all_copy(std::move(out), "{{version_code}}", versionCode);
    return true;
}
inline bool render_manifest(const std::string& templateText, bool has_code,
                            const std::string& appId, const std::string& label,
                            const std::string& activityName, const std::string& libName,
                            const std::string& minSdk, const std::string& targetSdk,
                            const std::string& versionName, const std::string& versionCode,
                            std::string& out, std::string& reason,
                            std::string& message) {
    return render_manifest(templateText, has_code, !has_code, appId, label, activityName, libName,
                           minSdk, targetSdk, versionName, versionCode, out, reason, message);
}

inline std::string run_json(const std::string& app_id, const std::string& activity_name) {
    // Hand-built, not through a JSON library this collection does not
    // depend on: two string fields, neither of which this member lets
    // through unescaped input (`app_id` is derived from the package's own
    // identifiers, `activity_name` is a Java class name or the literal
    // `android.app.NativeActivity`).
    return std::format("{{\"package\": \"{}\", \"activity\": \"{}\"}}\n",
                       app_id, activity_name);
}

// ─── Merging library manifests into the application's ────────────────────

// What `mcpp.dist.apk` merges, and the rules it applies. This is a SUBSET of
// the Android Gradle plugin's manifest merger, stated rather than implied:
//
//   - `<uses-permission>`, `<uses-permission-sdk-23>`, `<permission>`,
//     `<uses-feature>` and the other children of `<manifest>` are added when
//     the application has no element of the same kind and `android:name`;
//     `<queries>` children are unioned;
//   - the children of a library's `<application>` -- activities, aliases,
//     services, receivers, providers, `<meta-data>`, `<uses-library>` -- are
//     added the same way; a library's `<application>` attribute the
//     application does not state is added;
//   - an identical element is not added twice; a DIFFERENT element under the
//     same name is refused, naming it, unless the application's own element
//     says `tools:node="replace"` (the application's wins) or
//     `tools:node="remove"` (neither is kept); a differing `<application>`
//     attribute is refused unless the application lists it in `tools:replace`;
//   - a library's `<uses-sdk android:minSdkVersion>` above the application's
//     floor is refused, as Gradle refuses it;
//   - `${applicationId}` is substituted everywhere, and any other `${...}`
//     placeholder is refused, naming it -- no placeholder source exists here;
//   - every `tools:` attribute and the `xmlns:tools` declaration are removed
//     from the result.
//
// Anything a project needs beyond that is stated in its own manifest template,
// which this member never edits except to add what the libraries declare.
struct manifest_source {
    std::string label;   // what a refusal names: a library, an AAR, a coordinate
    std::string text;
};

inline bool merge_manifests(std::string& appText, const std::vector<manifest_source>& libs,
                            const std::string& appId, const std::string& minSdk,
                            std::string& reason, std::string& message) {
    xml::node app;
    std::string err;
    if (!xml::parse(appText, app, err) || app.name != "manifest") {
        reason = "the application manifest cannot be read";
        message = "mcpp.dist.apk: the application's manifest cannot be read for merging: "
                + (err.empty() ? std::string("its root element is not <manifest>") : err);
        return false;
    }
    if (!substitute_placeholders(app, appId, "the application's manifest", message)) {
        reason = "unknown manifest placeholder";
        return false;
    }
    xml::node* application = find_child(app, "application");
    if (!application) {
        app.children.push_back(xml::node{"application", {}, {}, {}});
        application = &app.children.back();
    }
    const auto parse_number = [](const std::string& s) {
        long v = 0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] < '0' || s[i] > '9') return -1L;
            v = v * 10 + (s[i] - '0');
        }
        return s.empty() ? -1L : v;
    };

    for (auto const& lib : libs) {
        xml::node m;
        if (!xml::parse(lib.text, m, err) || m.name != "manifest") {
            reason = "a library manifest cannot be read";
            message = std::format("mcpp.dist.apk: the manifest of {} cannot be read: {}", lib.label,
                                  err.empty() ? std::string("its root element is not <manifest>") : err);
            return false;
        }
        if (!substitute_placeholders(m, appId, lib.label, message)) {
            reason = "unknown manifest placeholder";
            return false;
        }
        for (auto& c : m.children) {
            if (c.name.empty()) continue;
            if (c.name == "uses-sdk") {
                const long want = parse_number(xml::attr_of(c, "android:minSdkVersion"));
                const long have = parse_number(minSdk);
                if (want > 0 && have > 0 && want > have) {
                    reason = "a library needs a higher minSdkVersion";
                    message = std::format(
                        "mcpp.dist.apk: {} declares android:minSdkVersion {}, above this "
                        "application's floor {}. Raise the row's min_api_level, or do not "
                        "depend on it.", lib.label, want, have);
                    return false;
                }
                continue;
            }
            if (c.name == "application") {
                const std::string replaced = xml::attr_of(*application, "tools:replace");
                for (auto const& a : c.attrs) {
                    if (a.first.starts_with("tools:")) continue;
                    const std::string mine = xml::attr_of(*application, a.first);
                    if (mine.empty()) { xml::set_attr(*application, a.first, a.second); continue; }
                    if (mine == a.second) continue;
                    bool listed = false;
                    std::size_t start = 0;
                    while (start <= replaced.size()) {
                        auto comma = replaced.find(',', start);
                        if (comma == std::string::npos) comma = replaced.size();
                        if (xml::trim_copy(replaced.substr(start, comma - start)) == a.first) { listed = true; break; }
                        start = comma + 1;
                    }
                    if (!listed) {
                        reason = "conflicting <application> attribute";
                        message = std::format(
                            "mcpp.dist.apk: <application {}> is \"{}\" in the application and "
                            "\"{}\" in {}. Add tools:replace=\"{}\" to the application's "
                            "<application> to keep its value.", a.first, mine, a.second, lib.label, a.first);
                        return false;
                    }
                }
                for (auto& cc : c.children) {
                    if (cc.name.empty()) continue;
                    const std::string key = manifest_key(cc);
                    xml::node* mine = nullptr;
                    for (auto& x : application->children)
                        if (!x.name.empty() && manifest_key(x) == key) { mine = &x; break; }
                    if (!mine) { application->children.push_back(cc); continue; }
                    const std::string node = xml::attr_of(*mine, "tools:node");
                    if (node == "remove" || node == "replace") continue;
                    if (canonical_xml(*mine) == canonical_xml(cc)) continue;
                    reason = "conflicting manifest element";
                    message = std::format(
                        "mcpp.dist.apk: <{} android:name=\"{}\"> differs between the application "
                        "and {}. State tools:node=\"replace\" on the application's element to keep "
                        "it, or tools:node=\"remove\" to drop both.", cc.name,
                        xml::attr_of(cc, "android:name"), lib.label);
                    return false;
                }
                continue;
            }
            if (c.name == "queries") {
                xml::node* q = find_child(app, "queries");
                if (!q) {
                    auto pos = std::ranges::find_if(app.children, [](const xml::node& x) { return x.name == "application"; });
                    q = &*app.children.insert(pos, xml::node{"queries", {}, {}, {}});
                    application = find_child(app, "application");
                }
                for (auto& qc : c.children) {
                    if (qc.name.empty()) continue;
                    const std::string canon = canonical_xml(qc);
                    const bool present = std::ranges::any_of(q->children, [&](const xml::node& x) {
                        return canonical_xml(x) == canon;
                    });
                    if (!present) q->children.push_back(qc);
                }
                continue;
            }
            const std::string key = manifest_key(c);
            xml::node* mine = nullptr;
            for (auto& x : app.children)
                if (!x.name.empty() && manifest_key(x) == key) { mine = &x; break; }
            if (mine) {
                const std::string node = xml::attr_of(*mine, "tools:node");
                if (node == "remove" || node == "replace") continue;
                if (canonical_xml(*mine) == canonical_xml(c)) continue;
                reason = "conflicting manifest element";
                message = std::format(
                    "mcpp.dist.apk: <{} android:name=\"{}\"> differs between the application and "
                    "{}. State tools:node=\"replace\" on the application's element to keep it, or "
                    "tools:node=\"remove\" to drop both.", c.name, xml::attr_of(c, "android:name"), lib.label);
                return false;
            }
            auto pos = std::ranges::find_if(app.children, [](const xml::node& x) { return x.name == "application"; });
            app.children.insert(pos, c);
            application = find_child(app, "application");
        }
    }

    drop_removed(app);
    strip_tools(app);
    appText = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    xml::write(app, appText, 0);
    return true;
}

// Does the manifest ask for the native libraries to be loaded from the APK in
// place (`<application android:extractNativeLibs="false">`, 0.11.1)? The
// platform can map a library only from an entry stored uncompressed on a page
// boundary, so such a package is laid out that way; a compressed library in it
// fails to install.
inline bool loads_native_libraries_in_place(const std::string& manifest) {
    xml::node root;
    std::string error;
    if (!xml::parse(manifest, root, error) || root.name != "manifest") return false;
    const xml::node* application = find_child(root, "application");
    return application && xml::attr_of(*application, "android:extractNativeLibs") == "false";
}

} // namespace mcpp::dist::apk

module :private;

namespace mcpp::dist::apk {
// The eight tokens a manifest template may use. `version_name` is the
// package version as written; `version_code` is Android's monotonic integer
// derived from its leading numeric segments (`version_code_for`).
inline const std::vector<std::string>& manifest_tokens() {
    static const std::vector<std::string> v = {
        "application_id", "label", "activity", "lib_name", "min_sdk", "target_sdk",
        "version_name", "version_code"};
    return v;
}

// Every `{{...}}` a template names, in first-appearance order, duplicates
// dropped -- what both checks in `render_manifest` below read.
inline std::vector<std::string> tokens_in(const std::string& text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while ((i = text.find("{{", i)) != std::string::npos) {
        const auto close = text.find("}}", i + 2);
        if (close == std::string::npos) break;
        std::string name = text.substr(i + 2, close - (i + 2));
        if (std::ranges::find(out, name) == out.end()) out.push_back(std::move(name));
        i = close + 2;
    }
    return out;
}

// The tokens `assets/mcpp-run.json` is ALSO written from -- a template that
// omits one silently ships a run sidecar the manifest disagrees with.
// `application_id` and `activity` are required unconditionally; `lib_name`
// joins them at level 0, where the manifest's own `<meta-data>` element is
// the only place the loaded library's name is recorded.
inline std::vector<std::string> required_manifest_tokens(bool has_code, bool native_activity) {
    static_cast<void>(has_code);
    std::vector<std::string> v = {"application_id", "activity"};
    if (native_activity) v.push_back("lib_name");
    return v;
}
inline std::vector<std::string> required_manifest_tokens(bool has_code) {
    return required_manifest_tokens(has_code, !has_code);
}

inline std::string default_manifest_template(bool has_code) {
    return default_manifest_template(has_code, !has_code);
}

inline bool render_manifest(const std::string& templateText, bool has_code,
                            const std::string& appId, const std::string& label,
                            const std::string& activityName, const std::string& libName,
                            const std::string& minSdk, const std::string& targetSdk,
                            const std::string& versionName, const std::string& versionCode,
                            std::string& out, std::string& reason,
                            std::string& message) {
    return render_manifest(templateText, has_code, !has_code, appId, label, activityName, libName,
                           minSdk, targetSdk, versionName, versionCode, out, reason, message);
}

inline std::string manifest_key(const xml::node& n) {
    if (n.name == "uses-feature") {
        const std::string nm = xml::attr_of(n, "android:name");
        return nm.empty() ? "uses-feature@glEsVersion=" + xml::attr_of(n, "android:glEsVersion")
                          : "uses-feature:" + nm;
    }
    return n.name + ":" + xml::attr_of(n, "android:name");
}

inline void strip_tools(xml::node& n) {
    std::erase_if(n.attrs, [](const std::pair<std::string, std::string>& a) {
        return a.first.starts_with("tools:") || a.first == "xmlns:tools";
    });
    for (auto& c : n.children) strip_tools(c);
}

inline std::string canonical_xml(xml::node n) {
    strip_tools(n);
    std::string out;
    xml::write(n, out, 0);
    return out;
}

inline bool substitute_placeholders(xml::node& n, const std::string& appId,
                                    const std::string& label, std::string& message) {
    for (auto& a : n.attrs) {
        a.second = replace_all_copy(a.second, "${applicationId}", appId);
        const auto p = a.second.find("${");
        if (p != std::string::npos) {
            const auto e = a.second.find('}', p);
            message = std::format(
                "mcpp.dist.apk: {} uses the manifest placeholder '{}' in {}=\"{}\"; "
                "only ${{applicationId}} is substituted here.", label,
                a.second.substr(p, e == std::string::npos ? std::string::npos : e - p + 1),
                a.first, a.second);
            return false;
        }
    }
    for (auto& c : n.children)
        if (!substitute_placeholders(c, appId, label, message)) return false;
    return true;
}

inline xml::node* find_child(xml::node& parent, std::string_view name) {
    for (auto& c : parent.children) if (c.name == name) return &c;
    return nullptr;
}

// Removes every element the application marked `tools:node="remove"`: a marker
// that says "this is not wanted", not an element to ship.
inline void drop_removed(xml::node& n) {
    std::erase_if(n.children, [](const xml::node& c) { return xml::attr_of(c, "tools:node") == "remove"; });
    for (auto& c : n.children) drop_removed(c);
}
} // namespace mcpp::dist::apk

