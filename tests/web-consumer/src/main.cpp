import std;

// `1-2-3`, the marker the design record's own wasm measurements use
// (mcpp's `tests/e2e/650_wasm_row_names_its_launcher.sh` and
// `ci-target-matrix.yml`'s "wasm32-emscripten runs through its payload's
// runner" step both print it), so this fixture's expectation is not this
// repository's own invention.
int main() {
    std::vector<int> v{3, 1, 2};
    std::ranges::sort(v);
    std::print("{}-{}-{}\n", v[0], v[1], v[2]);
}
