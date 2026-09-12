// The same computation the GLSL fixture runs, written in Slang: out = a*x + y.
//
// Written with a generic so the file exercises what Slang has and GLSL does not.
// One storage buffer holds all three vectors, so the host side needs one
// allocation and one descriptor.
struct Push {
    float a;
    uint  n;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

[[vk::binding(0, 0)]]
RWStructuredBuffer<float> data;

// A generic the compiler specialises. GLSL would need a macro and a second copy
// of the body for every type; this is the difference the language buys.
T saxpy<T : __BuiltinFloatingPointType>(T a, T x, T y) {
    return a * x + y;
}


[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID) {
    const uint i = tid.x;
    if (i >= push.n) return;
    data[2 * push.n + i] = saxpy(push.a, data[i], data[push.n + i]);
}
