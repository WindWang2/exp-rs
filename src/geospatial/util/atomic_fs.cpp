/***************************************************************************
  geospatial/util/atomic_fs.cpp
  Geospatial I/O Foundation 4.0 — atomic filesystem publication primitives.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/util/atomic_fs.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace sicnu::geo::atomic_fs
{
namespace
{

std::atomic<unsigned> &stagingCounter()
{
  static std::atomic<unsigned> counter{ 0 };
  return counter;
}

#ifdef _WIN32
std::wstring wideFromUtf8( const std::string &text )
{
  if ( text.empty() )
    return std::wstring();
  const int size = MultiByteToWideChar( CP_UTF8, 0, text.c_str(), static_cast<int>( text.size() ), nullptr, 0 );
  std::wstring wide( static_cast<std::size_t>( size ), L'\0' );
  MultiByteToWideChar( CP_UTF8, 0, text.c_str(), static_cast<int>( text.size() ), wide.data(), size );
  return wide;
}

void throwLastWindowsError( const std::string &context, const std::string &path )
{
  Json::Value details;
  details["path"] = path;
  details["win32_error"] = static_cast<Json::UInt64>( GetLastError() );
  throw GeoError( ErrorCode::IoError, context, details );
}
#endif

} // namespace

bool fileExists( const std::string &path )
{
  std::error_code ec;
  const fs::file_status status = fs::status( fs::u8path( path ), ec );
  return !ec && fs::exists( status ) && !fs::is_directory( status );
}

std::uintmax_t fileSize( const std::string &path )
{
  std::error_code ec;
  const auto size = fs::file_size( fs::u8path( path ), ec );
  return ec ? 0 : size;
}

std::string stagedPathFor( const std::string &targetPath )
{
  // All path handling stays UTF-8: never route through fs::path::string(),
  // which re-encodes into the active code page and throws on Unicode names.
  const auto u8 = [] ( const fs::path &p ) -> std::string {
    const std::u8string text = p.u8string();
    return std::string( text.begin(), text.end() );
  };
  const fs::path target = fs::u8path( targetPath );
  const fs::path directory = target.parent_path().empty() ? fs::path( "." ) : target.parent_path();
  const std::string filename = u8( target.filename() );
  // Keep the final extension in place: extension-driven drivers (ESRI
  // Shapefile, ENVI, ...) must still recognize the staged dataset.
  const std::size_t dot = filename.rfind( '.' );
  const std::string stem = dot == std::string::npos ? filename : filename.substr( 0, dot );
  const std::string extension = dot == std::string::npos ? "" : filename.substr( dot );
  static const int kMaxAttempts = 64;
  for ( int attempt = 0; attempt < kMaxAttempts; ++attempt )
  {
    const std::string staged = u8( directory ) + "/" + stem + "." + std::to_string( stagingCounter()++ )
                                 + "." + std::to_string( ::rand() ) + ".tmp" + extension;
    if ( !fileExists( staged ) )
      return staged;
  }
  throw GeoError( ErrorCode::IoError, "Cannot allocate a staging path next to " + targetPath );
}

void fsyncFile( const std::string &path )
{
#ifdef _WIN32
  const HANDLE handle = CreateFileW( wideFromUtf8( path ).c_str(), GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
  if ( handle == INVALID_HANDLE_VALUE )
    throwLastWindowsError( "fsync: cannot open " + path, path );
  if ( !FlushFileBuffers( handle ) )
  {
    const Json::UInt64 code = static_cast<Json::UInt64>( GetLastError() );
    CloseHandle( handle );
    Json::Value details;
    details["path"] = path;
    details["win32_error"] = code;
    throw GeoError( ErrorCode::IoError, "fsync: FlushFileBuffers failed for " + path, details );
  }
  CloseHandle( handle );
#else
  const int fd = ::open( path.c_str(), O_WRONLY );
  if ( fd < 0 )
    throw GeoError( ErrorCode::IoError, "fsync: cannot open " + path );
  if ( ::fsync( fd ) != 0 )
  {
    ::close( fd );
    throw GeoError( ErrorCode::IoError, "fsync failed for " + path );
  }
  ::close( fd );
#endif
}

/// Copies src over dst (creating/overwriting), throwing GeoError on failure.
/// Used by the cross-device publish fallback (#807); callers fsync + rename
/// the copy into place so the target update itself stays atomic.
void copyFileOverwriting( const std::string &srcPath, const std::string &dstPath )
{
  std::error_code ec;
  fs::copy_file( fs::u8path( srcPath ), fs::u8path( dstPath ),
                 fs::copy_options::overwrite_existing, ec );
  if ( ec )
    throw GeoError( ErrorCode::IoError, "publish: copy failed for " + srcPath + " → " + dstPath,
                    Json::Value( ec.message() ) );
}

void publishStagedFile( const std::string &stagedPath, const std::string &targetPath )
{
  if ( !fileExists( stagedPath ) )
    throw GeoError( ErrorCode::IoError, "publish: staged file missing: " + stagedPath );
#ifdef _WIN32
  const std::wstring staged = wideFromUtf8( stagedPath );
  const std::wstring target = wideFromUtf8( targetPath );
  if ( fileExists( targetPath ) )
  {
    // ReplaceFileW swaps in one call when the target exists (the target's
    // previous content is preserved as backup until the swap completes).
    if ( ReplaceFileW( target.c_str(), staged.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr ) )
      return;
    // Fall through to move-with-replace for cross-system cases ReplaceFile
    // rejects (e.g. target on a different file attribute store).
  }
  if ( !MoveFileExW( staged.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH ) )
    throwLastWindowsError( "publish: rename failed for " + targetPath, targetPath );
#else
  if ( ::rename( stagedPath.c_str(), targetPath.c_str() ) != 0 )
  {
    // #807: rename(2) fails with EXDEV when staged and target live on
    // different filesystems. Fall back to copy-into-the-target-directory +
    // fsync + same-directory atomic rename, so the publish stays atomic on
    // the target device and the staged file is consumed either way.
    if ( errno != EXDEV )
    {
      Json::Value details;
      details["path"] = targetPath;
      details["errno"] = errno;
      throw GeoError( ErrorCode::IoError, "publish: rename failed for " + targetPath, details );
    }
    const std::string fallback = targetPath + ".publish-cross-device";
    try
    {
      copyFileOverwriting( stagedPath, fallback );
      fsyncFile( fallback );
      if ( ::rename( fallback.c_str(), targetPath.c_str() ) != 0 )
      {
        const int renameErrno = errno;
        removeFileQuiet( fallback );
        Json::Value details;
        details["path"] = targetPath;
        details["errno"] = renameErrno;
        throw GeoError( ErrorCode::IoError,
                        "publish: cross-device fallback rename failed for " + targetPath, details );
      }
      removeFileQuiet( stagedPath );
    }
    catch ( const GeoError & )
    {
      removeFileQuiet( fallback );
      throw;
    }
  }
#endif
}

/// Renames a file within the same directory (backup moves). Quiet: false on
/// any failure.
bool moveFileQuiet( const std::string &from, const std::string &to )
{
  std::error_code ec;
  fs::rename( fs::u8path( from ), fs::u8path( to ), ec );
  return !ec;
}

bool removeFileQuiet( const std::string &path )
{
  if ( !fileExists( path ) )
    return true;
  std::error_code ec;
  fs::remove( fs::u8path( path ), ec );
  return !ec;
}

std::vector<std::string> sidecarsFor( const std::string &mainPath )
{
  // Shapefile-family sidecars share the .shp stem (X.shp → X.shx/X.dbf/...);
  // everything else (world files, PAM, ESRI XML) appends to the full name.
  static const char *kReplacedSuffixes[] = { ".shx", ".dbf", ".prj", ".qpj", ".cpg", ".sbn", ".sbx", ".qix" };
  static const char *kAppendedSuffixes[] = { ".shp.xml", ".tfw", ".aux", ".aux.xml", ".jpw", ".jgw" };

  std::string stem = mainPath;
  static const char *kMainExtensions[] = { ".shp", ".gpkg", ".tif", ".nc" };
  for ( const char *mainExtension : kMainExtensions )
  {
    if ( mainPath.size() > strlen( mainExtension )
         && mainPath.compare( mainPath.size() - strlen( mainExtension ), strlen( mainExtension ), mainExtension ) == 0 )
    {
      stem = mainPath.substr( 0, mainPath.size() - strlen( mainExtension ) );
      break;
    }
  }

  std::vector<std::string> sidecars;
  for ( const char *suffix : kReplacedSuffixes )
    sidecars.push_back( stem + suffix );
  for ( const char *suffix : kAppendedSuffixes )
    sidecars.push_back( mainPath + suffix );
  return sidecars;
}

void discardStaged( const std::string &stagedMainPath )
{
  removeFileQuiet( stagedMainPath );
  for ( const std::string &sidecar : sidecarsFor( stagedMainPath ) )
    removeFileQuiet( sidecar );
}

void publishStagedGroup( const std::string &stagedMainPath, const std::string &targetMainPath )
{
  if ( !fileExists( stagedMainPath ) )
    throw GeoError( ErrorCode::IoError, "group publish: staged main file missing: " + stagedMainPath );

  // Snapshot existing targets so a mid-publish failure can RESTORE the
  // previous good group: each existing member moves aside to a backup name
  // first; cleanup restores backups for replaced members and removes only
  // newly-created ones (a stale sidecar is recoverable; a deleted
  // pre-existing sidecar referenced by the old main file is not).
  // #791: the MAIN target is part of that backup set too — it used to be
  // replaced with no backup, so a failure at or after the main swap (e.g.
  // inside the #807 cross-device fallback) lost the previous good main file.
  const std::vector<std::string> stagedSidecars = sidecarsFor( stagedMainPath );
  const std::vector<std::string> targetSidecars = sidecarsFor( targetMainPath );
  std::vector<bool> hadTarget( targetSidecars.size(), false );
  const bool hadMainTarget = fileExists( targetMainPath );
  const std::string mainBackup = targetMainPath + ".bak";
  std::vector<std::string> published;
  auto cleanup = [ & ]( const std::string &failedName ) {
    for ( const std::string &done : published )
      removeFileQuiet( done );
    // Restore every backed-up member (the previous good group)...
    if ( hadMainTarget && fileExists( mainBackup ) )
    {
      removeFileQuiet( targetMainPath );
      // #791 review: a failed restore must NOT drop the backup — that
      // would destroy the last copy of the previous good main file.
      if ( !moveFileQuiet( mainBackup, targetMainPath ) )
        throw GeoError( ErrorCode::IoError,
                        "group publish failed at " + failedName +
                            "; the previous main file could not be restored from " + mainBackup );
    }
    for ( std::size_t i = 0; i < targetSidecars.size(); ++i )
    {
      const std::string backup = targetSidecars[i] + ".bak";
      if ( hadTarget[i] && fileExists( backup ) )
      {
        removeFileQuiet( targetSidecars[i] );
        moveFileQuiet( backup, targetSidecars[i] );
      }
    }
    if ( hadMainTarget && fileExists( mainBackup ) )
    {
      removeFileQuiet( targetMainPath );
      moveFileQuiet( mainBackup, targetMainPath );
    }
    // ...then drop any leftover backup copies.
    removeFileQuiet( mainBackup );
    for ( std::size_t i = 0; i < targetSidecars.size(); ++i )
      removeFileQuiet( targetSidecars[i] + ".bak" );
    Json::Value details;
    details["failed_member"] = failedName;
    throw GeoError( ErrorCode::IoError, "group publish failed at " + failedName + "; target group rolled back", details );
  };

  // Sidecars first, main file last: an interrupted publish can leave a stale
  // sidecar but never a main file referencing missing members.
  for ( std::size_t i = 0; i < stagedSidecars.size(); ++i )
  {
    if ( !fileExists( stagedSidecars[i] ) )
      continue;
    hadTarget[i] = fileExists( targetSidecars[i] );
    if ( hadTarget[i] )
    {
      const std::string backup = targetSidecars[i] + ".bak";
      removeFileQuiet( backup );
      if ( !moveFileQuiet( targetSidecars[i], backup ) )
        cleanup( targetSidecars[i] ); // cannot protect the old member: refuse
    }
    try
    {
      publishStagedFile( stagedSidecars[i], targetSidecars[i] );
    }
    catch ( const GeoError & )
    {
      cleanup( targetSidecars[i] );
    }
    published.push_back( targetSidecars[i] );
  }
  // Main file last, with the same backup discipline as the sidecars (#791).
  if ( hadMainTarget )
  {
    removeFileQuiet( mainBackup );
    if ( !moveFileQuiet( targetMainPath, mainBackup ) )
      cleanup( targetMainPath ); // cannot protect the old main file: refuse
  }
  try
  {
    publishStagedFile( stagedMainPath, targetMainPath );
  }
  catch ( const GeoError & )
  {
    cleanup( targetMainPath );
  }
  // Success: drop the backup set.
  removeFileQuiet( mainBackup );
  for ( std::size_t i = 0; i < targetSidecars.size(); ++i )
    removeFileQuiet( targetSidecars[i] + ".bak" );
}

void writeFileAtomic( const std::string &targetPath, const std::function<void( const std::string &stagedPath )> &writer )
{
  const std::string staged = stagedPathFor( targetPath );
  try
  {
    writer( staged );
    if ( fileExists( staged ) )
    {
      fsyncFile( staged );
      publishStagedFile( staged, targetPath );
    }
  }
  catch ( ... )
  {
    discardStaged( staged );
    throw;
  }
}

} // namespace sicnu::geo::atomic_fs
