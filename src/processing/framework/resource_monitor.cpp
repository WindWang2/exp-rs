// src/processing/framework/resource_monitor.cpp
#include "resource_monitor.h"

#include <qgsapplication.h>

#include <QFile>
#include <QString>
#include <QTextStream>

#if defined( Q_OS_LINUX ) || defined( Q_OS_MACOS )
#include <sys/resource.h>
#endif
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#include <mach/task_info.h>
#endif

namespace sicnu {

namespace {

unsigned int defaultRssMb()
{
#ifdef Q_OS_LINUX
  // ADR-0063 amendment: instantaneous current RSS via /proc/self/status
  // (VmRSS, in kB). Unlike getrusage(RUSAGE_SELF).ru_maxrss (the all-time
  // peak, monotonic), VmRSS reflects pages actually resident right now, so
  // the watermark gate reopens as soon as a task frees memory rather than
  // staying closed at the peak forever.
  QFile status( QStringLiteral( "/proc/self/status" ) );
  if ( status.open( QIODevice::ReadOnly | QIODevice::Text ) )
  {
    QTextStream in( &status );
    QString line;
    while ( in.readLineInto( &line ) )
    {
      if ( line.startsWith( QStringLiteral( "VmRSS:" ) ) )
      {
        const QStringList parts = line.split( QChar::Space, Qt::SkipEmptyParts );
        if ( parts.size() >= 2 )
        {
          bool ok = false;
          const unsigned int kb = parts[1].toUInt( &ok );
          if ( ok )
            return kb / 1024u;
        }
        break;
      }
    }
  }
  return 0; // /proc unavailable: gate disabled (falls back to count throttling)
#elif defined( Q_OS_MACOS )
  // mach_task_basic_info.resident_size is bytes of resident memory.
  mach_task_basic_info_data_t info;
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if ( task_info( mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>( &info ), &count ) == KERN_SUCCESS )
  {
    return static_cast<unsigned int>( info.resident_size / ( 1024u * 1024u ) );
  }
  return 0;
#else
  return 0; // unsupported platform: gate disabled
#endif
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
