#include <cstdio>

#if defined(__ANDROID__)
#include <android/native_activity.h>
#endif

int apk_graph_lib_answer();

int main() { return apk_graph_lib_answer() == 42 ? 0 : 1; }

#if defined(__ANDROID__)
extern "C" void ANativeActivity_onCreate(ANativeActivity* activity, void*, size_t) {
    std::printf("apk-consumer-graph %d\n", apk_graph_lib_answer());
    ANativeActivity_finish(activity);
}
#endif
