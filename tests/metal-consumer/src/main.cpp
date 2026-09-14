#include <cstdio>
#include <cstring>
#include <string>

// Opens the libraries the build placed beside the program and checks the
// Metal library magic ("MTLB"), so the criterion is the file and no GPU.
int main(int, char** argv) {
#if defined(__APPLE__)
    std::string dir = argv[0];
    const auto slash = dir.rfind('/');
    dir = slash == std::string::npos ? std::string(".") : dir.substr(0, slash);
    for (const char* name : { "scale", "tint", "tint_red" }) {
        const std::string path = dir + "/metallib/" + name + ".metallib";
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) { std::printf("missing %s\n", path.c_str()); return 1; }
        char magic[4] = {0};
        const auto n = std::fread(magic, 1, 4, f);
        std::fclose(f);
        if (n != 4 || std::memcmp(magic, "MTLB", 4) != 0) {
            std::printf("not a Metal library: %s\n", path.c_str());
            return 1;
        }
    }
#else
    (void)argv;
#endif
    std::printf("metal-consumer ok\n");
    return 0;
}
