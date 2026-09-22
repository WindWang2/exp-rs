// fsync_compat.h — portable file/directory durability for the chunk family.
//
// POSIX: fsync(2). Windows: FlushFileBuffers (real durability — a silent
// no-op previously allowed the journal to outlive unflushed tile bytes
// (#1228 / #1186 item 33)). File flush failure throws; directory flush is
// best-effort (BACKUP_SEMANTICS open can fail on some volumes).
#pragma once

#include <string>
#include <stdexcept>

#if !defined( _WIN32 )
#include <fcntl.h>
#include <unistd.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace sicnu::runtime::chunk
{

inline void fsyncPathCompat( const std::string &path, bool directory )
{
#if !defined( _WIN32 )
    const int flags = directory ? ( O_RDONLY | O_DIRECTORY ) : O_RDONLY;
    const int fd = ::open( path.c_str(), flags );
    if ( fd < 0 )
    {
        if ( !directory )
            throw std::runtime_error( "fsync: cannot open " + path );
        return;
    }
    if ( ::fsync( fd ) != 0 && !directory )
    {
        ::close( fd );
        throw std::runtime_error( "fsync failed for " + path );
    }
    ::close( fd );
#else
    const int wlen = MultiByteToWideChar( CP_UTF8, 0, path.c_str(), -1, nullptr, 0 );
    if ( wlen <= 0 )
    {
        if ( !directory )
            throw std::runtime_error( "fsync: cannot widen path " + path );
        return;
    }
    std::wstring wpath( static_cast<std::size_t>( wlen ), L'\0' );
    MultiByteToWideChar( CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen );
    const DWORD flags = directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL;
    const DWORD access = directory ? GENERIC_READ : GENERIC_WRITE;
    const HANDLE handle =
      CreateFileW( wpath.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                   nullptr, OPEN_EXISTING, flags, nullptr );
    if ( handle == INVALID_HANDLE_VALUE )
    {
        if ( !directory )
            throw std::runtime_error( "fsync: cannot open " + path );
        return;
    }
    if ( !FlushFileBuffers( handle ) && !directory )
    {
        CloseHandle( handle );
        throw std::runtime_error( "fsync: FlushFileBuffers failed for " + path );
    }
    CloseHandle( handle );
#endif
}

/// Best-effort wrapper: never throws (legacy call sites that tolerate miss).
inline void fsyncPathBestEffort( const std::string &path, bool directory = false )
{
    try
    {
        fsyncPathCompat( path, directory );
    }
    catch ( ... )
    {
    }
}

} // namespace sicnu::runtime::chunk
