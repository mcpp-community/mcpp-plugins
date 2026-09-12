#include <cstdio>
#include <string>

#if defined(__ANDROID__)
#include <android/log.h>
#include <android/native_activity.h>
#endif

// This fixture asserts PACKAGING, and, on the row `adb-run` serves, a real
// launch through `mcpp run --format apk` -- see dist/apk.cppm's header and
// this fixture's own mcpp.toml for what is measured about the row. The
// engine compiles this file as a translation unit of `lib<name>.so`,
// exactly as it does for any other member of the shared object -- an
// `app` target's `main` keeps its ordinary meaning as a compiled entry
// point for the HOST row, where this program is linked and run directly.
// On Android, the platform never calls `main`: it loads the library and
// calls `android.app.NativeActivity`'s own native entry point,
// `ANativeActivity_onCreate`, named in the manifest `dist-apk` generates.
// Both are defined here so the one source file works on both rows.
//
// `std::to_string` FORCES THE libc++ DEPENDENCY THIS FIXTURE MEASURES.
// `dist-apk` bundles `libc++_shared.so` only when the closure NEEDs it
// (`needs_libcxx_shared`, read from the dynamic section); a program that
// touched nothing but `<cstdio>` would not exercise that path honestly, so
// this one builds its marker through `std::string`, on both rows.
namespace {
std::string marker() {
    return "1-2-3, count=" + std::to_string(3);
}
}  // namespace

int main() {
    std::puts(marker().c_str());
    return 0;
}

#if defined(__ANDROID__)
// `adb-run` sets `log.redirect-stdio`, so this process's own stdout reaches
// logcat the same way it would on the host row -- `std::puts` here is not
// decorative, though a locked-down device can still refuse the property
// (measured on a real arm64-v8a phone: "Failed to set property
// 'log.redirect-stdio'"), which is exactly why `__android_log_print` is
// also called, unconditionally, as the channel that does not depend on it.
// `ANativeActivity_finish` ends the ACTIVITY; MEASURED 2026-09-12: it does
// not end the PROCESS (ActivityManager keeps it as a cached, reusable
// process instead), so `adb-run` itself does not wait on `pidof` alone --
// it also watches for this activity's own record to disappear from
// `dumpsys activity activities`, and force-stops the process once it does.
extern "C" void ANativeActivity_onCreate(ANativeActivity* activity, void*, size_t) {
    std::puts(marker().c_str());
    __android_log_print(ANDROID_LOG_INFO, "apk-consumer", "1-2-3");
    ANativeActivity_finish(activity);
}
#endif
