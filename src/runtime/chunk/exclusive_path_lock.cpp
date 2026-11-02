// exclusive_path_lock.cpp — see exclusive_path_lock.h for the contract.
#include "exclusive_path_lock.h"

#include "platform/portable.h" // UTF-8 <-> fs::path boundary (ACP-safe on Windows)

#include <cerrno>
#include <cstring>
#include <utility>

#if defined( _WIN32 )
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace sicnu::runtime::chunk
{

ExclusivePathLock::~ExclusivePathLock()
{
    release();
}

ExclusivePathLock::ExclusivePathLock( ExclusivePathLock &&other ) noexcept
{
    *this = std::move( other );
}

ExclusivePathLock &ExclusivePathLock::operator=( ExclusivePathLock &&other ) noexcept
{
    if ( this != &other )
    {
        release();
#if defined( _WIN32 )
        m_handle = std::exchange( other.m_handle, kInvalid );
#else
        m_fd = std::exchange( other.m_fd, kInvalid );
#endif
        m_path = std::move( other.m_path );
    }
    return *this;
}

const char *ExclusivePathLock::refusalName( Refusal r )
{
    switch ( r )
    {
    case Refusal::None:
        return "acquired";
    case Refusal::HeldElsewhere:
        return "held-elsewhere";
    case Refusal::OpenFailed:
        return "open-failed";
    case Refusal::LockFailed:
        return "lock-failed";
    }
    return "unknown";
}

#if defined( _WIN32 )

ExclusivePathLock::Refusal ExclusivePathLock::tryAcquire( const std::string &lockFilePath,
                                                          std::string &errorOut )
{
    release();
    errorOut.clear();
    m_path = lockFilePath;

    HANDLE handle = CreateFileW(
        sicnu::portable::wideFromUtf8( lockFilePath ).c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr );
    if ( handle == INVALID_HANDLE_VALUE )
    {
        errorOut = "CreateFileW failed (" + std::to_string( GetLastError() ) + ") on "
                   + lockFilePath;
        return Refusal::OpenFailed;
    }
    OVERLAPPED overlapped{};
    if ( !LockFileEx( handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                      MAXDWORD, MAXDWORD, &overlapped ) )
    {
        const DWORD err = GetLastError();
        CloseHandle( handle );
        // ERROR_LOCK_VIOLATION is the "someone else holds it" answer.
        errorOut = "LockFileEx failed (" + std::to_string( err ) + ") on " + lockFilePath;
        return err == ERROR_LOCK_VIOLATION ? Refusal::HeldElsewhere : Refusal::LockFailed;
    }
    m_handle = handle;
    return Refusal::None;
}

void ExclusivePathLock::release()
{
    if ( m_handle == kInvalid )
        return;
    OVERLAPPED overlapped{};
    UnlockFileEx( m_handle, 0, MAXDWORD, MAXDWORD, &overlapped );
    CloseHandle( m_handle );
    m_handle = kInvalid;
    m_path.clear();
}

#else

ExclusivePathLock::Refusal ExclusivePathLock::tryAcquire( const std::string &lockFilePath,
                                                          std::string &errorOut )
{
    release();
    errorOut.clear();
    m_path = lockFilePath;

    const int fd = ::open( lockFilePath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666 );
    if ( fd < 0 )
    {
        errorOut = std::string( "open failed: " ) + std::strerror( errno ) + " on "
                   + lockFilePath;
        return Refusal::OpenFailed;
    }
    if ( ::flock( fd, LOCK_EX | LOCK_NB ) != 0 )
    {
        const int err = errno;
        ::close( fd );
        errorOut = std::string( "flock failed: " ) + std::strerror( err ) + " on "
                   + lockFilePath;
        // EWOULDBLOCK means another open description holds the lock; anything
        // else (EINTR aside) is an OS-level failure the caller must surface.
        return err == EWOULDBLOCK || err == EAGAIN ? Refusal::HeldElsewhere
                                                   : Refusal::LockFailed;
    }
    m_fd = fd;
    return Refusal::None;
}

void ExclusivePathLock::release()
{
    if ( m_fd == kInvalid )
        return;
    // Unlock BEFORE close: close alone also drops the lock, but the explicit
    // unlock keeps the release observable/orderable for diagnostics.
    ::flock( m_fd, LOCK_UN );
    ::close( m_fd );
    m_fd = kInvalid;
    m_path.clear();
}

#endif

} // namespace sicnu::runtime::chunk
