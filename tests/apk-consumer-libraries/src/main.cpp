// The application object the Kotlin activity loads. What this fixture measures
// is the package around it, so it only has to link on the Android rows and run
// on the host.
#include <cstdio>
#include <string>

int main() {
    std::puts(("apk-consumer-libraries " + std::to_string(11)).c_str());
    return 0;
}
