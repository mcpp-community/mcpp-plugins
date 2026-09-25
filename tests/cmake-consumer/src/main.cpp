#include <greet.h>

#include <cstdio>

int main() {
    std::printf("cmake-consumer: greet says %d\n", greet_answer());
    return greet_answer() == 42 ? 0 : 1;
}
