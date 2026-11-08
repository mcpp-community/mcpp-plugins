// A CUDA implementation retained beside SYCL. A pure SYCL build must
// leave it out without requiring a CUDA-source rule to consume it.
extern "C" __global__ void unused_cuda_kernel() {}
