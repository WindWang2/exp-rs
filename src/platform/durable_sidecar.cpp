/***************************************************************************
  platform/durable_sidecar.cpp — see durable_sidecar.h for the contract.
 ***************************************************************************/

#include "platform/durable_sidecar.h"

#include "platform/portable.h"

#include <atomic>
#include <cstdio>
#include <random>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace sicnu::platform::sidecar
{
namespace
{

constexpr int kMaxStagedNameAttempts = 8;

enum class ReadOutcome
{
  Ok,
  Missing,
  Unreadable
};

std::atomic<unsigned> stagingCounter{ 0 };

std::uint64_t stagingProcessId()
{
  return static_cast<std::uint64_t>( sicnu::portable::pid() );
}

std::string u8FromFsPath( const std::filesystem::path &path )
{
  const std::u8string text = path.u8string();
  return std::string( text.begin(), text.end() );
}

std::filesystem::path fsPathFromU8Leaf( const std::filesystem::path &directory, const std::string &leaf )
{
  return directory / std::filesystem::path( std::u8string( leaf.begin(), leaf.end() ) );
}

/// Same staged-name shape as geo::atomic_fs::stagedPathFor: unique per pid +
/// counter + random_device entropy, claimed O_EXCL, final extension kept.
/// Returns an empty string when no name could be claimed (@p osError carries
/// the raw code of the non-race refusal).
std::string claimStagedPath( const std::string &targetPath, std::string &osError )
{
  const std::filesystem::path target = sicnu::portable::pathFromUtf8( targetPath );
  const std::string filename = u8FromFsPath( target.filename() );
  const std::size_t dot = filename.rfind( '.' );
  const std::string stem = dot == std::string::npos ? filename : filename.substr( 0, dot );
  const std::string extension = dot == std::string::npos ? "" : filename.substr( dot );

  static thread_local std::mt19937_64 rng{ [] {
    std::random_device rd;
    return std::mt19937_64{ rd() ^ ( stagingProcessId() << 1 ) };
  }() };

  const std::filesystem::path directory =
    target.parent_path().empty() ? std::filesystem::path( "." ) : target.parent_path();

  for ( int attempt = 0; attempt < kMaxStagedNameAttempts; ++attempt )
  {
    const std::string leaf = stem + "." + std::to_string( stagingProcessId() ) + "." +
                             std::to_string( stagingCounter++ ) + "." +
                             std::to_string( rng() ) + ".tmp" + extension;
    const std::string staged = u8FromFsPath( fsPathFromU8Leaf( directory, leaf ) );

    portable::ClaimFailure failure = portable::ClaimFailure::Other;
    std::uint64_t errorCode = 0;
    if ( portable::claimExclusiveUtf8( staged, &failure, &errorCode ) )
      return staged;

    if ( failure != portable::ClaimFailure::NameExists )
    {
      osError = std::to_string( errorCode );
      return std::string();
    }
    // NameExists: ordinary lost-claim race — retry with a fresh name.
  }
  osError = "exhausted";
  return std::string();
}

bool removeQuiet( const std::string &path )
{
  std::error_code ec;
  std::filesystem::remove( sicnu::portable::pathFromUtf8( path ), ec );
  return !ec;
}

std::string lastOsError()
{
#ifdef _WIN32
  return "win32_error=" + std::to_string( ::GetLastError() );
#else
  return "errno=" + std::to_string( errno );
#endif
}

/// Writes the staged temp file completely. False with @p error on open or
/// short-write/stream failure.
bool writeTempFile( const std::string &stagedPath, const std::string &bytes, std::string &error )
{
  std::FILE *file = portable::fileOpenUtf8( stagedPath, "wb" );
  if ( !file )
  {
    error = "open staged failed (" + lastOsError() + ")";
    return false;
  }
  const std::size_t written = bytes.empty() ? 0u : std::fwrite( bytes.data(), 1, bytes.size(), file );
  const bool streamError = std::ferror( file ) != 0;
  std::fclose( file );
  if ( written != bytes.size() || streamError )
  {
    error = "short write on staged file (" + std::to_string( written ) + " of " +
            std::to_string( bytes.size() ) + " bytes)";
    return false;
  }
  return true;
}

/// Publishes a fully-written + fsynced staged file onto the target.
/// POSIX: rename(2). Windows: READONLY attr cleared, ReplaceFileW when the
/// target exists, else MoveFileExW(MOVEFILE_REPLACE_EXISTING |
/// MOVEFILE_WRITE_THROUGH). Mirrors geo::atomic_fs::publishStagedFile's
/// per-platform semantics for the single-file case (COPY_ALLOWED is not
/// needed here: the staged name is always claimed in the target's own
/// directory, so the rename can never cross a volume).
bool publishStaged( const std::string &stagedPath, const std::string &targetPath, std::string &error )
{
#ifdef _WIN32
  const std::wstring staged = portable::wideFromUtf8( stagedPath );
  const std::wstring target = portable::wideFromUtf8( targetPath );

  const DWORD attrs = ::GetFileAttributesW( target.c_str() );
  if ( attrs != INVALID_FILE_ATTRIBUTES && ( attrs & FILE_ATTRIBUTE_READONLY ) )
    ::SetFileAttributesW( target.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY );

  const DWORD replaceError = ::GetLastError();
  if ( attrs != INVALID_FILE_ATTRIBUTES &&
       ::ReplaceFileW( target.c_str(), staged.c_str(), nullptr,
                       REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr ) )
  {
    return true;
  }
  if ( ::MoveFileExW( staged.c_str(), target.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) )
  {
    return true;
  }
  // Carry both reasons: the ReplaceFileW attempt failed first (attrs may
  // have been INVALID), then the MoveFileExW fallback failed with the error
  // that actually refused the publish.
  error = "publish failed (replace_error=" + std::to_string( replaceError ) +
          ", " + lastOsError() + ")";
  return false;
#else
  if ( ::rename( stagedPath.c_str(), targetPath.c_str() ) == 0 )
    return true;
  error = "publish failed (" + lastOsError() + ")";
  return false;
#endif
}

bool fileSizeAtMost( const std::string &path, std::size_t maxBytes, std::string &error )
{
  std::error_code ec;
  const std::uintmax_t size = std::filesystem::file_size( sicnu::portable::pathFromUtf8( path ), ec );
  if ( ec )
  {
    error = "stat failed: " + ec.message();
    return false;
  }
  if ( size > maxBytes )
  {
    error = "file is " + std::to_string( size ) + " bytes, above the " +
            std::to_string( maxBytes ) + " cap";
    return false;
  }
  return true;
}

/// Reads a whole file (UTF-8 path), refusing above maxBytes.
ReadOutcome readWholeFile( const std::string &path, std::size_t maxBytes, std::string &out,
                           std::string &error )
{
  std::error_code ec;
  const std::filesystem::path fsPath = sicnu::portable::pathFromUtf8( path );
  // Check the error code FIRST: filesystem::exists(p, ec) returns false when
  // the status query itself fails (denied traversal, broken chain), so
  // testing !exists alone would swallow the error and report Missing —
  // fail-open ("fresh state") for an existing-but-unstatable sidecar.
  std::filesystem::exists( fsPath, ec );
  if ( ec )
  {
    error = "stat failed: " + ec.message();
    return ReadOutcome::Unreadable;
  }
  if ( !std::filesystem::exists( fsPath ) )
    return ReadOutcome::Missing;

  std::string statError;
  if ( !fileSizeAtMost( path, maxBytes, statError ) )
  {
    error = "oversize: " + statError;
    return ReadOutcome::Unreadable;
  }

  // Open with FULL sharing on Windows: the authority own reads (verify
  // read-back, recovery resolution) must never create a sharing-violation
  // window that fails a concurrent publisher rename (win32_error=5/32).
  std::string bytes;
#ifdef _WIN32
  const HANDLE handle = ::CreateFileW( portable::wideFromUtf8( path ).c_str(), GENERIC_READ,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
  if ( handle == INVALID_HANDLE_VALUE )
  {
    error = "open failed (" + lastOsError() + ")";
    return ReadOutcome::Unreadable;
  }
  char buffer[8192];
  DWORD got = 0;
  for ( ;; )
  {
    if ( !::ReadFile( handle, buffer, sizeof( buffer ), &got, nullptr ) )
    {
      error = "read failed (" + lastOsError() + ")";
      ::CloseHandle( handle );
      return ReadOutcome::Unreadable;
    }
    if ( got == 0 )
      break;
    bytes.append( buffer, got );
    if ( bytes.size() > maxBytes )
    {
      // Grew past the cap between stat and read — enforce the bound here,
      // not just in the pre-check.
      error = "oversize during read";
      ::CloseHandle( handle );
      return ReadOutcome::Unreadable;
    }
  }
  ::CloseHandle( handle );
#else
  std::FILE *file = portable::fileOpenUtf8( path, "rb" );
  if ( !file )
  {
    error = "open failed (" + lastOsError() + ")";
    return ReadOutcome::Unreadable;
  }
  char buffer[8192];
  std::size_t got = 0;
  while ( ( got = std::fread( buffer, 1, sizeof( buffer ), file ) ) > 0 )
  {
    bytes.append( buffer, got );
    if ( bytes.size() > maxBytes )
    {
      error = "oversize during read";
      std::fclose( file );
      return ReadOutcome::Unreadable;
    }
  }
  const bool readError = std::ferror( file ) != 0;
  std::fclose( file );
  if ( readError )
  {
    error = "read failed";
    return ReadOutcome::Unreadable;
  }
#endif
  out = bytes;
  return ReadOutcome::Ok;
}

/// One claim → write → durability → publish pass with no fault seams (used
/// verbatim for the last-good rotation). On failure the target is untouched
/// and the staged file is removed; an unremovable temp is reported through
/// @p residue (inert by contract).
bool stageAndPublish( const std::string &targetPath, const std::string &bytes, std::size_t maxBytes,
                      std::string &error, std::string &residue )
{
  std::string claimError;
  const std::string staged = claimStagedPath( targetPath, claimError );
  if ( staged.empty() )
  {
    error = "claim staged name failed: " + claimError;
    return false;
  }

  const auto failWith = [&]( const std::string &message ) {
    error = message;
    if ( !removeQuiet( staged ) )
      residue = staged;
    return false;
  };

  std::string writeError;
  if ( !writeTempFile( staged, bytes, writeError ) )
    return failWith( writeError );

  portable::SyncFailure syncFailure = portable::SyncFailure::OpenFailed;
  std::uint64_t syncError = 0;
  if ( !portable::syncFileUtf8( staged, &syncFailure, &syncError ) )
  {
    const char *why = syncFailure == portable::SyncFailure::OpenFailed ? "open" : "flush";
    return failWith( std::string( "durability gate failed (" ) + why + ", os_error=" +
                     std::to_string( syncError ) + ")" );
  }

  std::string publishError;
  if ( !publishStaged( staged, targetPath, publishError ) )
    return failWith( publishError );

  portable::syncDirectoryBestEffortUtf8( targetPath );
  return true;
}

} // namespace

const char *writeStatusName( WriteStatus status )
{
  switch ( status )
  {
    case WriteStatus::Ok:
      return "ok";
    case WriteStatus::EmptyPath:
      return "empty_path";
    case WriteStatus::TooLarge:
      return "payload_too_large";
    case WriteStatus::ClaimFailed:
      return "staged_claim_failed";
    case WriteStatus::WriteFailed:
      return "write_failed";
    case WriteStatus::DurabilityFailed:
      return "durability_failed";
    case WriteStatus::PublishFailed:
      return "publish_failed";
    case WriteStatus::VerifyFailed:
      return "verify_failed";
    case WriteStatus::LastGoodFailed:
      return "last_good_failed";
    case WriteStatus::StagedCleanupFailed:
      return "staged_cleanup_failed";
  }
  return "unknown";
}

WriteResult write( const WriteRequest &request )
{
  WriteResult result;

  // Prepare: validation refusals happen before any filesystem effect.
  if ( request.targetPath.empty() )
  {
    result.status = WriteStatus::EmptyPath;
    result.error = "empty target path";
    return result;
  }
  if ( request.bytes.size() > request.maxBytes )
  {
    result.status = WriteStatus::TooLarge;
    result.error = "payload is " + std::to_string( request.bytes.size() ) +
                   " bytes, above the " + std::to_string( request.maxBytes ) + " cap";
    return result;
  }

  std::string claimError;
  const std::string staged = claimStagedPath( request.targetPath, claimError );
  if ( staged.empty() )
  {
    result.status = WriteStatus::ClaimFailed;
    result.error = "claim staged name failed: " + claimError;
    return result;
  }

  // Write temp: an injected fault takes the real write-failure branch
  // (temp discarded, target untouched) — the same observable state as a
  // short write; payloadOverride at this phase instead models a torn write
  // that the verify gate must catch.
  if ( request.hooks.failAt && request.hooks.failAt( WritePhase::WriteTemp ) )
  {
    if ( !removeQuiet( staged ) )
    {
      result.status = WriteStatus::StagedCleanupFailed;
      result.stagedPath = staged;
    }
    else
    {
      result.status = WriteStatus::WriteFailed;
    }
    result.error = "injected WriteTemp fault";
    return result;
  }
  const std::string payload = request.hooks.payloadOverride
                                ? request.hooks.payloadOverride( WritePhase::WriteTemp )
                                : request.bytes;

  std::string writeError;
  if ( !writeTempFile( staged, payload, writeError ) )
  {
    if ( !removeQuiet( staged ) )
    {
      result.status = WriteStatus::StagedCleanupFailed;
      result.stagedPath = staged;
    }
    else
    {
      result.status = WriteStatus::WriteFailed;
    }
    result.error = writeError;
    return result;
  }

  // Durability boundary: a failed sync refuses the publish — the real
  // branch discards the temp and leaves the target untouched.
  if ( request.hooks.failAt && request.hooks.failAt( WritePhase::Durability ) )
  {
    if ( !removeQuiet( staged ) )
    {
      result.status = WriteStatus::StagedCleanupFailed;
      result.stagedPath = staged;
    }
    else
    {
      result.status = WriteStatus::DurabilityFailed;
    }
    result.error = "injected Durability fault";
    return result;
  }
  portable::SyncFailure syncFailure = portable::SyncFailure::OpenFailed;
  std::uint64_t syncError = 0;
  if ( !portable::syncFileUtf8( staged, &syncFailure, &syncError ) )
  {
    const char *why = syncFailure == portable::SyncFailure::OpenFailed ? "open" : "flush";
    if ( !removeQuiet( staged ) )
    {
      result.status = WriteStatus::StagedCleanupFailed;
      result.stagedPath = staged;
    }
    else
    {
      result.status = WriteStatus::DurabilityFailed;
    }
    result.error = std::string( "durability gate failed (" ) + why + ", os_error=" +
                   std::to_string( syncError ) + ")";
    return result;
  }

  // Publish: an injected fault takes the real publish-failure branch
  // (temp discarded, target untouched) BEFORE the rename is attempted.
  if ( request.hooks.failAt && request.hooks.failAt( WritePhase::Publish ) )
  {
    if ( !removeQuiet( staged ) )
    {
      result.status = WriteStatus::StagedCleanupFailed;
      result.stagedPath = staged;
    }
    else
    {
      result.status = WriteStatus::PublishFailed;
    }
    result.error = "injected Publish fault";
    return result;
  }
  std::string publishError;
  if ( !publishStaged( staged, request.targetPath, publishError ) )
  {
    if ( !removeQuiet( staged ) )
    {
      result.status = WriteStatus::StagedCleanupFailed;
      result.stagedPath = staged;
    }
    else
    {
      result.status = WriteStatus::PublishFailed;
    }
    result.error = publishError;
    return result;
  }
  portable::syncDirectoryBestEffortUtf8( request.targetPath );

  // Last-good rotation: main is already committed; the rotation is
  // best-effort and its failure is reported, never fatal.
  if ( !request.lastGoodSuffix.empty() )
  {
    if ( request.hooks.failAt && request.hooks.failAt( WritePhase::LastGood ) )
    {
      result.lastGoodStatus = WriteStatus::LastGoodFailed;
      result.error = "injected LastGood fault";
    }
    else
    {
      const std::string lastGoodPath = request.targetPath + request.lastGoodSuffix;
      std::string lastGoodError;
      std::string lastGoodResidue;
      if ( stageAndPublish( lastGoodPath, request.bytes, request.maxBytes, lastGoodError,
                            lastGoodResidue ) )
      {
        result.lastGoodWritten = true;
      }
      else
      {
        result.lastGoodStatus = WriteStatus::LastGoodFailed;
        result.error = "last-good rotation failed: " + lastGoodError;
        if ( !lastGoodResidue.empty() )
          result.stagedPath = lastGoodResidue;
      }
    }
  }

  // Verify: read the published main back and byte-compare with the payload
  // (not the torn override — the gate measures what was SUPPOSED to publish).
  if ( request.verifyReadBack )
  {
    if ( request.hooks.failAt && request.hooks.failAt( WritePhase::Verify ) )
    {
      result.status = WriteStatus::VerifyFailed;
      if ( !result.error.empty() )
        result.error += "; ";
      result.error += "injected Verify fault";
      return result;
    }
    std::string published;
    std::string readError;
    if ( readWholeFile( request.targetPath, request.maxBytes, published, readError ) !=
         ReadOutcome::Ok )
    {
      result.status = WriteStatus::VerifyFailed;
      if ( !result.error.empty() )
        result.error += "; ";
      result.error += "verify read-back failed: " + readError;
      return result;
    }
    if ( published != request.bytes )
    {
      result.status = WriteStatus::VerifyFailed;
      if ( !result.error.empty() )
        result.error += "; ";
      result.error += "verify mismatch: published main differs from the payload";
      return result;
    }
  }

  return result;
}

ReadResult read( const std::string &targetPath, const std::string &lastGoodSuffix,
                 std::size_t maxBytes, const ReadHooks &hooks )
{
  ReadResult result;

  const bool mainInjectedFail = hooks.failAt && hooks.failAt( ReadPhase::ReadMain );
  std::string mainError;
  ReadOutcome mainOutcome = ReadOutcome::Unreadable;
  if ( !mainInjectedFail )
  {
    mainOutcome = readWholeFile( targetPath, maxBytes, result.bytes, mainError );
  }
  else
  {
    mainError = "injected ReadMain fault";
  }

  if ( mainOutcome == ReadOutcome::Ok )
  {
    result.source = ReadSource::Main;
    return result;
  }

  result.error = "main: " + ( mainError.empty() ? std::string( "missing" ) : mainError );
  result.bytes.clear();

  if ( lastGoodSuffix.empty() )
  {
    result.source = mainOutcome == ReadOutcome::Missing ? ReadSource::Missing
                                                        : ReadSource::Corrupt;
    return result;
  }

  const bool lastGoodInjectedFail = hooks.failAt && hooks.failAt( ReadPhase::ReadLastGood );
  std::string lastGoodError;
  ReadOutcome lastGoodOutcome = ReadOutcome::Unreadable;
  if ( !lastGoodInjectedFail )
  {
    lastGoodOutcome =
      readWholeFile( targetPath + lastGoodSuffix, maxBytes, result.bytes, lastGoodError );
  }
  else
  {
    lastGoodError = "injected ReadLastGood fault";
  }

  if ( lastGoodOutcome == ReadOutcome::Ok )
  {
    result.source = ReadSource::LastGood;
    result.error += "; last-good: recovered";
    return result;
  }

  result.error += "; last-good: " + ( lastGoodError.empty() ? std::string( "missing" ) : lastGoodError );
  result.bytes.clear();
  result.source = mainOutcome == ReadOutcome::Missing && lastGoodOutcome == ReadOutcome::Missing
                    ? ReadSource::Missing
                    : ReadSource::Corrupt;
  return result;
}

} // namespace sicnu::platform::sidecar
