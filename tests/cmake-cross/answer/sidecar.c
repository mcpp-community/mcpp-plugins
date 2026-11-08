#include <answer.h>
#include <stdio.h>

int main(void) {
    printf("cmake-sidecar: the subproject was compiled against %s\n", answer_libc());
    return 0;
}
