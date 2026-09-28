#include <cstdio>
#include <unicode/uversion.h>
int main() { std::printf("vcpkg-make-port: icu %s\n", U_ICU_VERSION); return 0; }
