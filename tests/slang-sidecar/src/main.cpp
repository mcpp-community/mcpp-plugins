// The same shape as the embedded consumer. Nothing here knows that the payload
// is a file, or that it was written in Slang.
import std;
import slang_sidecar.shaders;

int main() {
    const auto s = slang_sidecar::shaders::scale();
    if (s.code == nullptr || s.size_bytes == 0) {
        // The failure this storage can have and the other two cannot: the file
        // was not where the program looked. Reported here rather than left to
        // crash, because "started from the wrong directory" is the whole of its
        // contract and a null pointer says nothing.
        std::cout << "BAD: the sidecar payload was not found\n";
        return 1;
    }
    const bool ok = s.size_bytes >= 4 && s.code[0] == 0x07230203u;
    std::cout << std::format("magic={:08x} bytes={} {}\n", s.code[0], s.size_bytes,
                             ok ? "ok" : "BAD");
    std::cout << (ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
