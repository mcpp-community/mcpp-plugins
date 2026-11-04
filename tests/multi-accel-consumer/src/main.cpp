extern "C" int cuda_anchor();
extern "C" int sycl_anchor();
// Exercise both host objects without requiring a physical GPU or a queue.
int main(){return cuda_anchor()+sycl_anchor()==3?0:1;}
