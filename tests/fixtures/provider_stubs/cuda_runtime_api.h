// Compile-stub of the CUDA runtime API surface used by
// tensorrt_provider.cpp — -fsyntax-only TU gating only.
#pragma once
#include <cstddef>
using cudaError_t = int;
constexpr cudaError_t cudaSuccess = 0;
using cudaStream_t = void *;
enum cudaMemcpyKind { cudaMemcpyHostToDevice, cudaMemcpyDeviceToHost };
inline cudaError_t cudaSetDevice( int ) { return cudaSuccess; }
inline cudaError_t cudaStreamCreate( cudaStream_t * ) { return cudaSuccess; }
inline cudaError_t cudaStreamDestroy( cudaStream_t ) { return cudaSuccess; }
inline cudaError_t cudaStreamSynchronize( cudaStream_t ) { return cudaSuccess; }
inline cudaError_t cudaMalloc( void **, std::size_t ) { return cudaSuccess; }
inline cudaError_t cudaFree( void * ) { return cudaSuccess; }
inline cudaError_t cudaMemcpyAsync( void *, const void *, std::size_t, cudaMemcpyKind, cudaStream_t )
{
  return cudaSuccess;
}
inline cudaError_t cudaMemcpy( void *, const void *, std::size_t, cudaMemcpyKind )
{
  return cudaSuccess;
}
