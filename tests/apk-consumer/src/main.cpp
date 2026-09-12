#include <cstdio>
#include <string>

// This fixture asserts PACKAGING (and, when the emulator lane runs, a real
// launch); nothing else here runs on a device by default -- see dist/
// apk.cppm's header and this fixture's own mcpp.toml for what is measured
// about the row instead. The engine compiles this file as a translation
// unit of `lib<name>.so`, exactly as it does for any other member of the
// shared object -- an `app` target's `main` keeps its ordinary meaning as a
// compiled entry point; the platform's own entry into the library is
// `android.app.NativeActivity`, named in the manifest this build's
// `dist-apk` step generates, and is not this function.
//
// `std::to_string` FORCES THE libc++ DEPENDENCY THIS FIXTURE MEASURES.
// `dist-apk` bundles `libc++_shared.so` only when the closure NEEDs it
// (`needs_libcxx_shared`, read from the dynamic section); a `main` that
// touched nothing but `<cstdio>` would not exercise that path honestly, so
// this one builds its marker through `std::string`.
int main() {
    std::string marker = "1-2-3, count=" + std::to_string(3);
    std::puts(marker.c_str());
    return 0;
}
