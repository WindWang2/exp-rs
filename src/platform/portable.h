/***************************************************************************
  platform/portable.h
  Cross-platform Completion — shared portability primitives.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Single home for the per-platform shims this repo previously hand-copied
  per file (process id, UTF-8 path/environment conversion). Header-only so
  every module can include it without a new link dependency; the Windows
  branches mirror the proven patterns in geospatial/util/atomic_fs.cpp
  (wideFromUtf8) and runtime/chunk/fsync_compat.h (CreateFileW + CP_UTF8).

  Repo-wide path convention: `std::string` path values hold UTF-8 bytes.
  These helpers are the sanctioned boundary between that convention and
  std::filesystem (whose narrow conversions are ACP-encoded on Windows)
  for NEW and migrated code. Pre-existing local variants with slightly
  different invalid-input semantics stay put on purpose: sdk/exprs
  path_policy's pathFromUtf8 rejects invalid UTF-8 (typed rejection, not
  an empty path), env_doctor's u8PathString renders via
  generic_u8string with a fallback, and mirror/external_process keep
  private wide converters for their Win32 branches. Converge those only
  with a dedicated semantic review — do not silently swap them.
 ***************************************************************************/

#ifndef SICNU_PLATFORM_PORTABLE_H
#define SICNU_PLATFORM_PORTABLE_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#if !defined( _WIN32 )
#include <fcntl.h>
#include <unistd.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sicnu::portable
{

/// Current process id, in a fixed-width type so log lines and staging-name
/// suffixes format identically on every platform. Replaces the per-file
/// `#ifdef _WIN32 GetCurrentProcessId/_getpid #else getpid` shims.
inline std::uint32_t pid()
{
#if !defined( _WIN32 )
  return static_cast<std::uint32_t>( ::getpid() );
#else
  return static_cast<std::uint32_t>( ::GetCurrentProcessId() );
#endif
}

#if defined( _WIN32 )
/// UTF-8 bytes → UTF-16. Empty stays empty.
inline std::wstring wideFromUtf8( const std::string &utf8 )
{
  if ( utf8.empty() )
    return std::wstring();
  const int len = ::MultiByteToWideChar( CP_UTF8, 0, utf8.data(),
                                         static_cast<int>( utf8.size() ), nullptr, 0 );
  if ( len <= 0 )
    return std::wstring();
  std::wstring wide( static_cast<std::size_t>( len ), L'\0' );
  ::MultiByteToWideChar( CP_UTF8, 0, utf8.data(), static_cast<int>( utf8.size() ),
                         wide.data(), len );
  return wide;
}

/// UTF-16 → UTF-8 bytes. Empty stays empty.
inline std::string utf8FromWide( const std::wstring &wide )
{
  if ( wide.empty() )
    return std::string();
  const int len = ::WideCharToMultiByte( CP_UTF8, 0, wide.data(),
                                         static_cast<int>( wide.size() ), nullptr, 0,
                                         nullptr, nullptr );
  if ( len <= 0 )
    return std::string();
  std::string utf8( static_cast<std::size_t>( len ), '\0' );
  ::WideCharToMultiByte( CP_UTF8, 0, wide.data(), static_cast<int>( wide.size() ),
                         utf8.data(), len, nullptr, nullptr );
  return utf8;
}
#endif

/// std::filesystem::path from UTF-8 bytes. Windows decodes through UTF-16 so
/// non-ASCII paths survive regardless of the process ANSI code page; POSIX
/// path values are byte-transparent, so the bytes are taken verbatim.
inline std::filesystem::path pathFromUtf8( const std::string &utf8 )
{
#if !defined( _WIN32 )
  return std::filesystem::path( utf8 );
#else
  return std::filesystem::path( wideFromUtf8( utf8 ) );
#endif
}

/// std::filesystem::path → UTF-8 bytes. Exact inverse of pathFromUtf8.
/// This is what "render a fs::path into a std::string field" must use —
/// never path::string(), which is ACP-encoded on Windows.
inline std::string pathToUtf8( const std::filesystem::path &path )
{
#if !defined( _WIN32 )
  return path.native();
#else
  return utf8FromWide( path.native() );
#endif
}

/// Environment lookup returning UTF-8 bytes; a missing variable yields an
/// empty string (callers keep their own missing-value semantics). Windows
/// reads through GetEnvironmentVariableW — never the ACP getenv — so a
/// non-ASCII install path in an env var survives the read.
inline std::string envUtf8( const char *name )
{
#if !defined( _WIN32 )
  if ( const char *value = std::getenv( name ) )
    return std::string( value );
  return std::string();
#else
  const std::wstring wideName = wideFromUtf8( name );
  const DWORD needed = ::GetEnvironmentVariableW( wideName.c_str(), nullptr, 0 );
  if ( needed == 0 )
    return std::string();
  std::wstring value( static_cast<std::size_t>( needed ), L'\0' );
  const DWORD got = ::GetEnvironmentVariableW( wideName.c_str(), value.data(), needed );
  if ( got == 0 || got > needed )
    return std::string();
  value.resize( got );
  return utf8FromWide( value );
#endif
}

/// True when `name` is a Windows reserved device name ("CON", "PRN", "AUX",
/// "NUL", "COM1".."COM9", "LPT1".."LPT9") — case-insensitive, checked on the
/// base name before the first dot, per Microsoft's naming rules. Such names
/// cannot exist as regular files on Windows drives, while they are legal
/// POSIX names: validation seams must call this only on Windows-bound
/// writes, never to reject inputs on POSIX.
inline bool isWindowsReservedName( const std::string &name )
{
  std::string base = name;
  const std::size_t dot = base.find( '.' );
  if ( dot != std::string::npos )
    base.resize( dot );
  if ( base.empty() )
    return false;
  for ( char &c : base )
  {
    const unsigned char uc = static_cast<unsigned char>( c );
    if ( uc >= 'a' && uc <= 'z' )
      c = static_cast<char>( uc - 'a' + 'A' );
  }
  static const char *kReserved[] = { "CON", "PRN", "AUX", "NUL", "COM1", "COM2",
                                     "COM3", "COM4", "COM5", "COM6", "COM7", "COM8",
                                     "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5",
                                     "LPT6", "LPT7", "LPT8", "LPT9" };
  for ( const char *reserved : kReserved )
    if ( base == reserved )
      return true;
  return false;
}

/// Claims @p path exclusively as a fresh staging file: POSIX open(2) with
/// O_EXCL, Windows CreateFileW with CREATE_NEW. True when this caller now
/// owns the name (the file exists empty; the caller writes and publishes it
/// or removes it). This is the check-then-use antidote for staging names on
/// directories other writers may touch: two publishers can never hold the
/// same temp, whatever their pids/counters happen to be.
inline bool claimExclusiveUtf8( const std::string &path )
{
#if !defined( _WIN32 )
  const int fd = ::open( path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600 );
  if ( fd < 0 )
    return false;
  ::close( fd );
  return true;
#else
  const HANDLE handle = ::CreateFileW( wideFromUtf8( path ).c_str(), GENERIC_WRITE,
                                       FILE_SHARE_READ, nullptr, CREATE_NEW,
                                       FILE_ATTRIBUTE_NORMAL, nullptr );
  if ( handle == INVALID_HANDLE_VALUE )
    return false;
  ::CloseHandle( handle );
  return true;
#endif
}

/// fopen() over a UTF-8 @p path (POSIX byte-transparent; Windows widens to
/// UTF-16 so a non-ASCII cache/data directory survives the C file API — the
/// narrow fopen decodes with the process ANSI code page and misses). @p mode
/// is the usual ASCII fopen mode text. Returns nullptr like fopen on failure.
inline std::FILE *fileOpenUtf8( const std::string &utf8, const char *mode )
{
#if !defined( _WIN32 )
  return std::fopen( utf8.c_str(), mode );
#else
  const std::wstring wideMode( mode, mode + std::strlen( mode ) );
  return ::_wfopen( wideFromUtf8( utf8 ).c_str(), wideMode.c_str() );
#endif
}

/// Durability gate for a publish sequence: flush @p path's written bytes to
/// the storage device (fsync(2), resp. FlushFileBuffers). @p path is UTF-8.
/// Returns false when the file cannot be opened or the flush fails — a
/// publisher must then refuse to rename the file into place, or a crash can
/// commit the directory entry for bytes that never reached the disk.
inline bool syncFileUtf8( const std::string &path )
{
#if !defined( _WIN32 )
  // O_WRONLY so fsync(2) is legal on every POSIX flavor (XSI is strictest
  // about O_RDONLY fds). Windows opens GENERIC_WRITE, which fails on a
  // read-only file — publishers call this on files they just wrote.
  const int fd = ::open( path.c_str(), O_WRONLY );
  if ( fd < 0 )
    return false;
  const bool flushed = ::fsync( fd ) == 0;
  ::close( fd );
  return flushed;
#else
  const HANDLE handle = ::CreateFileW( wideFromUtf8( path ).c_str(), GENERIC_WRITE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
  if ( handle == INVALID_HANDLE_VALUE )
    return false;
  const bool flushed = ::FlushFileBuffers( handle ) != 0;
  ::CloseHandle( handle );
  return flushed;
#endif
}

/// Best-effort durability for a RENAME's directory entry: flushes the
/// directory containing @p path (UTF-8) so a crash after publish cannot
/// revert the directory entry. Silent by design — several legitimate
/// filesystems (network/FUSE mounts) refuse directory fsync with EINVAL, and
/// failing the publish there would trade a real capability for a durability
/// nicety. Windows has no directory-flush API; MOVEFILE_WRITE_THROUGH-style
/// semantics are the publisher's responsibility there, so this is a no-op.
inline void syncDirectoryBestEffortUtf8( const std::string &path )
{
#if !defined( _WIN32 )
  const std::filesystem::path target = pathFromUtf8( path );
  const std::filesystem::path directory =
    target.parent_path().empty() ? std::filesystem::path( "." ) : target.parent_path();
  const int fd = ::open( directory.c_str(), O_RDONLY );
  if ( fd < 0 )
    return;
  ::fsync( fd ); // ignored: EINVAL on filesystems without directory fsync
  ::close( fd );
#endif
}

} // namespace sicnu::portable

#endif // SICNU_PLATFORM_PORTABLE_H
