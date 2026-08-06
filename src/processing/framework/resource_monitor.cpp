// src/processing/framework/resource_monitor.cpp
#include "resource_monitor.h"

#include <qgsapplication.h>

#if defined( Q_OS_LINUX ) || defined( Q_OS_MACOS )
#include <sys/resource.h>
#endif

namespace sicnu {

namespace {

unsigned int defaultRssMb()
{
#if defined( Q_OS_LINUX ) || defined( Q_OS_MACOS )
  struct rusage usage {};
  if ( getrusage( RUSAGE_SELF, &usage ) == 0 )
  {
#ifdef Q_OS_LINUX
    // ru_maxrss is kilobytes on Linux.
    return static_cast<unsigned int>( usage.ru_maxrss / 1024 );
#else
    // ru_maxrss is bytes on macOS.
    return static_cast<unsigned int>( usage.ru_maxrss / ( 1024 * 1024 ) );
#endif
  }
#endif
  return 0;
}

/// Default watermark: 75% of the system's usable physical RAM, or 0 if the
/// size cannot be determined (gate disabled so a misconfigured environment
/// never hard-blocks all task launches).
unsigned int defaultWatermarkMb()
{
  const int sysRam = QgsApplication::systemMemorySizeMb();
  return sysRam > 0 ? static_cast<unsigned int>( sysRam ) * 75u / 100u : 0u;
}

} // namespace

ResourceMonitor::ResourceMonitor()
  : m_limitMb( defaultWatermarkMb() )
  , m_sampler( &defaultRssMb )
{
}

void ResourceMonitor::setMemoryLimitMb( unsigned int mb )
{
  m_limitMb = mb;
}

unsigned int ResourceMonitor::memoryLimitMb() const
{
  return m_limitMb;
}

bool ResourceMonitor::memoryPressureHigh() const
{
  if ( m_limitMb == 0 )
    return false;
  return currentRssMb() >= m_limitMb;
}

void ResourceMonitor::setRssSampler( RssSampler sampler )
{
  m_sampler = sampler ? std::move( sampler ) : RssSampler( &defaultRssMb );
}

unsigned int ResourceMonitor::currentRssMb() const
{
  return m_sampler ? m_sampler() : 0u;
}

} // namespace sicnu
