// src/python/isolated/shared_memory_segment.cpp
#include "shared_memory_segment.h"

#include <QSharedMemory>
#include <QNativeIpcKey>
#include <QUuid>

#include <cstring>

// POSIX shm lifetime management (ADR 0064 leak fix). QSharedMemory in
// PosixRealtime mode backs each segment with TWO objects under /dev/shm:
//   - sicnu_shm_<uuid>   : the shared memory object itself
//   - sem.sicnu_shm_<uuid>: a POSIX semaphore Qt uses for internal locking
// Neither is reclaimed until something unlinks it, so a process that creates
// segments without unlinking leaks both. We reclaim them here.
#if defined( Q_OS_LINUX ) || defined( Q_OS_MACOS )
#include <fcntl.h>     // O_* constants (kept for completeness)
#include <sys/mman.h>  // shm_unlink
#include <semaphore.h> // sem_unlink
#define SICNU_HAVE_POSIX_SHM 1
#endif

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
    case DType::UInt16:  return 2;
    case DType::Float64: return 8;
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
    m_isOwner = false;
    m_unlinked = false;
    return false;
  }

  // We created the POSIX shm object + its semaphore: we own the duty to
  // unlink them (see detach()/unlink()).
  m_isOwner = true;
  m_unlinked = false;

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
    m_isOwner = false;
    m_unlinked = false;
    return false;
  }

  // Attaching does not transfer ownership: the creator is still responsible
  // for unlinking the backing objects.
  m_isOwner = false;
  m_unlinked = false;

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
  // Reclaim the POSIX backing objects before dropping the mapping. Only the
  // creator unlinks; a segment we merely attached() must not unlink out from
  // under the owner.
  if ( m_isOwner && !m_unlinked )
    unlink();

  if ( m_shm && m_shm->isAttached() )
    m_shm->detach();
  m_shm.reset();
  m_key.clear();
  m_payloadSize = 0;
}

bool SharedMemorySegment::unlink()
{
  if ( m_unlinked )
    return true;
  if ( !m_isOwner || m_key.isEmpty() )
    return false;

#ifdef SICNU_HAVE_POSIX_SHM
  // POSIX shm/semaphore names begin with a single '/'. Qt's PosixRealtime
  // backend derives both the shm object and the semaphore from the key; on
  // Linux they surface in /dev/shm as "sicnu_shm_<uuid>" and
  // "sem.sicnu_shm_<uuid>" respectively. We unlink both so neither leaks.
  const QByteArray posixName = '/' + m_key.toUtf8();

  // shm_unlink is idempotent enough: ENOENT (already gone) is not an error
  // for us. Best-effort on the semaphore too.
  const int shmRc = ::shm_unlink( posixName.constData() );
  // sem_unlink name: Qt passes the key to sem_open; glibc prepends "sem.".
  // The leading-slash form matches the POSIX convention Qt follows.
  const int semRc = ::sem_unlink( posixName.constData() );
  (void)semRc; // best-effort; ignore "no such semaphore"

  m_unlinked = true;
  return shmRc == 0;
#else
  // Windows / unsupported: nothing to unlink.
  m_unlinked = true;
  return false;
#endif
}

bool SharedMemorySegment::isOwner() const
{
  return m_isOwner;
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
