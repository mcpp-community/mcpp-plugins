#include <cstdio>

// Printed by the AppImage when it runs, which is what the end-to-end check
// greps for. An AppImage that starts and produces nothing is the failure this
// whole category is most exposed to, so the assertion is on OUTPUT rather than
// on the file existing.
int main() { std::puts("appimage-consumer ok"); return 0; }
