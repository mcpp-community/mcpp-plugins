#include <cstdio>

// The Swift function this program calls, declared here rather than taken from
// the header rules-swift generates. That header is written for C and
// Objective-C callers, and which of its declarations a plain C++ translation
// unit sees depends on the Swift version; `check-swift.sh` reads the header and
// records what it declares, while this program depends only on the symbol the
// `@_cdecl` attribute fixes.
extern "C" int swift_consumer_answer(int base);

int main() {
    const int answer = swift_consumer_answer(2);
    std::printf("swift-consumer: cpp side, answer %d\n", answer);
    return answer == 42 ? 0 : 1;
}
