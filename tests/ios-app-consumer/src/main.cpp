#include <cstdio>
// Exits 7, not 0: a runner that loses the program's status returns 0, and the
// macOS lane asserts the 7 (`simctl launch` returns 0 for an application that
// exits 7; `simctl spawn`, which `simctl-run` 0.3.0 uses for this bundle,
// returns the application's own status).
int main() { std::puts("1-2-3"); return 7; }
