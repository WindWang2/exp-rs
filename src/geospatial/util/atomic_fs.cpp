/***************************************************************************
  geospatial/util/atomic_fs.cpp
  Geospatial I/O Foundation 4.0 — atomic filesystem publication primitives.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/util/atomic_fs.h"

#include "platform/portable.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <set>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
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

/// The current process id, portable across the Windows/POSIX branches: the
/// staging counter is per-process, so cross-process uniqueness needs the pid
/// (two processes otherwise generate identical staged names).
std::uint64_t stagingProcessId()
{
  return static_cast<std::uint64_t>( sicnu::portable::pid() );
}

#ifdef _WIN32
// The UTF-8→wide conversion lives in platform/portable.h (the single
// authority for this repo's per-platform shims).
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
  const fs::file_status status = fs::status( sicnu::portable::pathFromUtf8( path ), ec );
  return !ec && fs::exists( status ) && !fs::is_directory( status );
}

std::uintmax_t fileSize( const std::string &path )
{
  std::error_code ec;
  const auto size = fs::file_size( sicnu::portable::pathFromUtf8( path ), ec );
  return ec ? 0 : size;
}

std::string reservedStagedPathFor( const std::string &targetPath )
{
  // All path handling stays UTF-8: never route through fs::path::string(),
  // which re-encodes into the active code page and throws on Unicode names.
  const auto u8 = [] ( const fs::path &p ) -> std::string {
    const std::u8string text = p.u8string();
    return std::string( text.begin(), text.end() );
  };
  const fs::path target = sicnu::portable::pathFromUtf8( targetPath );
  const std::string filename = u8( target.filename() );
  // Keep the final extension in place: extension-driven drivers (ESRI
  // Shapefile, ENVI, ...) must still recognize the staged dataset.
  const std::size_t dot = filename.rfind( '.' );
  const std::string stem = dot == std::string::npos ? filename : filename.substr( 0, dot );
  const std::string extension = dot == std::string::npos ? "" : filename.substr( dot );
  // #1097: pid + counter alone still collide across forks that share a
  // counter reset. random_device supplies entropy that ::rand() (never
  // seeded in src/) cannot.
  static thread_local std::mt19937_64 rng{ [] {
    std::random_device rd;
    return std::mt19937_64{ rd() ^ ( stagingProcessId() << 1 ) };
  }() };
  const std::string leaf = stem + "." + std::to_string( stagingProcessId() ) + "." +
                           std::to_string( stagingCounter()++ ) + "." +
                           std::to_string( rng() ) + ".tmp" + extension;
  // Join through fs::path (never a literal "/"): GDAL's Win32 VSI layer
  // opens files through \\?\-prefixed paths, where forward slashes are NOT
  // normalized away — a mixed-separator staged name made VRT/COG (and any
  // driver taking the strict long-path open) fail with "Failed to open … to
  // write". u8string() renders the platform-preferred separators as UTF-8.
  //
  // The staging directory resolves PHYSICALLY (weakly_canonical, lexical
  // fallback): ".." after a symlinked component pops the LINK TARGET's
  // parent for the kernel while lexically_normal drops the link textually,
  // so the lexical form can allocate the staged name in a different
  // directory than the one the publish rename's destination resolves into.
  // Canonicalizing first makes the lexical ".." pop on the canonical tail
  // physical-safe; an unresolvable path keeps the previous lexical behavior.
  std::error_code resolutionEc;
  fs::path directory = fs::weakly_canonical(
    target.parent_path().empty() ? fs::path( "." ) : target.parent_path(), resolutionEc );
  if ( resolutionEc )
    directory = target.parent_path().empty() ? fs::path( "." ) : target.parent_path();
  directory = directory.lexically_normal();
  const fs::path staged = directory / sicnu::portable::pathFromUtf8( leaf );
  return u8( staged );
}

std::string stagedPathFor( const std::string &targetPath )
{
  // The exclusive-creation staging claim is the whole reservation contract: the
  // name exists (as an empty file) from allocation until the writer fills
  // it, so no other allocator can hand out the same path. The claim syscall
  // itself lives in the portable authority (single home, source-pinned);
  // this layer adds the retry-on-lost-race and the typed GeoError. Retry
  // ONLY on a lost claim race; any other refusal (missing directory,
  // permissions) fails immediately with the raw OS cause in the details.
  static const int kMaxAttempts = 64;
  for ( int attempt = 0; attempt < kMaxAttempts; ++attempt )
  {
    const std::string staged = reservedStagedPathFor( targetPath );
    sicnu::portable::ClaimFailure failure = sicnu::portable::ClaimFailure::NameExists;
    std::uint64_t osError = 0;
    if ( sicnu::portable::claimExclusiveUtf8( staged, &failure, &osError ) )
      return staged;
    if ( failure != sicnu::portable::ClaimFailure::NameExists )
    {
      Json::Value details;
      details["path"] = staged;
#ifdef _WIN32
      details["win32_error"] = static_cast<Json::UInt64>( osError );
#else
      details["errno"] = static_cast<Json::Int64>( osError );
#endif
      throw GeoError( ErrorCode::IoError, "Cannot allocate a staging path next to " + targetPath,
                      details );
    }
  }
  throw GeoError( ErrorCode::IoError, "Cannot allocate a staging path next to " + targetPath );
}

void fsyncFile( const std::string &path )
{
  // The flush syscall lives in the portable authority (single home,
  // source-pinned); this layer turns the two failure classes back into the
  // typed GeoError with
  // the raw OS cause preserved (errno on POSIX, the Win32 error on Windows
  // — both lanes used to keep only one of the two).
  sicnu::portable::SyncFailure failure = sicnu::portable::SyncFailure::OpenFailed;
  std::uint64_t osError = 0;
  if ( sicnu::portable::syncFileUtf8( path, &failure, &osError ) )
    return;
  Json::Value details;
  details["path"] = path;
#ifdef _WIN32
  details["win32_error"] = static_cast<Json::UInt64>( osError );
#else
  details["errno"] = static_cast<Json::Int64>( osError );
#endif
  if ( failure == sicnu::portable::SyncFailure::OpenFailed )
    throw GeoError( ErrorCode::IoError, "fsync: cannot open " + path, details );
  throw GeoError( ErrorCode::IoError, "fsync failed for " + path, details );
}

/// Copies src over dst (creating/overwriting), throwing GeoError on failure.
/// Used by the cross-device publish fallback (#807); callers fsync + rename
/// the copy into place so the target update itself stays atomic.
void copyFileOverwriting( const std::string &srcPath, const std::string &dstPath )
{
  std::error_code ec;
  fs::copy_file( sicnu::portable::pathFromUtf8( srcPath ), sicnu::portable::pathFromUtf8( dstPath ),
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
  const std::wstring staged = sicnu::portable::wideFromUtf8( stagedPath );
  const std::wstring target = sicnu::portable::wideFromUtf8( targetPath );
  if ( fileExists( targetPath ) )
  {
    // POSIX rename(2) replaces a read-only target — the gate is the
    // DIRECTORY's write permission, never the target's R/O bit. Windows'
    // ReplaceFileW/MoveFileExW refuse a READONLY target with
    // ERROR_ACCESS_DENIED, so a stale attribute (a former RO output, a
    // restored backup) would fail the publish where POSIX publishes. Clear
    // it first so both platforms answer with the same contract.
    const DWORD attributes = ::GetFileAttributesW( target.c_str() );
    if ( attributes != INVALID_FILE_ATTRIBUTES && ( attributes & FILE_ATTRIBUTE_READONLY ) )
      ::SetFileAttributesW( target.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY );
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
  // Durability of the RENAME itself (POSIX lane): fsync the containing
  // directory so a crash after publish cannot revert the directory entry.
  // Best-effort and silent: several legitimate filesystems (some network/
  // FUSE mounts) refuse directory fsync with EINVAL — failing the publish
  // there would trade a real capability for a durability nicety. The
  // file-content fsync above is the correctness gate; this only narrows the
  // crash window for the entry. The portable authority's directory flush is
  // the single implementation. (The Windows lane of this function returned
  // above: its durability rides MOVEFILE_WRITE_THROUGH inside the
  // MoveFileExW call.)
  sicnu::portable::syncDirectoryBestEffortUtf8( targetPath );
#endif
}

bool renameReplaceQuiet( const std::string &from, const std::string &to )
{
  if ( !fileExists( from ) )
    return false;
#ifdef _WIN32
  const std::wstring wideFrom = sicnu::portable::wideFromUtf8( from );
  const std::wstring wideTo = sicnu::portable::wideFromUtf8( to );
  return MoveFileExW( wideFrom.c_str(), wideTo.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED ) != 0;
#else
  return ::rename( from.c_str(), to.c_str() ) == 0;
#endif
}

/// Backs up @p targetPath under @p backupPath WITHOUT moving the target
/// aside (#1391): a hardlink gives the old bytes a second name while the
/// target keeps its directory entry, so the publishStagedFile rename that
/// follows swaps it atomically — a crash at ANY point leaves the target
/// present (old bytes before the rename lands, new bytes after). The
/// move-aside discipline this replaces parked the previous good file at
/// .bak first, leaving a window where the target was ABSENT and nothing
/// healed it (the C++ rollback never runs on a crash; sweepOrphans does
/// not sweep .bak). Falls back to move-aside on filesystems without
/// hardlink support (FAT, some network/FUSE mounts): the old, narrower
/// guarantee — refused silence.
bool backupInPlace( const std::string &targetPath, const std::string &backupPath )
{
  removeFileQuiet( backupPath );
#ifdef _WIN32
  if ( ::CreateHardLinkW( sicnu::portable::wideFromUtf8( backupPath ).c_str(),
                          sicnu::portable::wideFromUtf8( targetPath ).c_str(), nullptr ) )
    return true;
#else
  if ( ::link( targetPath.c_str(), backupPath.c_str() ) == 0 )
    return true;
#endif
  return renameReplaceQuiet( targetPath, backupPath );
}

bool removeFileQuiet( const std::string &path )
{
  if ( !fileExists( path ) )
    return true;
  std::error_code ec;
  fs::remove( sicnu::portable::pathFromUtf8( path ), ec );
  return !ec;
}

std::vector<std::string> sidecarsFor( const std::string &mainPath )
{
  // Shapefile-family sidecars share the .shp stem (X.shp → X.shx/X.dbf/...);
  // everything else (world files, PAM, ESRI XML) appends to the full name.
  static const char *kReplacedSuffixes[] = { ".shx", ".dbf", ".prj", ".qpj", ".cpg", ".sbn", ".sbx", ".qix" };
  // ".sicnu-manifest.json" (io/finalize_manifest.h) joins the appended
  // family so the finalize manifest rides the group transaction:
  // published sidecar-first, cleaned by discardStaged, swept with the group.
  static const char *kAppendedSuffixes[] = { ".shp.xml", ".tfw", ".aux", ".aux.xml", ".jpw", ".jgw",
                                             ".sicnu-manifest.json" };

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

namespace
{

struct JournalMember
{
  std::string staged;
  std::string target;
  std::string backup;
  bool hadTarget = false;
};

void writeJournalFile( const std::string &journalPath, const std::string &mainTarget, const std::vector<JournalMember> &members )
{
  Json::Value root;
  root["version"] = 1;
  root["main"] = mainTarget;
  Json::Value arr( Json::arrayValue );
  for ( const auto &m : members )
  {
    Json::Value item;
    item["staged"] = m.staged;
    item["target"] = m.target;
    item["backup"] = m.backup;
    item["had_target"] = m.hadTarget;
    arr.append( item );
  }
  root["members"] = arr;

  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  const std::string text = Json::writeString( builder, root );

  std::ofstream out( sicnu::portable::pathFromUtf8( journalPath ), std::ios::binary | std::ios::trunc );
  if ( !out )
    throw GeoError( ErrorCode::IoError, "cannot write atomic fs journal: " + journalPath );
  out.write( text.data(), static_cast<std::streamsize>( text.size() ) );
  out.flush();
  out.close();
  // #1701: a journal that hit ENOSPC mid-write is a truncated JSON that
  // recoverFromJournalFile() parses as invalid and DELETES — silently
  // discarding the group's only rollback record. The write must be proven,
  // not assumed: a failed stream is an error while the targets are still
  // untouched, exactly like the open failure above.
  if ( !out.good() )
    throw GeoError( ErrorCode::IoError,
                    "atomic fs journal write did not complete (stream error): " + journalPath );

  fsyncFile( journalPath );
}

bool recoverFromJournalFile( const std::string &journalPath, bool isCommitted )
{
  try
  {
    std::ifstream file( sicnu::portable::pathFromUtf8( journalPath ), std::ios::binary );
    if ( !file )
      return false;
    std::string text( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
    file.close();

    Json::Value json;
    Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    std::string err;
    if ( !reader->parse( text.data(), text.data() + text.size(), &json, &err ) )
    {
      removeFileQuiet( journalPath );
      return false;
    }

    if ( !json.isObject() )
    {
      removeFileQuiet( journalPath );
      return false;
    }

    if ( !json.isMember( "members" ) || !json["members"].isArray() )
    {
      removeFileQuiet( journalPath );
      return false;
    }

    const Json::Value members = json["members"];
    if ( isCommitted )
    {
      for ( Json::ArrayIndex i = 0; i < members.size(); ++i )
      {
        if ( !members[i].isObject() )
          continue;
        const std::string backup = members[i].isMember( "backup" ) && members[i]["backup"].isString()
                                       ? members[i]["backup"].asString()
                                       : "";
        if ( !backup.empty() )
          removeFileQuiet( backup );
      }
    }
    else
    {
      // Remove newly created targets that had no prior target
      for ( Json::ArrayIndex i = 0; i < members.size(); ++i )
      {
        if ( !members[i].isObject() )
          continue;
        const std::string target = members[i].isMember( "target" ) && members[i]["target"].isString()
                                       ? members[i]["target"].asString()
                                       : "";
        const bool hadTarget = members[i].isMember( "had_target" ) && members[i]["had_target"].isBool()
                                   ? members[i]["had_target"].asBool()
                                   : false;
        if ( !hadTarget && !target.empty() )
          removeFileQuiet( target );
      }

      // Restore backed-up members: restore main target first (if present), then others
      int mainIdx = -1;
      const std::string mainTarget = json.isMember( "main" ) && json["main"].isString()
                                         ? json["main"].asString()
                                         : "";
      for ( Json::ArrayIndex i = 0; i < members.size(); ++i )
      {
        if ( !members[i].isObject() )
          continue;
        const std::string target = members[i].isMember( "target" ) && members[i]["target"].isString()
                                       ? members[i]["target"].asString()
                                       : "";
        if ( !mainTarget.empty() && target == mainTarget )
        {
          mainIdx = static_cast<int>( i );
          break;
        }
      }

      if ( mainIdx >= 0 )
      {
        const std::string target = members[mainIdx].isMember( "target" ) && members[mainIdx]["target"].isString()
                                       ? members[mainIdx]["target"].asString()
                                       : "";
        const std::string backup = members[mainIdx].isMember( "backup" ) && members[mainIdx]["backup"].isString()
                                       ? members[mainIdx]["backup"].asString()
                                       : "";
        const bool hadTarget = members[mainIdx].isMember( "had_target" ) && members[mainIdx]["had_target"].isBool()
                                   ? members[mainIdx]["had_target"].asBool()
                                   : false;
        bool restored = false;
        if ( hadTarget && fileExists( backup ) )
        {
          removeFileQuiet( target );
          restored = renameReplaceQuiet( backup, target );
        }
        if ( !hadTarget || restored )
          removeFileQuiet( backup );
      }

      for ( Json::ArrayIndex i = 0; i < members.size(); ++i )
      {
        if ( static_cast<int>( i ) == mainIdx )
          continue;
        if ( !members[i].isObject() )
          continue;
        const std::string target = members[i].isMember( "target" ) && members[i]["target"].isString()
                                       ? members[i]["target"].asString()
                                       : "";
        const std::string backup = members[i].isMember( "backup" ) && members[i]["backup"].isString()
                                       ? members[i]["backup"].asString()
                                       : "";
        const bool hadTarget = members[i].isMember( "had_target" ) && members[i]["had_target"].isBool()
                                   ? members[i]["had_target"].asBool()
                                   : false;
        bool restored = false;
        if ( hadTarget && fileExists( backup ) )
        {
          removeFileQuiet( target );
          restored = renameReplaceQuiet( backup, target );
        }
        if ( !hadTarget || restored )
          removeFileQuiet( backup );
      }
    }

    removeFileQuiet( journalPath );
    return true;
  }
  catch ( const std::exception & )
  {
    removeFileQuiet( journalPath );
    return false;
  }
}

} // namespace

bool hasPendingJournal( const std::string &targetMainPath )
{
  return fileExists( targetMainPath + ".publish-journal" ) ||
         fileExists( targetMainPath + ".publish-journal.committed" ) ||
         fileExists( targetMainPath + ".sicnu-journal" ) ||
         fileExists( targetMainPath + ".sicnu-journal.committed" );
}

bool hasUncommittedPublishJournal( const std::string &targetMainPath )
{
  return fileExists( targetMainPath + ".publish-journal" ) ||
         fileExists( targetMainPath + ".sicnu-journal" );
}

bool interruptedPublishPending( const std::string &localPath )
{
  if ( localPath.empty() || localPath.rfind( "/vsi", 0 ) == 0 || localPath.find( "://" ) != std::string::npos )
    return false;
  return hasUncommittedPublishJournal( localPath );
}

void throwIfInterruptedPublish( const std::string &localPath )
{
  if ( interruptedPublishPending( localPath ) )
    throwInterruptedPublish( localPath );
}

void throwInterruptedPublish( const std::string &localPath )
{
  Json::Value details;
  details["path"] = localPath;
  details["reason"] = "interrupted_group_publish";
  details["recovery"] = "atomic_fs::recoverDatasetGroup(path) restores the last complete group";
  throw GeoError( ErrorCode::IoError,
                  "dataset group was interrupted mid-publish (uncommitted journal present: a "
                  "publish is in progress or was killed); refusing to read a possibly mixed "
                  "generation: " + localPath,
                  details );
}

bool recoverDatasetGroup( const std::string &targetMainPath )
{
  const std::string journal1 = targetMainPath + ".publish-journal";
  const std::string committed1 = targetMainPath + ".publish-journal.committed";
  const std::string journal2 = targetMainPath + ".sicnu-journal";
  const std::string committed2 = targetMainPath + ".sicnu-journal.committed";

  bool recovered = false;
  if ( fileExists( committed1 ) )
    recovered = recoverFromJournalFile( committed1, true ) || recovered;
  if ( fileExists( committed2 ) )
    recovered = recoverFromJournalFile( committed2, true ) || recovered;
  if ( fileExists( journal1 ) )
    recovered = recoverFromJournalFile( journal1, false ) || recovered;
  if ( fileExists( journal2 ) )
    recovered = recoverFromJournalFile( journal2, false ) || recovered;

  return recovered;
}

std::size_t recoverDirectoryJournals( const std::string &directoryPath )
{
  std::size_t count = 0;
  std::error_code ec;
  const fs::path p = sicnu::portable::pathFromUtf8( directoryPath );
  if ( !fs::is_directory( p, ec ) )
    return 0;

  std::set<std::string> mainsToRecover;
  for ( const auto &entry : fs::directory_iterator( p, ec ) )
  {
    if ( ec )
      break;
    if ( !entry.is_regular_file( ec ) )
      continue;
    const std::string filename = sicnu::portable::pathToUtf8( entry.path().filename() );
    const std::string fullPath = sicnu::portable::pathToUtf8( entry.path() );

    auto checkSuffix = [&]( const std::string &suffix ) -> bool {
      if ( filename.size() > suffix.size() &&
           filename.compare( filename.size() - suffix.size(), suffix.size(), suffix ) == 0 )
      {
        const std::string mainPath = fullPath.substr( 0, fullPath.size() - suffix.size() );
        mainsToRecover.insert( mainPath );
        return true;
      }
      return false;
    };

    if ( checkSuffix( ".publish-journal.committed" ) ) continue;
    if ( checkSuffix( ".publish-journal" ) ) continue;
    if ( checkSuffix( ".sicnu-journal.committed" ) ) continue;
    if ( checkSuffix( ".sicnu-journal" ) ) continue;
  }

  for ( const auto &main : mainsToRecover )
  {
    if ( recoverDatasetGroup( main ) )
      ++count;
  }

  return count;
}


void publishStagedGroup( const std::string &stagedMainPath, const std::string &targetMainPath )
{
  if ( !fileExists( stagedMainPath ) )
    throw GeoError( ErrorCode::IoError, "group publish: staged main file missing: " + stagedMainPath );

  // Auto-recover any leftover journal from previous interrupted runs
  recoverDatasetGroup( targetMainPath );

  const std::vector<std::string> stagedSidecars = sidecarsFor( stagedMainPath );
  const std::vector<std::string> targetSidecars = sidecarsFor( targetMainPath );
  std::vector<bool> hadTarget( targetSidecars.size(), false );
  const bool hadMainTarget = fileExists( targetMainPath );
  const std::string mainBackup = targetMainPath + ".bak";
  std::vector<std::string> published;

  const std::string journalPath = targetMainPath + ".publish-journal";
  const std::string commitMarkerPath = targetMainPath + ".publish-journal.committed";

  // Prepare journal members and write persistent journal before modifying any targets
  std::vector<JournalMember> journalMembers;
  for ( std::size_t i = 0; i < stagedSidecars.size(); ++i )
  {
    if ( fileExists( stagedSidecars[i] ) )
    {
      hadTarget[i] = fileExists( targetSidecars[i] );
      journalMembers.push_back( { stagedSidecars[i], targetSidecars[i], targetSidecars[i] + ".bak", hadTarget[i] } );
    }
  }
  journalMembers.push_back( { stagedMainPath, targetMainPath, mainBackup, hadMainTarget } );

  // #1701: sweep STALE member backups BEFORE the journal names them. After
  // recoverDatasetGroup() above no live journal exists, so any leftover
  // <target>.bak is residue of an older interrupted run (a journal that
  // failed to parse is deleted WITHOUT touching .baks). Left in place, a
  // crash between writeJournalFile() and backupInPlace() would let the next
  // recovery "restore" those ancient bytes over the current good target.
  // From here on, any .bak that exists was written by THIS run's
  // backupInPlace() — a provable pre-publish image.
  for ( const JournalMember &member : journalMembers )
    removeFileQuiet( member.backup );

  writeJournalFile( journalPath, targetMainPath, journalMembers );

  auto cleanup = [ & ]( const std::string &failedName ) {
    std::vector<std::string> restoreFailures;
    for ( const std::string &done : published )
      removeFileQuiet( done );
    bool mainRestored = false;
    if ( hadMainTarget && fileExists( mainBackup ) )
    {
      removeFileQuiet( targetMainPath );
      if ( !renameReplaceQuiet( mainBackup, targetMainPath ) )
        restoreFailures.push_back( mainBackup + " -> " + targetMainPath );
      else
        mainRestored = true;
    }
    std::vector<bool> sidecarsRestored( targetSidecars.size(), false );
    for ( std::size_t i = 0; i < targetSidecars.size(); ++i )
    {
      const std::string backup = targetSidecars[i] + ".bak";
      if ( hadTarget[i] && fileExists( backup ) )
      {
        removeFileQuiet( targetSidecars[i] );
        if ( !renameReplaceQuiet( backup, targetSidecars[i] ) )
          restoreFailures.push_back( backup + " -> " + targetSidecars[i] );
        else
          sidecarsRestored[i] = true;
      }
    }
    if ( !hadMainTarget || mainRestored )
      removeFileQuiet( mainBackup );
    for ( std::size_t i = 0; i < targetSidecars.size(); ++i )
    {
      if ( !hadTarget[i] || sidecarsRestored[i] )
        removeFileQuiet( targetSidecars[i] + ".bak" );
    }
    removeFileQuiet( journalPath );

    Json::Value details;
    details["failed_member"] = failedName;
    if ( !restoreFailures.empty() )
    {
      Json::Value fails( Json::arrayValue );
      for ( const auto &f : restoreFailures )
        fails.append( f );
      details["restore_failures"] = fails;
      throw GeoError( ErrorCode::IoError,
                      "group publish failed at " + failedName +
                          "; target group rollback encountered restore failures",
                      details );
    }
    throw GeoError( ErrorCode::IoError, "group publish failed at " + failedName + "; target group rolled back", details );
  };

  for ( std::size_t i = 0; i < stagedSidecars.size(); ++i )
  {
    if ( !fileExists( stagedSidecars[i] ) )
      continue;
    hadTarget[i] = fileExists( targetSidecars[i] );
    if ( hadTarget[i] )
    {
      const std::string backup = targetSidecars[i] + ".bak";
      if ( !backupInPlace( targetSidecars[i], backup ) )
        cleanup( targetSidecars[i] );
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

  if ( hadMainTarget )
  {
    if ( !backupInPlace( targetMainPath, mainBackup ) )
      cleanup( targetMainPath );
  }
  try
  {
    publishStagedFile( stagedMainPath, targetMainPath );
  }
  catch ( const GeoError & )
  {
    cleanup( targetMainPath );
  }

  // Atomically mark committed
  // #1701: an unlanded marker means the NEXT recovery reads the journal as
  // uncommitted and rolls the group back. That is only sound while the
  // backups still exist — deleting them below regardless of the rename
  // result turned a marker failure into "delete freshly published targets
  // with had_target=false and keep new bytes elsewhere": half committed,
  // half destroyed. Fail LOUDLY, keep journal + backups: recovery then
  // restores the full pre-publish group (or a retry re-publishes).
  if ( !renameReplaceQuiet( journalPath, commitMarkerPath ) )
  {
    Json::Value details;
    details["journal"] = journalPath;
    details["commit_marker"] = commitMarkerPath;
    details["recovery"] = "atomic_fs::recoverDatasetGroup(<main target>) restores the pre-publish group";
    throw GeoError( ErrorCode::IoError,
                    "group publish completed but the commit marker could not be "
                    "installed; journal and backups kept for recovery: " + targetMainPath,
                    details );
  }

  // Success: drop the backup set
  removeFileQuiet( mainBackup );
  for ( std::size_t i = 0; i < targetSidecars.size(); ++i )
    removeFileQuiet( targetSidecars[i] + ".bak" );

  // Remove committed marker
  removeFileQuiet( commitMarkerPath );
}

void publishStagedMembers( const std::vector<std::pair<std::string, std::string>> &members )
{
  std::vector<std::pair<std::string, std::string>> live;
  live.reserve( members.size() );
  for ( const auto &m : members )
  {
    if ( fileExists( m.first ) )
      live.push_back( m );
  }
  if ( live.empty() )
    return;

  const std::string mainTarget = live.back().second;
  recoverDatasetGroup( mainTarget );

  std::vector<bool> hadTarget( live.size(), false );
  std::vector<std::string> published;
  const std::string journalPath = mainTarget + ".publish-journal";
  const std::string commitMarkerPath = mainTarget + ".publish-journal.committed";

  std::vector<JournalMember> journalMembers;
  for ( std::size_t i = 0; i < live.size(); ++i )
  {
    hadTarget[i] = fileExists( live[i].second );
    journalMembers.push_back( { live[i].first, live[i].second, live[i].second + ".bak", hadTarget[i] } );
  }
  // #1701: same stale-backup sweep as publishStagedGroup — after the
  // recover above no live journal exists, so leftover .baks are old-run
  // residue that must not become this run's rollback source.
  for ( const JournalMember &member : journalMembers )
    removeFileQuiet( member.backup );
  writeJournalFile( journalPath, mainTarget, journalMembers );

  auto cleanup = [ & ]( const std::string &failedName ) {
    std::vector<std::string> restoreFailures;
    for ( const std::string &done : published )
      removeFileQuiet( done );
    std::vector<bool> restored( live.size(), false );
    if ( !live.empty() )
    {
      const std::size_t mainIdx = live.size() - 1;
      const std::string mainBackup = live[mainIdx].second + ".bak";
      if ( hadTarget[mainIdx] && fileExists( mainBackup ) )
      {
        removeFileQuiet( live[mainIdx].second );
        if ( !renameReplaceQuiet( mainBackup, live[mainIdx].second ) )
          restoreFailures.push_back( mainBackup + " -> " + live[mainIdx].second );
        else
          restored[mainIdx] = true;
      }
      for ( std::size_t i = 0; i + 1 < live.size(); ++i )
      {
        const std::string backup = live[i].second + ".bak";
        if ( hadTarget[i] && fileExists( backup ) )
        {
          removeFileQuiet( live[i].second );
          if ( !renameReplaceQuiet( backup, live[i].second ) )
            restoreFailures.push_back( backup + " -> " + live[i].second );
          else
            restored[i] = true;
        }
      }
    }
    for ( std::size_t i = 0; i < live.size(); ++i )
    {
      if ( !hadTarget[i] || ( i < restored.size() && restored[i] ) )
        removeFileQuiet( live[i].second + ".bak" );
    }
    removeFileQuiet( journalPath );

    Json::Value details;
    details["failed_member"] = failedName;
    if ( !restoreFailures.empty() )
    {
      Json::Value fails( Json::arrayValue );
      for ( const auto &f : restoreFailures )
        fails.append( f );
      details["restore_failures"] = fails;
      throw GeoError( ErrorCode::IoError,
                      "group publish failed at " + failedName +
                          "; target group rollback encountered restore failures",
                      details );
    }
    throw GeoError( ErrorCode::IoError, "group publish failed at " + failedName + "; target group rolled back", details );
  };

  for ( std::size_t i = 0; i < live.size(); ++i )
  {
    hadTarget[i] = fileExists( live[i].second );
    if ( hadTarget[i] )
    {
      const std::string backup = live[i].second + ".bak";
      if ( !backupInPlace( live[i].second, backup ) )
        cleanup( live[i].second );
    }
    try
    {
      publishStagedFile( live[i].first, live[i].second );
    }
    catch ( const GeoError & )
    {
      cleanup( live[i].second );
    }
    published.push_back( live[i].second );
  }

  // Atomically mark committed — checked, same #1701 contract as
  // publishStagedGroup: an unlanded marker plus deleted backups is a
  // half-destroyed group at the next recovery; keep journal + backups and
  // fail loudly instead.
  if ( !renameReplaceQuiet( journalPath, commitMarkerPath ) )
  {
    Json::Value details;
    details["journal"] = journalPath;
    details["commit_marker"] = commitMarkerPath;
    details["recovery"] = "atomic_fs::recoverDatasetGroup(<main target>) restores the pre-publish group";
    throw GeoError( ErrorCode::IoError,
                    "member publish completed but the commit marker could not be "
                    "installed; journal and backups kept for recovery: " + mainTarget,
                    details );
  }

  for ( const auto &m : live )
    removeFileQuiet( m.second + ".bak" );

  removeFileQuiet( commitMarkerPath );
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
