#include <pinned.h>

#include <cstdio>

int main() {
    std::printf("cmake-not-relocatable: pinned says %d\n", pinned_answer());
    return pinned_answer() == 7 ? 0 : 1;
}
