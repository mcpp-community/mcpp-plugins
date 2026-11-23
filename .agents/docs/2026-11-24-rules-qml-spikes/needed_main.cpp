#include "binary.hpp"
#include <iostream>
int main(int argc, char** argv) {
    int rc = 0;
    for (int i = 1; i < argc; ++i) { auto r = binary::needed(argv[i]); std::cout << argv[i] << "\n";
        if (!r.ok()) { std::cout << "  ERROR " << r.error << "\n"; rc = 1; continue; }
        for (auto& n : r.names) std::cout << "  " << n << "\n"; }
    return rc;
}
