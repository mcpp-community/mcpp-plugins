__global__ void cuda_kernel(int* value){*value=1;}
extern "C" int cuda_anchor(){return 1;}
