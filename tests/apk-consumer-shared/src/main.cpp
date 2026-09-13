#include <cstdio>
#include <string>

#include "apk_consumer_dep.h"

#if defined(__ANDROID__)
#include <android/log.h>
#include <android/native_activity.h>
#endif

// The marker comes from the dependency, so the app's object NEEDs
// lib<dep>.so -- which is the fact `dist-apk` has to act on.
namespace {
std::string marker() { return std::string(apk_consumer_dep_marker()) + ", count=" + std::to_string(3); }
}  // namespace

int main() {
    std::puts(marker().c_str());
    return 0;
}

#if defined(__ANDROID__)
extern "C" void ANativeActivity_onCreate(ANativeActivity* activity, void*, size_t) {
    std::puts(marker().c_str());
    __android_log_print(ANDROID_LOG_INFO, "apk-consumer-shared", "%s", marker().c_str());
    ANativeActivity_finish(activity);
}
#endif
