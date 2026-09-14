#include <metal_stdlib>
using namespace metal;

kernel void scale_values(buffer<float, access::read_write> values [[buffer(0)]],
                         uint index [[thread_position_in_grid]]) {
    values[index] = values[index] * 2.0;
}
