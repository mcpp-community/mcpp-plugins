#include <cstdio>

// The program exists so the fixture has something to link. What is being
// tested happened before it: the build program compiled every rule module in
// the collection for this host.
int main() {
    std::puts("all-rules-compile ok");
    return 0;
}
