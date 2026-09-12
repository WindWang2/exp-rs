// src/operators/runtime/nvml_inventory.cpp — dlopen-based NVML probe.
// Every NVML entry point is resolved through dlsym; a missing symbol or
// library degrades to available=false with the reason recorded. No
// link-time NVIDIA dependency is introduced.
#include "operators/runtime/nvml_inventory.h"

#include <QProcessEnvironment>

#include <algorithm>

#ifdef _WIN32
#else
#include <dlfcn.h>
#endif

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
  const QString noNvml = env.value( QStringLiteral( "SICNU_MODEL_NO_NVML" ) );
  if ( noNvml == QStringLiteral( "1" ) || noNvml.compare( QStringLiteral( "true" ), Qt::CaseInsensitive ) == 0 )
  {
    inventory.unavailableReason = "NVML probe disabled by SICNU_MODEL_NO_NVML";
    return inventory;
  }

#ifdef _WIN32
  // The NVML probe is a POSIX-lane feature; Windows builds keep the honest
  // "unavailable" verdict (the historical env-var detection still applies).
  inventory.unavailableReason = "NVML probe not implemented on this platform";
  return inventory;
#else
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

  // A corrupt driver-reported count must not become a huge allocation.
  inventory.devices.reserve( std::min<unsigned int>( count, 64 ) );
  for ( unsigned int i = 0; i < count && i < 64u; ++i )
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
#endif
}

} // namespace sicnu::operators::runtime
