#include <answer.h>

#include <cstdio>

int main() {
    std::printf("cmake-cross: the subproject was compiled against %s\n", answer_libc());
    return 0;
}
