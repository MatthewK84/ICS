// Compile-only check that the CUDA image builds a kernel with each host
// compiler (ICS-004). It is not part of the CMake build and needs no GPU.
// Built for sm_89, the architecture of the field server's NVIDIA L40S GPUs.
#include <cstddef>

namespace {

__global__ void axpy(float alpha, const float* x, float* y, std::size_t count) {
  const std::size_t index = (static_cast<std::size_t>(blockIdx.x) * blockDim.x) + threadIdx.x;
  if (index < count) {
    y[index] = (alpha * x[index]) + y[index];
  }
}

}  // namespace

int main() {
  constexpr std::size_t kCount = 256U;
  float* x = nullptr;
  float* y = nullptr;
  if (cudaMalloc(&x, kCount * sizeof(float)) != cudaSuccess || cudaMalloc(&y, kCount * sizeof(float)) != cudaSuccess) {
    return 1;
  }
  axpy<<<1, kCount>>>(2.0F, x, y, kCount);
  const bool ok = cudaDeviceSynchronize() == cudaSuccess;
  static_cast<void>(cudaFree(x));
  static_cast<void>(cudaFree(y));
  return ok ? 0 : 1;
}
