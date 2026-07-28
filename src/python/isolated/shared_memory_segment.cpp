// src/python/isolated/shared_memory_segment.cpp
#include "shared_memory_segment.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <QDebug>
#include <QUuid>

namespace sicnu::python::isolated
{

SharedMemorySegment::SharedMemorySegment() = default;

SharedMemorySegment::~SharedMemorySegment()
{
  detach();
}

bool SharedMemorySegment::create( const QString &key, size_t payloadSize, int width, int height, int bands, int dataType )
{
  detach();

  m_key = key.startsWith( '/' ) ? key : QString( "/%1" ).arg( key );
  m_totalSize = sizeof( SharedHeader ) + payloadSize;

  QByteArray keyBytes = m_key.toUtf8();
  shm_unlink( keyBytes.constData() );

  m_fd = shm_open( keyBytes.constData(), O_CREAT | O_RDWR, 0666 );
  if ( m_fd == -1 )
  {
    qWarning() << "SharedMemorySegment: shm_open failed for key" << m_key;
    return false;
  }

  if ( ftruncate( m_fd, m_totalSize ) == -1 )
  {
    qWarning() << "SharedMemorySegment: ftruncate failed for size" << m_totalSize;
    close( m_fd );
    m_fd = -1;
    shm_unlink( keyBytes.constData() );
    return false;
  }

  m_mappedAddress = mmap( nullptr, m_totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0 );
  if ( m_mappedAddress == MAP_FAILED )
  {
    qWarning() << "SharedMemorySegment: mmap failed";
    close( m_fd );
    m_fd = -1;
    shm_unlink( keyBytes.constData() );
    m_mappedAddress = nullptr;
    return false;
  }

  SharedHeader *hdr = header();
  std::memset( hdr, 0, sizeof( SharedHeader ) );
  QString uuid = QUuid::createUuid().toString( QUuid::WithoutBraces );
  std::strncpy( hdr->uuid, uuid.toUtf8().constData(), sizeof( hdr->uuid ) - 1 );
  hdr->width = width;
  hdr->height = height;
  hdr->bands = bands;
  hdr->dataType = dataType;
  hdr->refCount = 1;
  hdr->dataSize = payloadSize;

  return true;
}

bool SharedMemorySegment::attach( const QString &key )
{
  detach();

  m_key = key.startsWith( '/' ) ? key : QString( "/%1" ).arg( key );
  QByteArray keyBytes = m_key.toUtf8();

  m_fd = shm_open( keyBytes.constData(), O_RDWR, 0666 );
  if ( m_fd == -1 )
  {
    return false;
  }

  struct stat st;
  if ( fstat( m_fd, &st ) == -1 )
  {
    close( m_fd );
    m_fd = -1;
    return false;
  }

  m_totalSize = st.st_size;
  m_mappedAddress = mmap( nullptr, m_totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0 );
  if ( m_mappedAddress == MAP_FAILED )
  {
    close( m_fd );
    m_fd = -1;
    m_mappedAddress = nullptr;
    return false;
  }

  return true;
}

void SharedMemorySegment::detach()
{
  if ( m_mappedAddress && m_mappedAddress != MAP_FAILED )
  {
    munmap( m_mappedAddress, m_totalSize );
    m_mappedAddress = nullptr;
  }
  if ( m_fd != -1 )
  {
    close( m_fd );
    m_fd = -1;
  }
  if ( !m_key.isEmpty() )
  {
    QByteArray keyBytes = m_key.toUtf8();
    shm_unlink( keyBytes.constData() );
    m_key.clear();
  }
  m_totalSize = 0;
}

bool SharedMemorySegment::isAttached() const
{
  return m_mappedAddress != nullptr && m_mappedAddress != MAP_FAILED;
}

QString SharedMemorySegment::key() const
{
  return m_key;
}

void *SharedMemorySegment::payload()
{
  if ( !isAttached() )
    return nullptr;
  return static_cast<char *>( m_mappedAddress ) + sizeof( SharedHeader );
}

const void *SharedMemorySegment::payload() const
{
  if ( !isAttached() )
    return nullptr;
  return static_cast<const char *>( m_mappedAddress ) + sizeof( SharedHeader );
}

SharedHeader *SharedMemorySegment::header()
{
  if ( !isAttached() )
    return nullptr;
  return static_cast<SharedHeader *>( m_mappedAddress );
}

const SharedHeader *SharedMemorySegment::header() const
{
  if ( !isAttached() )
    return nullptr;
  return static_cast<const SharedHeader *>( m_mappedAddress );
}

size_t SharedMemorySegment::payloadSize() const
{
  return m_totalSize > sizeof( SharedHeader ) ? m_totalSize - sizeof( SharedHeader ) : 0;
}

} // namespace sicnu::python::isolated
