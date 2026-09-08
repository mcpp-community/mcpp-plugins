// mcpp-embed -- the surface generator, as a program the BUILD GRAPH invokes.
//
// WHY THIS EXISTS AT ALL, AND WHY IT IS SO SMALL.
//
// `mcpp.plugins.surface` writes three files: the module interface, the
// implementation, and -- under `storage::object` -- an assembly source that
// names each payload in `.incbin`. Called from `build.mcpp`, all three are
// written at PLAN time, and that is correct for two of them: their content is
// a function of the item list, which the rule already knows.
//
// It is not correct for the third. The OBJECT the assembly produces is a
// function of the payload's BYTES, and those do not exist when build.mcpp
// runs -- the shader compiler has not been invoked yet. A file written at plan
// time cannot be an edge to a build-time product, so the object was assembled
// once and every later change to a payload was a green build over stale bytes.
// Measured against `mcpp:plugins` 0.3.0: editing a shader left the program
// reporting the previous payload's byte count.
//
// The fix is not a new channel for stating that edge. It is to stop writing
// the file at the wrong time. An `mcpp::action` declares its inputs, and a
// payload named as an input of the action that writes the assembly IS the
// edge -- expressed with the one graph primitive the engine has, and needing
// nothing added to it. Prototyped before this was written: the artifact
// followed the payload with no engine change at all.
//
// So this program exists to be an action's COMMAND. It holds no logic of its
// own: `mcpp.plugins.surface` decides every name, and this reads what the rule
// wrote down and calls it. The two forms cannot drift, because they are the
// same code -- which is why that module imports only `std`.
#include <cstdio>
#include <cstdlib>

import std;
import mcpp.plugins;

namespace {

// The manifest the rule writes at plan time. Deliberately a flat line-based
// format rather than JSON: the whole vocabulary is below, both ends are in
// this package, and a parser is a place for a defect that a `find`/`substr`
// pair is not.
//
//   surface   module|c_header
//   storage   header|object|sidecar
//   element   byte|word32
//   module    <module name>
//   outdir    <directory>
//   by        <what produced the payloads>
//   os        <target os>
//   gas       0|1
//   emit      interface|body        (which half this invocation writes)
//   item      <identifier>
//   ns        <segment>             (repeats, applies to the current item)
//   header    <data header, as an include writes it>
//   symbol    <array the header declares>
//   payload   <absolute path>
//   sidecar   <path relative to the working directory>
//   size      <expression, or empty for sizeof>
struct parsed {
    mcpp::plugins::surface::options            opt;
    std::vector<mcpp::plugins::surface::item>  items;
    std::string                                emit;   // "interface" | "body"
};

std::optional<parsed> read_manifest(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << std::format("mcpp-embed: cannot read {}\n", path);
        return std::nullopt;
    }
    parsed p;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto sp = line.find(' ');
        auto key = line.substr(0, sp);
        auto val = sp == std::string::npos ? std::string() : line.substr(sp + 1);
        namespace sf = mcpp::plugins::surface;
        if      (key == "surface") p.opt.surface = val == "c_header" ? sf::kind::c_header
                                                                    : sf::kind::module_;
        else if (key == "storage") p.opt.store   = val == "object"  ? sf::storage::object
                                                : val == "sidecar" ? sf::storage::sidecar
                                                                   : sf::storage::header;
        else if (key == "element") p.opt.elem    = val == "byte" ? sf::element::byte_
                                                                : sf::element::word32;
        else if (key == "module")  p.opt.module_name = val;
        else if (key == "outdir")  p.opt.out_dir     = val;
        else if (key == "by")      p.opt.produced_by = val;
        else if (key == "os")      p.opt.target_os   = val;
        else if (key == "gas")     p.opt.has_gas_assembler = val != "0";
        else if (key == "emit")    p.emit            = val;
        else if (key == "item")    p.items.push_back({ .identifier = val });
        else if (p.items.empty()) {
            std::cerr << std::format("mcpp-embed: `{}` before any `item`\n", key);
            return std::nullopt;
        }
        else if (key == "ns")      p.items.back().name_space.push_back(val);
        else if (key == "header")  p.items.back().data_header    = val;
        else if (key == "symbol")  p.items.back().data_symbol    = val;
        else if (key == "payload") p.items.back().payload_path   = val;
        else if (key == "sidecar") p.items.back().sidecar_name   = val;
        else if (key == "size")    p.items.back().data_size_expr = val;
        else {
            // Refused, not ignored: an unknown key means the rule and the tool
            // disagree about the vocabulary, and they ship in one package at
            // one version, so that can only be a defect.
            std::cerr << std::format("mcpp-embed: unknown key `{}`\n", key);
            return std::nullopt;
        }
    }
    return p;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: mcpp-embed <manifest>\n";
        return 2;
    }
    auto p = read_manifest(argv[1]);
    if (!p) return 1;
    namespace sf = mcpp::plugins::surface;
    if (p->emit != "interface" && p->emit != "body") {
        std::cerr << std::format("mcpp-embed: `emit` must be interface or body, got `{}`\n",
                                 p->emit);
        return 2;
    }
    const bool ok = sf::write(p->items, p->opt,
                              p->emit == "interface" ? sf::half::interface_ : sf::half::body);
    return ok ? 0 : 1;
}
