// The seam: a module of this project that imports the GENERATED module.
//
// This is the shape every example under `examples/09-heterogeneous` has. It is
// also where the boundary stops being C. Above this line callers pass spans;
// below it, pointers and a count, which is the one shape every device API
// agrees on.
//
// The generated names arrive in a namespace that is the boundary module's own
// path -- `island_interface::kernels` -- and a directory below the layout root
// extends it, which is why `scale` is reached through `::image`. `saxpy` and
// `scale` are the short spellings of `island_saxpy` and `island_scale`: the
// authored names are the symbols and are exported too.
export module island_interface.app;

import std;
import island_interface.kernels;

export namespace island_interface {

std::optional<std::vector<float>>
saxpy(float a, std::span<const float> x, std::span<const float> y) {
    if (x.size() != y.size()) return std::nullopt;
    std::vector<float> out(x.size());
    if (kernels::saxpy(a, x.data(), y.data(), out.data(),
                       static_cast<unsigned>(x.size())) != 0)
        return std::nullopt;
    return out;
}

bool scale(float a, std::span<float> v) {
    return kernels::image::island_scale(a, v.data(),
                                        static_cast<unsigned>(v.size())) == 0;
}

} // namespace island_interface
