// src/python/isolated/shared_memory_segment.cpp
#include "shared_memory_segment.h"

#include <QSharedMemory>
#include <QNativeIpcKey>
#include <QUuid>

#include <cstring>

namespace sicnu::python::isolated {

static constexpr size_t kHeaderSize = sizeof( SharedMemorySegment::Header );
static_assert( sizeof( SharedMemorySegment::Header ) == 32,
               "Header must be 32 bytes so Python can skip it with a fixed offset" );

SharedMemorySegment::SharedMemorySegment() = default;

SharedMemorySegment::~SharedMemorySegment()
{
  detach();
}

size_t SharedMemorySegment::dtypeSize( DType dtype )
{
  switch ( dtype )
  {
    case DType::Float32: return 4;
    case DType::UInt8:   return 1;
    case DType::Int32:   return 4;
  }
  return 0;
}

bool SharedMemorySegment::create( int width, int height, int bands, DType dtype )
{
  if ( width <= 0 || height <= 0 || bands <= 0 )
    return false;

  const size_t elemSize = dtypeSize( dtype );
  if ( elemSize == 0 )
    return false;

  m_payloadSize = static_cast<size_t>( width ) * height * bands * elemSize;
  const size_t totalSize = kHeaderSize + m_payloadSize;

  // Generate a unique key. Use PosixRealtime (shm_open) explicitly, not the
  // legacy SystemV default, so Python's multiprocessing.shared_memory can
  // attach to the same segment (it only supports POSIX shm_open).
  m_key = QStringLiteral( "sicnu_shm_%1" ).arg(
            QUuid::createUuid().toString( QUuid::WithoutBraces ) );

  m_shm = std::make_unique<QSharedMemory>();
  m_shm->setNativeKey( m_key, QNativeIpcKey::Type::PosixRealtime );

  if ( !m_shm->create( static_cast<int>( totalSize ) ) )
  {
    m_shm.reset();
    m_key.clear();
    m_payloadSize = 0;
    return false;
  }

  // Write the header.
  Header hdr{};
  const QByteArray keyBytes = m_key.toUtf8();
  const size_t copyLen = std::min( static_cast<size_t>( keyBytes.size() ), sizeof( hdr.uuid ) );
  std::memcpy( hdr.uuid, keyBytes.constData(), copyLen );
  hdr.width = width;
  hdr.height = height;
  hdr.bands = bands;
  hdr.dtype = static_cast<int32_t>( dtype );

  std::memcpy( m_shm->data(), &hdr, kHeaderSize );
  return true;
}

bool SharedMemorySegment::attach( const QString &key )
{
  if ( key.isEmpty() )
    return false;

  m_key = key;
  m_shm = std::make_unique<QSharedMemory>();
  m_shm->setNativeKey( m_key, QNativeIpcKey::Type::PosixRealtime );

  if ( !m_shm->attach() )
  {
    m_shm.reset();
    m_key.clear();
    return false;
  }

  // Read the header to recover payload size.
  if ( static_cast<size_t>( m_shm->size() ) >= kHeaderSize )
  {
    Header hdr;
    std::memcpy( &hdr, m_shm->data(), kHeaderSize );
    const size_t elemSize = dtypeSize( static_cast<DType>( hdr.dtype ) );
    m_payloadSize = static_cast<size_t>( hdr.width ) * hdr.height * hdr.bands * elemSize;
  }
  return true;
}

void SharedMemorySegment::detach()
{
  if ( m_shm && m_shm->isAttached() )
    m_shm->detach();
  m_shm.reset();
  m_key.clear();
  m_payloadSize = 0;
}

bool SharedMemorySegment::write( const void *data, size_t bytes )
{
  if ( !m_shm || !m_shm->isAttached() || !data )
    return false;
  if ( bytes > m_payloadSize )
    return false;
  std::memcpy( static_cast<char *>( m_shm->data() ) + kHeaderSize, data, bytes );
  return true;
}

const void *SharedMemorySegment::payload() const
{
  if ( !m_shm || !m_shm->isAttached() )
    return nullptr;
  return static_cast<const char *>( m_shm->data() ) + kHeaderSize;
}

void *SharedMemorySegment::payload()
{
  if ( !m_shm || !m_shm->isAttached() )
    return nullptr;
  return static_cast<char *>( m_shm->data() ) + kHeaderSize;
}

QString SharedMemorySegment::key() const
{
  return m_key;
}

QString SharedMemorySegment::nativeKey() const
{
  if ( !m_shm )
    return {};
  return m_shm->nativeKey();
}

size_t SharedMemorySegment::payloadSize() const
{
  return m_payloadSize;
}

bool SharedMemorySegment::isAttached() const
{
  return m_shm && m_shm->isAttached();
}

} // namespace sicnu::python::isolated
