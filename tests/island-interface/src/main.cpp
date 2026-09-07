// The consumer. It imports the SEAM, not the generated module, and includes
// nothing: the generated header exists for the island's compiler, which does
// not read modules.
//
// Nothing here names `saxpy_device`. That is the property the seam exists for:
// which island is underneath -- the device half or the host one -- is not
// visible from this file, and neither is the fact that a boundary was
// generated at all.
import std;
import island_interface.app;

int main() {
    const std::vector<float> x{1, 2, 3, 4};
    const std::vector<float> y{10, 20, 30, 40};

    auto out = island_interface::saxpy(2.0f, x, y);
    if (!out) {
        std::cout << "BAD: saxpy failed\n";
        return 1;
    }
    if (!island_interface::scale(0.5f, *out)) {
        std::cout << "BAD: scale failed\n";
        return 1;
    }

    // (2*1+10)/2, (2*2+20)/2, (2*3+30)/2, (2*4+40)/2
    const float want[4] = {6, 12, 18, 24};
    bool ok = out->size() == 4;
    for (std::size_t i = 0; ok && i < out->size(); ++i) {
        std::cout << std::format("out[{}]={} ", i, (*out)[i]);
        if ((*out)[i] != want[i]) ok = false;
    }
    std::cout << "\n";
    // Two entry points rather than one, because a generator that re-exported
    // only the first would still satisfy a single-function fixture.
    std::cout << (ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
