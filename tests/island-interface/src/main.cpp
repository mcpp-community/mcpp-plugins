// The C++ side. It imports the generated module and includes nothing: the
// header exists for the island's compiler, which does not read modules.
import std;
import island_interface.kernels;

int main() {
    float x[4] = {1, 2, 3, 4};
    float y[4] = {10, 20, 30, 40};
    float out[4] = {};

    if (saxpy_device(2.0f, x, y, out, 4) != 0) {
        std::cout << "BAD: saxpy_device failed\n";
        return 1;
    }
    if (scale_device(0.5f, out, 4) != 0) {
        std::cout << "BAD: scale_device failed\n";
        return 1;
    }

    // (2*1+10)/2, (2*2+20)/2, (2*3+30)/2, (2*4+40)/2
    const float want[4] = {6, 12, 18, 24};
    bool ok = true;
    for (int i = 0; i < 4; ++i) {
        std::cout << std::format("out[{}]={} ", i, out[i]);
        if (out[i] != want[i]) ok = false;
    }
    std::cout << "\n";
    // Two entry points rather than one, because a generator that re-exported
    // only the first would still satisfy a single-function fixture.
    std::cout << (ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
