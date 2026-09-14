#include <metal_stdlib>
using namespace metal;

#include "tint_common.h"

fragment half4 tint_fragment(float4 position [[position]]) {
    return half4(TINT_R, 0.25h, 0.5h, 1.0h);
}
