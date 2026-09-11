// src/operators/runtime/nvml_inventory.cpp — dlopen-based NVML probe.
// Every NVML entry point is resolved through dlsym; a missing symbol or
// library degrades to available=false with the reason recorded. No
// link-time NVIDIA dependency is introduced.
#include "operators/runtime/nvml_inventory.h"

#include <QProcessEnvironment>

#include <dlfcn.h>

#include <cstring>

namespace sicnu::operators::runtime {

namespace {

// Minimal NVML API surface (types mirrored from nvml.h; nvmlDevice_t is an
// opaque handle so void* is exact).
using NvmlReturn = int; // nvmlReturn_t; NVML_SUCCESS == 0
constexpr NvmlReturn kNvmlSuccess = 0;
using NvmlDeviceHandle = void *;
using NvmlInitFn = NvmlReturn ( * )();
using NvmlShutdownFn = NvmlReturn ( * )();
using NvmlGetCountFn = NvmlReturn ( * )( unsigned int * );
using NvmlGetHandleFn = NvmlReturn ( * )( unsigned int, NvmlDeviceHandle * );
using NvmlGetNameFn = NvmlReturn ( * )( NvmlDeviceHandle, char *, unsigned int );
struct NvmlMemory
{
  unsigned long long total;
  unsigned long long free;
  unsigned long long used;
};
using NvmlGetMemoryFn = NvmlReturn ( * )( NvmlDeviceHandle, NvmlMemory * );
using NvmlGetComputeFn = NvmlReturn ( * )( NvmlDeviceHandle, int *, int * );

unsigned long long toMb( unsigned long long bytes )
{
  return ( bytes + ( 1ULL << 20 ) - 1 ) / ( 1ULL << 20 );
}

} // namespace

NvidiaInventory NvidiaInventory::probe()
{
  NvidiaInventory inventory;
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if ( env.value( QStringLiteral( "SICNU_MODEL_NO_NVML" ) ) == QStringLiteral( "1" ) )
  {
    inventory.unavailableReason = "NVML probe disabled by SICNU_MODEL_NO_NVML=1";
    return inventory;
  }

  // The versioned soname first (driver installs), then the unversioned
  // development name — either is a real NVML.
  void *handle = dlopen( "libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL );
  if ( !handle )
    handle = dlopen( "libnvidia-ml.so", RTLD_LAZY | RTLD_LOCAL );
  if ( !handle )
  {
    inventory.unavailableReason = "libnvidia-ml not loadable (no NVIDIA driver on this host)";
    return inventory;
  }

  auto sym = [ & ]( const char *name ) { return dlsym( handle, name ); };
  // POSIX dlsym returns void* for function symbols; converting back needs a
  // C-style cast (reinterpret_cast between object and function pointers is
  // ill-formed). ISO pp. §J.5.7 / POSIX require this to work.
  const auto init = ( NvmlInitFn )sym( "nvmlInit_v2" );
  const auto shutdown = ( NvmlShutdownFn )sym( "nvmlShutdown" );
  const auto getCount = ( NvmlGetCountFn )sym( "nvmlDeviceGetCount_v2" );
  const auto getHandle = ( NvmlGetHandleFn )sym( "nvmlDeviceGetHandleByIndex_v2" );
  const auto getName = ( NvmlGetNameFn )sym( "nvmlDeviceGetName" );
  const auto getMemory = ( NvmlGetMemoryFn )sym( "nvmlDeviceGetMemoryInfo" );
  const auto getCompute = ( NvmlGetComputeFn )sym( "nvmlDeviceGetCudaComputeCapability" );
  if ( !init || !shutdown || !getCount || !getHandle || !getName || !getMemory )
  {
    inventory.unavailableReason = "libnvidia-ml loaded but the required symbols are missing";
    dlclose( handle );
    return inventory;
  }
  // Compute capability is optional (very old drivers).
  ( void )getCompute;

  if ( init() != kNvmlSuccess )
  {
    inventory.unavailableReason = "nvmlInit failed (driver present but unusable)";
    dlclose( handle );
    return inventory;
  }

  unsigned int count = 0;
  if ( getCount( &count ) != kNvmlSuccess )
  {
    inventory.unavailableReason = "nvmlDeviceGetCount failed";
    shutdown();
    dlclose( handle );
    return inventory;
  }

  inventory.devices.reserve( count );
  for ( unsigned int i = 0; i < count; ++i )
  {
    NvmlDeviceHandle deviceHandle = nullptr;
    if ( getHandle( i, &deviceHandle ) != kNvmlSuccess || !deviceHandle )
      continue; // a hot-plug race on one index must not poison the rest
    NvidiaDeviceInfo info;
    info.index = static_cast<int>( i );
    char name[96] = {};
    if ( getName( deviceHandle, name, sizeof( name ) ) == kNvmlSuccess && name[0] != '\0' )
      info.name = name;
    else
      info.name = "cuda:" + std::to_string( i );
    NvmlMemory memory {};
    if ( getMemory( deviceHandle, &memory ) == kNvmlSuccess )
    {
      info.totalVramMb = static_cast<int>( toMb( memory.total ) );
      info.freeVramMb = static_cast<int>( toMb( memory.free ) );
    }
    if ( getCompute )
    {
      int major = 0;
      int minor = 0;
      if ( getCompute( deviceHandle, &major, &minor ) == kNvmlSuccess )
      {
        info.computeMajor = major;
        info.computeMinor = minor;
      }
    }
    inventory.devices.push_back( std::move( info ) );
  }

  shutdown();
  dlclose( handle );
  inventory.available = true;
  return inventory;
}

} // namespace sicnu::operators::runtime
