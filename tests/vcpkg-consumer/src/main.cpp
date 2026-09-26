// Formats through fmt, so the program links it and -- on Windows, where the
// default triplet builds fmt as a DLL -- loads it at run time.
#include <fmt/format.h>

#include <cstdio>

int main() {
    std::puts(fmt::format("vcpkg-consumer: fmt {} says {}", FMT_VERSION / 10000, 6 * 7).c_str());
    return 0;
}
