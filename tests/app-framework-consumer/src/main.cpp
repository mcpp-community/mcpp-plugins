#include <cstdio>

#include "app_framework_dep.h"

// The marker comes from the dependency, so the program loads the dylib before
// it prints anything; the exit status is 7 so that a runner which reports its
// own status instead of the program's is visible.
int main(int argc, char**) {
    std::printf("%s argc=%d\n", app_framework_dep_marker(), argc);
    return 7;
}
