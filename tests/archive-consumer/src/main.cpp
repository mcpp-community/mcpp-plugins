// Reads two files of the archive from beside the executable.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static std::string first_line(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::string line;
    std::getline(in, line);
    return line;
}

int main(int, char** argv) {
    const auto dir = std::filesystem::absolute(argv[0]).parent_path() / "runtime" / "bundle";
    std::printf("archive-consumer: '%s', '%s'\n",
                first_line(dir / "greeting.txt").c_str(),
                first_line(dir / "nested" / "deep.txt").c_str());
}
