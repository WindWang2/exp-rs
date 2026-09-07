/***************************************************************************
 * exprs/msvc_posix_shim.h
 *
 * MSVC portability shim for the exprs plugin SDK (Tier-3 Windows seam).
 * Emulates the small POSIX surface the plugin layer uses — dirent
 * (opendir/readdir/closedir), dlfcn (dlopen/dlsym/dlclose/dlerror/RTLD_*),
 * lstat, and mkdir(path, mode) — over the Win32/UCRT API so the plugin
 * SDK compiles on Windows without touching POSIX call sites.
 * POSIX builds never include this file.
 ***************************************************************************/
#pragma once

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <direct.h>
#include <io.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstring>
#include <string>

// ---- sys/stat.h predicates missing from MSVC -------------------------------
#if !defined( S_ISREG )
#define S_ISREG( m ) ( ( ( m ) & _S_IFMT ) == _S_IFREG )
#endif
#if !defined( S_ISDIR )
#define S_ISDIR( m ) ( ( ( m ) & _S_IFMT ) == _S_IFDIR )
#endif

// chmod(path, mode): map the POSIX write bit onto the NT read-only attribute.
inline int exprs_chmod( const char *path, int mode )
{
    return _chmod( path, ( mode & 0200 ) ? _S_IWRITE : _S_IREAD );
}
#if !defined( chmod )
#define chmod( path, mode ) exprs_chmod( path, mode )
#endif

// POSIX lstat has no symlink meaning on this shim; stat is the portable probe.
#if !defined( lstat )
#define lstat( path, info ) stat( path, info )
#endif

// mkdir(path, mode): MSVC takes (path) only and reports EEXIST via errno.
inline int exprs_mkdir( const char *path, int )
{
    return _mkdir( path );
}
#if !defined( mkdir )
#define mkdir( path, mode ) exprs_mkdir( path, mode )
#endif

// ---- dirent ----------------------------------------------------------------
struct dirent
{
    char d_name[MAX_PATH];
};

struct DIR
{
    HANDLE handle = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAA data{};
    dirent current{};
    bool first = true;
};

inline DIR *opendir( const char *name )
{
    if ( !name || !*name )
    {
        errno = EINVAL;
        return nullptr;
    }
    std::string pattern( name );
    const char last = pattern.back();
    if ( last != '/' && last != '\\' )
        pattern += '/';
    pattern += '*';

    DIR *dir = new ( std::nothrow ) DIR();
    if ( !dir )
    {
        errno = ENOMEM;
        return nullptr;
    }
    dir->handle = FindFirstFileA( pattern.c_str(), &dir->data );
    if ( dir->handle == INVALID_HANDLE_VALUE )
    {
        delete dir;
        errno = ENOENT;
        return nullptr;
    }
    return dir;
}

inline dirent *readdir( DIR *dir )
{
    if ( !dir || dir->handle == INVALID_HANDLE_VALUE )
        return nullptr;
    if ( dir->first )
        dir->first = false;
    else if ( !FindNextFileA( dir->handle, &dir->data ) )
        return nullptr;
    std::strncpy( dir->current.d_name, dir->data.cFileName, MAX_PATH - 1 );
    dir->current.d_name[MAX_PATH - 1] = '\0';
    return &dir->current;
}

inline int closedir( DIR *dir )
{
    if ( !dir )
        return -1;
    if ( dir->handle != INVALID_HANDLE_VALUE )
        FindClose( dir->handle );
    delete dir;
    return 0;
}

// ---- dlfcn -----------------------------------------------------------------
inline constexpr int RTLD_LAZY = 1;
inline constexpr int RTLD_NOW = 2;
inline constexpr int RTLD_LOCAL = 0;
inline constexpr int RTLD_GLOBAL = 256;

struct ExprsDlState
{
    char buffer[512] = {};
    bool pending = false;
};

inline ExprsDlState &exprsDlState()
{
    static ExprsDlState state;
    return state;
}

inline void exprsDlSetError( const char *message )
{
    ExprsDlState &state = exprsDlState();
    std::strncpy( state.buffer, message ? message : "unknown error",
                  sizeof( state.buffer ) - 1 );
    state.pending = true;
}

inline char *dlerror()
{
    ExprsDlState &state = exprsDlState();
    if ( !state.pending )
        return nullptr;
    state.pending = false;
    return state.buffer;
}

inline void *dlopen( const char *file, int )
{
    HMODULE handle = LoadLibraryA( file );
    if ( !handle )
    {
        LPSTR messageBuffer = nullptr;
        const DWORD error = GetLastError();
        const DWORD size = FormatMessageA(
          FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
          nullptr, error, 0, reinterpret_cast<LPSTR>( &messageBuffer ), 0, nullptr );
        std::string message = messageBuffer
                                ? std::string( messageBuffer, size )
                                : ( "LoadLibrary failed with error " + std::to_string( error ) );
        if ( messageBuffer )
            LocalFree( messageBuffer );
        exprsDlSetError( message.c_str() );
    }
    return static_cast<void *>( handle );
}

inline void *dlsym( void *handle, const char *name )
{
    if ( !handle )
    {
        exprsDlSetError( "invalid module handle" );
        return nullptr;
    }
    FARPROC symbol = GetProcAddress( static_cast<HMODULE>( handle ), name );
    if ( !symbol )
        exprsDlSetError( "symbol not found" );
    return reinterpret_cast<void *>( symbol );
}

inline int dlclose( void *handle )
{
    if ( !handle )
        return -1;
    return FreeLibrary( static_cast<HMODULE>( handle ) ) ? 0 : -1;
}

#endif // _WIN32
