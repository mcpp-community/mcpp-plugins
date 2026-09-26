#include <fmt/format.h>

#include <cstdio>

int main() {
    std::puts(fmt::format("app-b: fmt {}", FMT_VERSION / 10000).c_str());
    return 0;
}
