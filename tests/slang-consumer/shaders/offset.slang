// A second shader, so the fixture has two modules and can tell a per-file
// option from a global one: `OFFSET_ENTRY` is defined for this file alone.
struct Push {
    float a;
    uint  n;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

[[vk::binding(0, 0)]]
RWStructuredBuffer<float> data;

#ifdef OFFSET_ENTRY
#define ENTRY offsetMainDefined
#else
#define ENTRY offsetMain
#endif

[shader("compute")]
[numthreads(64, 1, 1)]
void ENTRY(uint3 tid : SV_DispatchThreadID) {
    const uint i = tid.x;
    if (i >= push.n) return;
    data[i] = data[i] + push.a;
}
