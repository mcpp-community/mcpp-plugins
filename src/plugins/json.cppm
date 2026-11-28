export module mcpp.plugins:json;

import std;


// mcpp::plugins::json -- the JSON two dist members read.
//
// SHARED BECAUSE TWO DOCUMENTS ARE READ WITH IT. `dist-apk` reads coursier's
// resolution report, and `mcpp::plugins::graph` below reads the engine's graph
// document for `dist-apk` and `dist-apple`. A second parser for the second
// document is how two readers come to disagree about one file, so there is one.
// It reads objects, arrays, strings (with `\u` escapes, surrogate pairs
// included, and a refusal for a surrogate that is not part of one), numbers,
// `true`, `false` and `null`; a number keeps its spelling in `text`.
export namespace mcpp::plugins::json {

// A MEMBER IS ITS OWN STRUCT, NOT `std::pair<std::string, value>`. Under clang
// with libstdc++'s `std` module, `pair<string, value>` inside `value` is
// instantiated while `value` is still incomplete -- its destructor needs the
// pair and the pair needs `value` -- and the unit fails with "incomplete type
// used in type trait expression". GCC accepts the pair and so does clang with
// libc++, so neither build lane saw it; clangd reading a GCC project in the
// editor is exactly that combination. A plain aggregate, with `get()` defined
// once both types are complete, compiles under all three.
struct member;

struct value {
    enum class kind { null, boolean, number, string, array, object };
    kind                type = kind::null;
    std::string         text;    // string, number or boolean spelling
    std::vector<value>  items;
    std::vector<member> members;

    const value* get(std::string_view key) const;
};

struct member {
    std::string first;
    value       second;
};

inline const value* value::get(std::string_view key) const {
    for (auto const& m : members) if (m.first == key) return &m.second;
    return nullptr;
}

struct reader {
    std::string_view s;
    std::size_t      i = 0;

    void skip_space() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    // The four hexadecimal digits of one `\u` escape, `i` already past the `u`.
    bool hex4(unsigned& cp) {
        if (i + 4 > s.size()) return false;
        cp = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = s[i++];
            cp <<= 4;
            if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& out) {
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        while (i < s.size() && s[i] != '"') {
            if (s[i] != '\\') { out += s[i++]; continue; }
            if (++i >= s.size()) return false;
            const char e = s[i++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    // A code point above U+FFFF reaches JSON as a surrogate
                    // PAIR, which is two `\u` escapes. Encoding each half on its
                    // own produces two three-byte sequences holding unpaired
                    // surrogates -- not UTF-8, and accepted by nothing that
                    // reads the manifest this value is written into. The pair is
                    // combined here, and a surrogate that is not part of one is
                    // refused, so a malformed document is a refusal rather than
                    // a silently corrupted string.
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        if (i + 6 > s.size() || s[i] != '\\' || s[i + 1] != 'u') return false;
                        i += 2;
                        unsigned lo = 0;
                        if (!hex4(lo)) return false;
                        if (lo < 0xdc00 || lo > 0xdfff) return false;
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                    } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                        return false;   // a low surrogate with no high half
                    }
                    if (cp < 0x80) out += static_cast<char>(cp);
                    else if (cp < 0x800) { out += static_cast<char>(0xc0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3f)); }
                    else if (cp < 0x10000) { out += static_cast<char>(0xe0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f)); out += static_cast<char>(0x80 | (cp & 0x3f)); }
                    else {
                        out += static_cast<char>(0xf0 | (cp >> 18));
                        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
                        out += static_cast<char>(0x80 | (cp & 0x3f));
                    }
                    break;
                }
                default: out += e;
            }
        }
        if (i >= s.size()) return false;
        ++i;
        return true;
    }
    bool parse_value(value& out) {
        skip_space();
        if (i >= s.size()) return false;
        const char c = s[i];
        if (c == '"') { out.type = value::kind::string; return string(out.text); }
        if (c == '{') {
            out.type = value::kind::object;
            ++i;
            skip_space();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            for (;;) {
                skip_space();
                std::string key;
                if (!string(key)) return false;
                skip_space();
                if (i >= s.size() || s[i] != ':') return false;
                ++i;
                value v;
                if (!parse_value(v)) return false;
                out.members.emplace_back(std::move(key), std::move(v));
                skip_space();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return false;
            }
        }
        if (c == '[') {
            out.type = value::kind::array;
            ++i;
            skip_space();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            for (;;) {
                value v;
                if (!parse_value(v)) return false;
                out.items.push_back(std::move(v));
                skip_space();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return false;
            }
        }
        const std::size_t b = i;
        while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' &&
               s[i] != ' ' && s[i] != '\n' && s[i] != '\r' && s[i] != '\t') ++i;
        out.text = std::string(s.substr(b, i - b));
        if (out.text == "null") out.type = value::kind::null;
        else if (out.text == "true" || out.text == "false") out.type = value::kind::boolean;
        else out.type = value::kind::number;
        return !out.text.empty();
    }
};

// The whole text is one value, with nothing but white space after it.
inline bool parse_json(std::string_view text, value& out) {
    reader r{text};
    if (!r.parse_value(out)) return false;
    r.skip_space();
    return r.i == r.s.size();
}

// A string member of an object, or empty when absent or not a string.
inline std::string string_of(const value* v) {
    return v && v->type == value::kind::string ? v->text : std::string();
}

} // namespace mcpp::plugins::json
