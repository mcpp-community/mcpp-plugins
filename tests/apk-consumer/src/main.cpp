#include <cstdio>

// This fixture asserts PACKAGING, not execution: nothing here runs on a
// device or emulator (see dist/apk.cppm's header and this fixture's own
// mcpp.toml for what is measured about the row instead). The engine compiles
// this file as a translation unit of `lib<name>.so`, exactly as it does for
// any other member of the shared object -- an `app` target's `main` keeps
// its ordinary meaning as a compiled entry point; the platform's own entry
// into the library is `android.app.NativeActivity`, named in the manifest
// this build's `dist-apk` step generates, and is not this function.
int main() {
    std::puts("1-2-3");
    return 0;
}
