// The seam: a module of this project that imports the GENERATED module.
//
// This is the shape every example under `examples/09-heterogeneous` has, and it
// is the one the fixture was missing. `src/main.cpp` is not a module unit, so
// its `import` said nothing about the case that matters here -- a module
// interface written by this project importing a module interface written into
// the build directory during the same build. The two have to be ordered, and
// the ordering comes from the scan seeing the import rather than from anything
// this file declares.
//
// It is also where the boundary stops being C. Above this line callers pass
// spans; below it, pointers and a count, which is the one shape every device
// API agrees on.
export module island_interface.app;

import std;
import island_interface.kernels;

export namespace island_interface {

std::optional<std::vector<float>>
saxpy(float a, std::span<const float> x, std::span<const float> y) {
    if (x.size() != y.size()) return std::nullopt;
    std::vector<float> out(x.size());
    if (saxpy_device(a, x.data(), y.data(), out.data(),
                     static_cast<unsigned>(x.size())) != 0)
        return std::nullopt;
    return out;
}

bool scale(float a, std::span<float> v) {
    return scale_device(a, v.data(), static_cast<unsigned>(v.size())) == 0;
}

} // namespace island_interface
