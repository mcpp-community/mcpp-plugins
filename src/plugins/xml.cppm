export module mcpp.plugins:xml;

import std;


// mcpp::plugins::xml -- the XML two dist members read and write.
//
// SHARED BECAUSE TWO DIST MEMBERS READ IT. `dist-apk` merges library manifests
// into an application's, and `dist-apple` adds a project's Info.plist entries
// and reads the plist a provisioning profile carries. Neither needs more than
// elements, attributes and text, and neither interprets an entity, so this is
// that much and no more: no namespaces resolved, no DTD read, no validation.
export namespace mcpp::plugins::xml {

// A tree of elements and text. Comments, the XML declaration and a DOCTYPE are
// read and dropped: the documents this member writes from a tree are generated
// files, and a merged manifest carries no author to read a comment. Attribute
// values are kept exactly as written, entities included, because nothing here
// interprets them beyond comparing and substituting `${applicationId}`.
struct node {
    std::string                                      name;   // empty: a text node
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<node>                            children;
    std::string                                      text;   // a text node's characters
};

struct reader {
    std::string_view s;
    std::size_t      i = 0;
    std::string      error;

    static bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
    bool starts(std::string_view t) const { return s.substr(i, t.size()) == t; }
    void skip_space() { while (i < s.size() && space(s[i])) ++i; }
    bool skip_past(std::string_view end) {
        const auto p = s.find(end, i);
        if (p == std::string_view::npos) { i = s.size(); return false; }
        i = p + end.size();
        return true;
    }
    std::string name() {
        const std::size_t b = i;
        while (i < s.size() && !space(s[i]) && s[i] != '>' && s[i] != '/' && s[i] != '=') ++i;
        return std::string(s.substr(b, i - b));
    }

    bool element(node& out) {
        ++i;  // '<'
        out.name = name();
        if (out.name.empty()) { error = "an element without a name"; return false; }
        for (;;) {
            skip_space();
            if (i >= s.size()) { error = "<" + out.name + "> is not terminated"; return false; }
            if (starts("/>")) { i += 2; return true; }
            if (s[i] == '>') { ++i; break; }
            std::string attr = name();
            skip_space();
            if (attr.empty() || i >= s.size() || s[i] != '=') {
                error = "an attribute of <" + out.name + "> has no value";
                return false;
            }
            ++i;
            skip_space();
            if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) {
                error = "attribute " + attr + " of <" + out.name + "> is not quoted";
                return false;
            }
            const char quote = s[i++];
            const auto end = s.find(quote, i);
            if (end == std::string_view::npos) { error = "attribute " + attr + " is not terminated"; return false; }
            out.attrs.emplace_back(std::move(attr), std::string(s.substr(i, end - i)));
            i = end + 1;
        }
        for (;;) {
            if (i >= s.size()) { error = "<" + out.name + "> is not closed"; return false; }
            if (starts("</")) {
                i += 2;
                const std::string closing = name();
                skip_space();
                if (closing != out.name || i >= s.size() || s[i] != '>') {
                    error = "</" + closing + "> closes <" + out.name + ">";
                    return false;
                }
                ++i;
                return true;
            }
            if (starts("<!--")) { if (!skip_past("-->")) { error = "a comment is not terminated"; return false; } continue; }
            if (starts("<?"))   { if (!skip_past("?>"))  { error = "a processing instruction is not terminated"; return false; } continue; }
            if (starts("<![CDATA[")) {
                const std::size_t b = i;
                if (!skip_past("]]>")) { error = "a CDATA section is not terminated"; return false; }
                node t;
                t.text = std::string(s.substr(b, i - b));
                out.children.push_back(std::move(t));
                continue;
            }
            if (s[i] == '<') {
                node child;
                if (!element(child)) return false;
                out.children.push_back(std::move(child));
                continue;
            }
            const std::size_t b = i;
            auto end = s.find('<', i);
            if (end == std::string_view::npos) end = s.size();
            i = end;
            bool blank = true;
            for (std::size_t j = b; j < end; ++j) if (!space(s[j])) { blank = false; break; }
            if (!blank) {
                node t;
                t.text = std::string(s.substr(b, end - b));
                out.children.push_back(std::move(t));
            }
        }
    }

    bool document(node& root) {
        for (;;) {
            skip_space();
            if (starts("<?"))        { skip_past("?>");  continue; }
            if (starts("<!--"))      { skip_past("-->"); continue; }
            if (starts("<!DOCTYPE")) { skip_past(">");   continue; }
            break;
        }
        if (i >= s.size() || s[i] != '<') { error = "no root element"; return false; }
        if (!element(root)) return false;
        return true;
    }
};

inline bool parse(std::string_view text, node& root, std::string& error) {
    reader r{text};
    if (!r.document(root)) { error = r.error; return false; }
    return true;
}

inline std::string trim_copy(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && reader::space(s[b])) ++b;
    while (e > b && reader::space(s[e - 1])) --e;
    return s.substr(b, e - b);
}

inline void write(const node& n, std::string& out, int depth) {
    const std::string pad(static_cast<std::size_t>(depth) * 4, ' ');
    if (n.name.empty()) { out += pad + trim_copy(n.text) + "\n"; return; }
    out += pad + "<" + n.name;
    for (auto const& a : n.attrs) out += " " + a.first + "=\"" + a.second + "\"";
    if (n.children.empty()) { out += "/>\n"; return; }
    if (n.children.size() == 1 && n.children.front().name.empty()) {
        out += ">" + n.children.front().text + "</" + n.name + ">\n";
        return;
    }
    out += ">\n";
    for (auto const& c : n.children) write(c, out, depth + 1);
    out += pad + "</" + n.name + ">\n";
}

inline std::string attr_of(const node& n, std::string_view key) {
    for (auto const& a : n.attrs) if (a.first == key) return a.second;
    return {};
}

inline void set_attr(node& n, const std::string& key, const std::string& value) {
    for (auto& a : n.attrs) if (a.first == key) { a.second = value; return; }
    n.attrs.emplace_back(key, value);
}

} // namespace mcpp::plugins::xml
