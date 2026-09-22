/***************************************************************************
  geospatial/fabric/mirror.cpp — explicit offline mirror.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/fabric/mirror.h"

#include "geospatial/fabric/object_store.h"
#include "geospatial/identity/asset_identity.h"
#include "geospatial/remote/remote_identity_token.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/util/resource_uri.h"
#include "geospatial/util/sha256.h"
#include "geospatial/util/time_normalization.h"

#include <cpl_vsi.h>

#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#endif

namespace sicnu::geo
{

namespace
{

constexpr const char *kMirrorManifest = "manifest.json";
constexpr const char *kMirrorChunkDir = "chunks";
constexpr const char *kMirrorLockFile = "writer.lock";

/// A manifest is a bounded inventory (hundreds of bytes per chunk entry);
/// a planted oversized file must never be slurped into memory.
constexpr std::uintmax_t kMaxMirrorManifestBytes = 64ull * 1024ull * 1024ull;

/// A manifest-controlled chunk file name is a PLAIN BASENAME inside the
/// mirror's chunk directory: separators, "..", drive letters and control
/// characters would let a crafted manifest escape the mirror root.
bool isSafeMirrorChunkFileName( const std::string &file )
{
  if ( file.empty() || file == "." || file == ".." )
    return false;
  if ( file.find( '/' ) != std::string::npos || file.find( '\\' ) != std::string::npos )
    return false;
  if ( file.find( ':' ) != std::string::npos )
    return false;
  for ( const char c : file )
  {
    if ( static_cast<unsigned char>( c ) < 0x20 )
      return false;
  }
  return true;
}

/// 13.0: a directory entry's name in UTF-8 — the manifest's own domain,
/// so the orphan test compares like with like. POSIX names are bytes and
/// always convert; on Windows the UTF-16→UTF-8 step throws on an invalid
/// boundary (unpaired surrogate) — and a lossy implementation that
/// substitutes U+FFFD instead of throwing is caught by the round-trip
/// check: a name that cannot re-encode to itself was never proven
/// unreferenced — the caller counts it as a refusal, never as an orphan
/// to delete (fail closed).
bool utf8FileName( const std::filesystem::directory_entry &entry, std::string &out )
{
  try
  {
    const std::filesystem::path filename = entry.path().filename();
    const std::u8string name = filename.u8string();
    out.assign( reinterpret_cast<const char *>( name.c_str() ), name.size() );
    if ( std::filesystem::u8path( out ) != filename )
    {
      out.clear();
      return false;
    }
    return true;
  }
  catch ( const std::exception & )
  {
    out.clear();
    return false;
  }
}

Json::Value readManifest( const std::string &mirrorDirectory )
{
  const std::string path = mirrorDirectory + "/" + kMirrorManifest;
  VSIStatBufL statBuffer;
  if ( VSIStatL( path.c_str(), &statBuffer ) != 0 )
    return Json::Value( Json::objectValue );
  if ( static_cast<std::uintmax_t>( statBuffer.st_size ) > kMaxMirrorManifestBytes )
    return Json::Value();   // corrupt-by-contract (oversized) — caller counts the skip
  // The stream opens the UTF-8 path — a narrow-char open would route a
  // non-ASCII mirror root through the Windows ANSI code page and fail.
  std::ifstream in( std::filesystem::u8path( path ), std::ios::binary );
  if ( !in )
    // Stat succeeded but the open failed (permissions, transient lock):
    // an UNREADABLE manifest, not an empty one — reporting it empty
    // would mark every chunk file an orphan and let prune delete the
    // whole mirror payload (fail closed, same as a corrupt parse).
    return Json::Value();
  std::string text( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
  Json::CharReaderBuilder builder;
  std::string errors;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  Json::Value parsed;
  if ( !reader->parse( text.data(), text.data() + text.size(), &parsed, &errors ) )
    return Json::Value();   // corrupt manifest — caller counts the skip
  return parsed;
}

/// Manifest snapshot cache (11.0): replay reads consult the manifest per
/// window; re-parsing the whole JSON per (token, key) lookup is O(manifest)
/// per chunk. Keyed by (directory, size, mtime) — a manifest REWRITE by a
/// concurrent materialization pass changes both and invalidates the entry,
/// so a cached snapshot can never hide a newer entry for long, and the
/// lookup contract (typed skip, never a guess) is unaffected.
struct CachedManifest
{
  Json::Value value;
  std::uint64_t size = 0;
  std::int64_t mtime = 0;
};
std::mutex g_manifestCacheMutex;
std::map<std::string, CachedManifest> g_manifestCache;

/// The parsed manifest for a directory, cache-aware. A corrupt manifest
/// parses to a NULL value (callers count the skip); missing = empty object.
Json::Value manifestSnapshot( const std::string &mirrorDirectory )
{
  const std::string path = mirrorDirectory + "/" + kMirrorManifest;
  VSIStatBufL statBuffer;
  if ( VSIStatL( path.c_str(), &statBuffer ) != 0 )
    return Json::Value( Json::objectValue );
  const std::uint64_t size = static_cast<std::uint64_t>( statBuffer.st_size );
  const std::int64_t mtime = static_cast<std::int64_t>( statBuffer.st_mtime );
  {
    std::lock_guard<std::mutex> lock( g_manifestCacheMutex );
    const auto it = g_manifestCache.find( path );
    if ( it != g_manifestCache.end() && it->second.size == size && it->second.mtime == mtime )
      return it->second.value;
  }
  Json::Value parsed = readManifest( mirrorDirectory );
  // #1186: do not cache a NULL (unreadable/corrupt) snapshot under the current
  // (size,mtime) — a transient Windows sharing-violation open failure would
  // stick as "manifest corrupt" until the file is rewritten.
  if ( parsed.isNull() )
    return parsed;
  std::lock_guard<std::mutex> lock( g_manifestCacheMutex );
  // Cap the cache: manifest rewrites allocate a new snapshot; the map only
  // needs the few directories this process replays from.
  if ( g_manifestCache.size() > 8 )
    g_manifestCache.clear();
  g_manifestCache[path] = CachedManifest { parsed, size, mtime };
  return parsed;
}

void invalidateManifestSnapshot( const std::string &mirrorDirectory )
{
  std::lock_guard<std::mutex> lock( g_manifestCacheMutex );
  g_manifestCache.erase( mirrorDirectory + "/" + kMirrorManifest );
}

/// ISO-8601 UTC now (materialization timestamps; expiry basis). Same shape
/// as the validator's checkedAt stamps.
std::string nowIso8601Utc()
{
  const auto now = std::chrono::system_clock::now();
  const auto itt = std::chrono::system_clock::to_time_t( now );
  std::tm tmBuf {};
#ifdef _WIN32
  gmtime_s( &tmBuf, &itt );
#else
  gmtime_r( &itt, &tmBuf );
#endif
  char buffer[32];
  std::strftime( buffer, sizeof( buffer ), "%Y-%m-%dT%H:%M:%SZ", &tmBuf );
  return buffer;
}

/// SHA-256 hex of a local file (integrity checksum of a mirrored chunk).
/// Returns "" when the file cannot be read back.
/// 13.0: reads through the VSI layer — the manifest's file-name domain is
/// UTF-8 and VSIFOpenL converts it to the native spelling on every
/// platform; a narrow std::ifstream would route a non-ASCII name through
/// the active code page on Windows and fail the proof of a healthy entry.
std::string fileSha256Hex( const std::string &path )
{
  VSILFILE *file = VSIFOpenL( path.c_str(), "rb" );
  if ( file == nullptr )
    return {};
  Sha256 hash;
  char buffer[64 * 1024];
  std::size_t read = 0;
  while ( ( read = VSIFReadL( buffer, 1, sizeof( buffer ), file ) ) > 0 )
    hash.update( buffer, read );
  VSIFCloseL( file );
  return toHex( hash.finalize() );
}

#ifdef _WIN32
/// UTF-8→UTF-16 for the Win32 lock call — the mirror path domain is
/// UTF-8 everywhere else (VSIStatL, fs::u8path), so the A-codepage
/// CreateFileA would misresolve a non-ASCII mirror root.
std::wstring wideFromUtf8( const std::string &text )
{
  if ( text.empty() )
    return std::wstring();
  const int size = MultiByteToWideChar( CP_UTF8, 0, text.c_str(), static_cast<int>( text.size() ),
                                        nullptr, 0 );
  std::wstring wide( static_cast<std::size_t>( size ), L'\0' );
  MultiByteToWideChar( CP_UTF8, 0, text.c_str(), static_cast<int>( text.size() ), wide.data(), size );
  return wide;
}
#endif

/// Single-writer guard for one mirror directory (O_EXCL create). The lock
/// records the writer's pid + timestamp (#1163): a leftover lock is broken
/// when its owner is provably dead, or — for an empty/unreadable pre-#1163
/// leftover — when it is older than any legitimate pass could run. A live
/// foreign writer is never stolen from, and the manifest itself is only
/// ever replaced atomically.
class MirrorWriterLock
{
  public:
    explicit MirrorWriterLock( const std::string &mirrorDirectory )
        : mLockPath( mirrorDirectory + "/" + kMirrorLockFile )
    {
      if ( tryAcquire() )
        return;
      // #1163: the acquisition failed against a leftover lock. Breaking it
      // is justified only when the recorded writer is DEAD or the lock is
      // older than any legitimate pass could run: a live foreign pid keeps
      // the single-writer contract, and a fresh lock with an unreadable
      // owner errs on the side of NOT stealing (recover by hand, the
      // refusal names the file).
      const StaleLockInfo stale = inspectStaleLock();
      const bool breakable = stale.knownOwner ? !pidAlive( stale.ownerPid )
                                              : stale.ageSeconds > kMaxLockAgeSeconds;
      if ( !breakable )
        return;
      atomic_fs::removeFileQuiet( mLockPath );
      if ( tryAcquire() )
        mBrokeStaleLock = true;
    }
    ~MirrorWriterLock()
    {
      if ( !mHeld )
        return;
#ifdef _WIN32
      CloseHandle( mHandle );
#else
      ::close( mHandle );
#endif
      atomic_fs::removeFileQuiet( mLockPath );
    }
    bool held() const { return mHeld; }
    /// True when this acquisition broke a stale (crashed-writer) lock.
    bool brokeStaleLock() const { return mBrokeStaleLock; }

  private:
    /// A legitimate pass finishes in minutes; anything older was abandoned
    /// by a crashed/killed writer.
    static constexpr std::int64_t kMaxLockAgeSeconds = 3600;

    struct StaleLockInfo
    {
      bool knownOwner = false;
      long ownerPid = -1;
      std::int64_t ageSeconds = -1;
    };

    bool tryAcquire()
    {
#ifdef _WIN32
      mHandle = CreateFileW( wideFromUtf8( mLockPath ).c_str(), GENERIC_WRITE, 0,
                             nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr );
      mHeld = mHandle != INVALID_HANDLE_VALUE;
#else
      mHandle = ::open( mLockPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644 );
      mHeld = mHandle >= 0;
#endif
      if ( mHeld )
        stampOwner();
      return mHeld;
    }

    /// Records this writer's pid + timestamp inside the lock so a later
    /// crashed run is distinguishable from a live one.
    void stampOwner()
    {
      const std::string stamp = std::to_string( static_cast<long>( ::getpid() ) ) + " " +
                                std::to_string( std::time( nullptr ) ) + "\n";
#ifdef _WIN32
      // The pid form differs across platforms; the AGE fallback below
      // still breaks crashed Windows writers.
      (void)stamp;
#else
      if ( mHandle >= 0 )
      {
        const ssize_t written = ::write( mHandle, stamp.c_str(), stamp.size() );
        (void)written;
      }
#endif
    }

    /// Reads pid + mtime from an existing lock. An EMPTY lock (a crash
    /// between create and stamp, or a pre-#1163 leftover) has no known
    /// owner — only the age rule can break it.
    StaleLockInfo inspectStaleLock() const
    {
      StaleLockInfo info;
      std::ifstream in( mLockPath );
      if ( in )
      {
        long pid = -1;
        std::int64_t stamp = 0;
        if ( in >> pid >> stamp )
        {
          info.knownOwner = true;
          info.ownerPid = pid;
          info.ageSeconds = std::time( nullptr ) - stamp;
        }
      }
      if ( info.ageSeconds < 0 )
      {
        struct ::stat st {};
        if ( ::stat( mLockPath.c_str(), &st ) == 0 )
          info.ageSeconds = std::time( nullptr ) - st.st_mtime;
      }
      return info;
    }

    static bool pidAlive( long pid )
    {
      if ( pid <= 0 )
        return false;
#ifdef _WIN32
      return false; // no liveness probe on Windows — the age rule decides
#else
      return ::kill( static_cast<pid_t>( pid ), 0 ) == 0 || errno != ESRCH;
#endif
    }

#ifdef _WIN32
    void *mHandle = nullptr;
#else
    int mHandle = -1;
#endif
    bool mHeld = false;
    bool mBrokeStaleLock = false;
    std::string mLockPath;
};

struct MirrorWalkResult
{
  MirrorReport report;
};

} // namespace

Json::Value MirrorReport::toJson() const
{
  Json::Value json;
  Json::Value chunkArray( Json::arrayValue );
  for ( const MirrorChunkOutcome &outcome : chunks )
  {
    Json::Value entry;
    entry["index"] = static_cast<Json::UInt64>( outcome.index );
    entry["status"] = outcome.status;
    if ( !outcome.file.empty() )
      entry["file"] = outcome.file;
    entry["bytes"] = static_cast<Json::UInt64>( outcome.bytes );
    if ( !outcome.token.empty() )
      entry["token"] = outcome.token.substr( 0, 16 );   // bounded prefix only
    if ( !outcome.chunkKey.empty() )
      entry["chunkKey"] = outcome.chunkKey;
    if ( !outcome.errorText.empty() )
      entry["error"] = outcome.errorText;
    if ( !outcome.assetIdHint.empty() )
      entry["assetIdHint"] = outcome.assetIdHint;
    chunkArray.append( entry );
  }
  json["chunks"] = chunkArray;
  json["bytesWritten"] = static_cast<Json::UInt64>( bytesWritten );
  json["mirrored"] = static_cast<Json::UInt64>( mirrored );
  json["alreadyPresent"] = static_cast<Json::UInt64>( alreadyPresent );
  json["skippedUnprovable"] = static_cast<Json::UInt64>( skippedUnprovable );
  json["skippedBudget"] = static_cast<Json::UInt64>( skippedBudget );
  json["skippedCancel"] = static_cast<Json::UInt64>( skippedCancel );
  json["failed"] = static_cast<Json::UInt64>( failed );
  json["budgetStopped"] = budgetStopped;
  json["cancelled"] = cancelled;
  return json;
}

std::string fabricChunkMirrorKey( const std::string &identityToken, const std::string &assetPath,
                                  const RasterWindow &sourceWindow, const std::string &bandSelector )
{
  // Stable across processes: the full identity basis, pipe-joined (the
  // token is hex, paths may carry '|' — the window/band fields are fixed
  // arity so the join stays unambiguous for keying purposes).
  const std::string basis = identityToken + "|" + assetPath + "|" + std::to_string( sourceWindow.xOff ) +
                            "," + std::to_string( sourceWindow.yOff ) + "," +
                            std::to_string( sourceWindow.width ) + "," +
                            std::to_string( sourceWindow.height ) + "|" + bandSelector;
  return sha256Hex( basis ).substr( 0, 32 );
}

std::string resolveMirrorHit( const std::string &mirrorDirectory, const std::string &token,
                              const std::string &chunkKey, std::string *skippedCorrupt )
{
  const Json::Value manifest = manifestSnapshot( mirrorDirectory );
  if ( manifest.isNull() || !manifest.isObject() )
  {
    if ( skippedCorrupt )
      *skippedCorrupt = "manifest unreadable";
    return {};
  }
  const Json::Value &entry = manifest[token];
  if ( !entry.isObject() || !entry[chunkKey].isObject() )
    return {};
  const Json::Value &chunk = entry[chunkKey];
  // Wrong-typed manifest entries are CORRUPT entries (a {file: {...}} must
  // skip, not throw Json::LogicError through the reader's GeoError catch).
  if ( !chunk["file"].isString() )
  {
    if ( skippedCorrupt )
      *skippedCorrupt = "entry with non-string file: token " + token.substr( 0, 8 );
    return {};
  }
  const std::string file = chunk["file"].asString();
  if ( !isSafeMirrorChunkFileName( file ) )
  {
    if ( skippedCorrupt )
      *skippedCorrupt = "unsafe chunk file name: " + file.substr( 0, 64 );
    return {};
  }
  const std::string fullPath = mirrorDirectory + "/" + kMirrorChunkDir + "/" + file;
  VSIStatBufL statBuffer;
  if ( VSIStatL( fullPath.c_str(), &statBuffer ) != 0 )
  {
    if ( skippedCorrupt )
      *skippedCorrupt = "chunk file missing: " + file;
    return {};
  }
  // Integrity is enforced HERE so every replay path (virtual_cube included)
  // receives a verified file, never a manifest-named path: the declared size
  // must match, and an 11.0 manifest (it carries the offline index) must
  // prove the payload with sha256 — absence is corrupt there. v1 manifests
  // predate the checksum and stay size-checked only.
  if ( chunk["bytes"].isUInt64() &&
       chunk["bytes"].asUInt64() != static_cast<Json::UInt64>( statBuffer.st_size ) )
  {
    if ( skippedCorrupt )
      *skippedCorrupt = "chunk size mismatch: " + file;
    return {};
  }
  if ( chunk["sha256"].isString() )
  {
    const std::string digest = fileSha256Hex( fullPath );
    if ( digest.empty() || digest != chunk["sha256"].asString() )
    {
      if ( skippedCorrupt )
        *skippedCorrupt = "chunk sha256 mismatch: " + file;
      return {};
    }
  }
  else if ( manifest.isMember( "index" ) )
  {
    if ( skippedCorrupt )
      *skippedCorrupt = "chunk entry without sha256: " + file;
    return {};
  }
  return fullPath;
}

std::string fabricMirrorIndexKey( const std::string &assetPath )
{
  // Object stores first: every spelling converges on the canonical key.
  const CanonicalObjectKey objectKey = canonicalObjectKey( assetPath );
  if ( objectKey.valid )
    return objectKey.canonical;
  // http(s): the credential-stripped identity URL (the same form token
  // bases use — a re-signed href maps to the SAME index entry). Anything
  // else (local files, VSI containers) canonicalizes through ResourceUri.
  return remoteIdentityUrlBasis( assetPath );
}

bool lookupMirrorAsset( const std::string &mirrorDirectory, const std::string &assetPath,
                        MirrorIndexAssetFacts &facts, std::string *skippedCorrupt )
{
  facts = MirrorIndexAssetFacts {};
  const Json::Value manifest = manifestSnapshot( mirrorDirectory );
  if ( manifest.isNull() || !manifest.isObject() )
  {
    if ( skippedCorrupt )
      *skippedCorrupt = "manifest unreadable";
    return false;
  }
  const Json::Value &index = manifest["index"];
  if ( !index.isObject() )
    return false;
  const Json::Value &entry = index[fabricMirrorIndexKey( assetPath )];
  if ( !entry.isObject() )
    return false;
  if ( !entry["token"].isString() )
  {
    if ( skippedCorrupt )
      *skippedCorrupt = "index entry without token: " + assetPath;
    return false;
  }
  facts.found = true;
  facts.token = entry["token"].asString();
  facts.assetId = entry["assetId"].isString() ? entry["assetId"].asString() : std::string();
  facts.writtenUtc = entry["writtenUtc"].isString() ? entry["writtenUtc"].asString() : std::string();
  const Json::Value &grid = entry["grid"];
  if ( grid.isObject() && grid["width"].isInt() && grid["height"].isInt() &&
       grid["geotransform"].isArray() && grid["geotransform"].size() == 6 )
  {
    const Json::Value &gt = grid["geotransform"];
    if ( !gt[0].isNumeric() || !gt[1].isNumeric() || !gt[3].isNumeric() || !gt[5].isNumeric() )
      return true;   // token found; the grid facts are corrupt — never coerced
    facts.hasGrid = true;
    facts.rasterWidth = grid["width"].asInt();
    facts.rasterHeight = grid["height"].asInt();
    facts.resX = gt[1].asDouble();
    facts.resY = gt[5].asDouble();
    facts.assetMinX = gt[0].asDouble();
    facts.assetMaxY = gt[3].asDouble();
    facts.assetMaxX = facts.assetMinX + facts.resX * facts.rasterWidth;
    facts.assetMinY = facts.assetMaxY + facts.resY * facts.rasterHeight;
    facts.epsgAuthid = grid["epsg"].isString() ? grid["epsg"].asString() : std::string();
  }
  return true;
}

MirrorArtifactHit resolveMirrorArtifact( const std::string &mirrorDirectory,
                                         const std::string &assetPath,
                                         const RasterWindow &sourceWindow,
                                         const std::string &bandSelector,
                                         std::uint64_t maxAgeSeconds )
{
  MirrorArtifactHit result;
  MirrorIndexAssetFacts facts;
  std::string skipped;
  if ( !lookupMirrorAsset( mirrorDirectory, assetPath, facts, &skipped ) )
  {
    result.skippedCorrupt = skipped;
    return result;
  }
  if ( !facts.found || facts.token.empty() )
    return result;   // unmirrored or unprovable asset — never a guess
  result.token = facts.token;

  // Expiry: the materialization stamp is the age basis. Unparseable stamps
  // never expire (absence is not evidence of staleness).
  if ( maxAgeSeconds > 0 && !facts.writtenUtc.empty() )
  {
    const InstantParse parsed = parseIso8601Instant( facts.writtenUtc );
    if ( parsed.ok )
    {
      const auto nowNanos =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::system_clock::now().time_since_epoch() ).count();
      const std::int64_t ageNanos = nowNanos - parsed.epochNanos;
      if ( ageNanos > 0 &&
           static_cast<std::uint64_t>( ageNanos ) / 1000000000ull > maxAgeSeconds )
      {
        result.expired = true;
        return result;
      }
    }
  }

  // The v2 chunk key uses the index key as its path basis; the 10.0 key
  // (raw path) is the fallback so v1 manifests keep resolving.
  const std::string indexKey = fabricMirrorIndexKey( assetPath );
  std::string key = fabricChunkMirrorKey( facts.token, indexKey, sourceWindow, bandSelector );
  std::string file = resolveMirrorHit( mirrorDirectory, facts.token, key, &skipped );
  if ( file.empty() )
  {
    const std::string legacyKey =
      fabricChunkMirrorKey( facts.token, assetPath, sourceWindow, bandSelector );
    if ( legacyKey != key )
      file = resolveMirrorHit( mirrorDirectory, facts.token, legacyKey, &skipped );
    if ( !file.empty() )
      key = legacyKey;
  }
  if ( file.empty() )
  {
    result.skippedCorrupt = skipped;
    return result;
  }

  // resolveMirrorHit already enforced the integrity contract (declared
  // size + sha256 proof on 11.0 manifests) before returning the path; only
  // the byte count is needed here.
  VSIStatBufL statBuffer;
  if ( VSIStatL( file.c_str(), &statBuffer ) != 0 )
    return result;

  result.hit = true;
  result.chunkKey = key;
  result.file = file;
  result.bytes = static_cast<std::uint64_t>( statBuffer.st_size );
  return result;
}

Json::Value MirrorVerifyReport::toJson() const
{
  Json::Value json;
  json["entries_checked"] = static_cast<Json::UInt64>( entriesChecked );
  json["ok"] = static_cast<Json::UInt64>( ok );
  json["missing_files"] = static_cast<Json::UInt64>( missingFiles );
  json["size_mismatches"] = static_cast<Json::UInt64>( sizeMismatches );
  json["checksum_mismatches"] = static_cast<Json::UInt64>( checksumMismatches );
  json["bad_entries"] = static_cast<Json::UInt64>( badEntries );
  json["unreferenced_files"] = static_cast<Json::UInt64>( unreferencedFiles );
  json["unreferenced_bytes"] = static_cast<Json::UInt64>( unreferencedBytes );
  json["unclassified_entries"] = static_cast<Json::UInt64>( unclassifiedEntries );
  json["bytes_checked"] = static_cast<Json::UInt64>( bytesChecked );
  json["manifest_unreadable"] = manifestUnreadable;
  return json;
}

namespace
{

/// The verdict of one manifest chunk entry (shared by verifyMirror and the
/// repair cleanup so both apply EXACTLY the same contract).
enum class ChunkEntryVerdict
{
  Ok,
  BadEntry,           ///< wrong-typed / unsafe name / missing sha256 proof
  MissingFile,
  SizeMismatch,
  ChecksumMismatch,
};

ChunkEntryVerdict verifyChunkEntry( const Json::Value &manifest, const std::string &mirrorDirectory,
                                    const std::string &file, bool hasDeclaredBytes,
                                    std::uint64_t declaredBytes, bool hasSha256,
                                    const std::string &sha256, std::uint64_t *bytesOut )
{
  if ( !isSafeMirrorChunkFileName( file ) )
    return ChunkEntryVerdict::BadEntry;
  const std::string fullPath = mirrorDirectory + "/" + kMirrorChunkDir + "/" + file;
  VSIStatBufL statBuffer;
  if ( VSIStatL( fullPath.c_str(), &statBuffer ) != 0 )
    return ChunkEntryVerdict::MissingFile;
  if ( hasDeclaredBytes && declaredBytes != static_cast<Json::UInt64>( statBuffer.st_size ) )
    return ChunkEntryVerdict::SizeMismatch;
  if ( hasSha256 )
  {
    const std::string digest = fileSha256Hex( fullPath );
    if ( digest.empty() || digest != sha256 )
      return ChunkEntryVerdict::ChecksumMismatch;
  }
  else if ( manifest.isMember( "index" ) )
  {
    // An 11.0 manifest (it carries the offline index) demands the proof;
    // absence is corrupt, exactly like the replay-read contract.
    return ChunkEntryVerdict::BadEntry;
  }
  if ( bytesOut != nullptr )
    *bytesOut = static_cast<std::uint64_t>( statBuffer.st_size );
  return ChunkEntryVerdict::Ok;
}

} // namespace

MirrorVerifyReport verifyMirror( const std::string &mirrorDirectory )
{
  MirrorVerifyReport report;
  const Json::Value manifest = manifestSnapshot( mirrorDirectory );
  if ( manifest.isNull() || !manifest.isObject() )
  {
    report.manifestUnreadable = true;
    return report;
  }

  // The referenced chunk file names — the orphan scan at the end compares
  // the directory listing against this set.
  std::set<std::string> referenced;

  for ( const std::string &token : manifest.getMemberNames() )
  {
    if ( token == "index" )
      continue;   // the offline index is metadata, not chunk entries
    const Json::Value &chunks = manifest[token];
    if ( !chunks.isObject() )
      continue;
    for ( const std::string &chunkKey : chunks.getMemberNames() )
    {
      const Json::Value &chunk = chunks[chunkKey];
      if ( !chunk.isObject() )
        continue;
      report.entriesChecked += 1;
      const std::string file = chunk["file"].isString() ? chunk["file"].asString() : std::string();
      // Referenced = the manifest NAMES the file (name-based, independent of
      // the verdict): a corrupt-but-present file is not an orphan.
      if ( !file.empty() )
        referenced.insert( file );
      std::uint64_t entryBytes = 0;
      const ChunkEntryVerdict verdict = verifyChunkEntry(
        manifest, mirrorDirectory, file, chunk["bytes"].isUInt64(),
        chunk["bytes"].isUInt64() ? chunk["bytes"].asUInt64() : 0, chunk["sha256"].isString(),
        chunk["sha256"].isString() ? chunk["sha256"].asString() : std::string(), &entryBytes );
      switch ( verdict )
      {
        case ChunkEntryVerdict::Ok:
          report.bytesChecked += entryBytes;
          report.ok += 1;
          break;
        case ChunkEntryVerdict::BadEntry: report.badEntries += 1; break;
        case ChunkEntryVerdict::MissingFile: report.missingFiles += 1; break;
        case ChunkEntryVerdict::SizeMismatch: report.sizeMismatches += 1; break;
        case ChunkEntryVerdict::ChecksumMismatch: report.checksumMismatches += 1; break;
      }
    }
  }

  // Orphan scan: every file in chunks/ the manifest never names. Bounded
  // work: one listing pass, stat per orphan (no reads).
  // 13.0: the iterator spells the dir through atomic_fs's UTF-8 path
  // discipline (a non-ASCII mirror root survives on Windows), and each
  // entry is classified by symlink_status — a symlink/reparse point is
  // never a chunk payload and is never followed.
  const std::string chunkDir = mirrorDirectory + "/" + kMirrorChunkDir;
  std::error_code ec;
  for ( std::filesystem::directory_iterator it( std::filesystem::u8path( chunkDir ), ec ), end;
        !ec && it != end; it.increment( ec ) )
  {
    // A fresh error_code per probe: one transient stat failure (a racing
    // directory change) must not abort the whole scan silently — but it
    // IS counted: the audit admits what it could not classify.
    std::error_code statusEc;
    const std::filesystem::file_status status = it->symlink_status( statusEc );
    if ( statusEc || std::filesystem::is_symlink( status ) )
    {
      ++report.unclassifiedEntries;
      continue;
    }
    if ( !std::filesystem::is_regular_file( status ) )
      continue;
    // The name must convert to the manifest's UTF-8 domain before it can
    // be compared to the referenced set — an unconvertible name cannot be
    // proven unreferenced (counted, not reported as an orphan).
    std::string name;
    if ( !utf8FileName( *it, name ) )
    {
      ++report.unclassifiedEntries;
      continue;
    }
    if ( referenced.count( name ) > 0 )
      continue;
    std::error_code sizeEc;
    const auto size = std::filesystem::file_size( it->path(), sizeEc );
    report.unreferencedBytes += sizeEc ? 0 : static_cast<std::uint64_t>( size );
    report.unreferencedFiles += 1;
  }
  // A mid-scan increment error truncates the inventory: the tail held at
  // least one uninspected entry, and the audit admits that rather than
  // reporting a clean sweep it never completed.
  if ( ec )
    ++report.unclassifiedEntries;
  return report;
}

namespace
{

/// The repair cleanup phase (12.0): re-verify every entry against the
/// CURRENT manifest, unlink + drop the broken ones, publish atomically.
/// Re-deriving the verdicts here (instead of replaying the caller's verify
/// report) means a manifest rewritten between verify and cleanup can never
/// make the repair drop a healthy entry.
struct RepairCleanupResult
{
  MirrorVerifyReport verify;
  std::uint64_t removedBadEntries = 0;
  bool refused = false;
};

RepairCleanupResult repairCleanup( const std::string &mirrorDirectory )
{
  RepairCleanupResult result;
  result.verify = verifyMirror( mirrorDirectory );
  if ( result.verify.manifestUnreadable )
  {
    result.refused = true;
    return result;
  }
  const bool badState = result.verify.missingFiles > 0 || result.verify.sizeMismatches > 0 ||
                        result.verify.checksumMismatches > 0 || result.verify.badEntries > 0;
  if ( !badState )
    return result;   // nothing to clean; the re-materialization no-ops

  MirrorWriterLock lock( mirrorDirectory );
  if ( !lock.held() )
    throw GeoError( ErrorCode::PermissionDenied,
                    "another writer holds this mirror directory (single-writer contract); a leftover 'writer.lock' from a crashed writer is broken automatically by pid liveness or age" );
  // Read the manifest UNDER the lock (P0 review fix): reading before the
  // lock would race a materialization pass that publishes new entries while
  // we wait — this cleanup would then drop their fresh chunk files as
  // "broken" and publish the stale view back over them.
  Json::Value manifest = readManifest( mirrorDirectory );
  if ( !manifest.isObject() )
  {
    // Raced with a corrupting writer between verify and here: refuse.
    result.refused = true;
    result.verify = MirrorVerifyReport {};
    result.verify.manifestUnreadable = true;
    return result;
  }

  bool dropped = false;
  for ( const std::string &token : manifest.getMemberNames() )
  {
    if ( token == "index" )
      continue;
    Json::Value &chunks = manifest[token];
    if ( !chunks.isObject() )
      continue;
    std::vector<std::string> dropKeys;
    for ( const std::string &chunkKey : chunks.getMemberNames() )
    {
      const Json::Value &chunk = chunks[chunkKey];
      if ( !chunk.isObject() )
      {
        dropKeys.push_back( chunkKey );   // wrong-typed slot
        continue;
      }
      const std::string file = chunk["file"].isString() ? chunk["file"].asString() : std::string();
      const ChunkEntryVerdict verdict = verifyChunkEntry(
        manifest, mirrorDirectory, file, chunk["bytes"].isUInt64(),
        chunk["bytes"].isUInt64() ? chunk["bytes"].asUInt64() : 0, chunk["sha256"].isString(),
        chunk["sha256"].isString() ? chunk["sha256"].asString() : std::string(), nullptr );
      if ( verdict == ChunkEntryVerdict::Ok )
        continue;
      dropKeys.push_back( chunkKey );
      // Unlink the file when the manifest provably names a safe path for
      // it (a missing file needs no unlink; an unsafe name is never a path
      // we touch).
      if ( !file.empty() && isSafeMirrorChunkFileName( file ) &&
           verdict != ChunkEntryVerdict::MissingFile )
        atomic_fs::removeFileQuiet( mirrorDirectory + "/" + kMirrorChunkDir + "/" + file );
    }
    for ( const std::string &key : dropKeys )
    {
      chunks.removeMember( key );
      ++result.removedBadEntries;
      dropped = true;
    }
  }
  if ( dropped )
  {
    atomic_fs::writeFileAtomic( mirrorDirectory + "/" + kMirrorManifest,
                                [ & ]( const std::string &staged ) {
      const std::string text = Json::writeString( Json::StreamWriterBuilder(), manifest );
      // The staged path is UTF-8 — open it as such, not through the
      // Windows ANSI code page (a non-ASCII mirror root would fail here).
      std::ofstream out( std::filesystem::u8path( staged ), std::ios::binary | std::ios::trunc );
      if ( !out )
        throw GeoError( ErrorCode::IoError, "mirror manifest: cannot create " + staged );
      out.write( text.data(), static_cast<std::streamsize>( text.size() ) );
      out.flush();
      if ( !out )
        throw GeoError( ErrorCode::IoError, "mirror manifest: write failed for " + staged );
    } );
    invalidateManifestSnapshot( mirrorDirectory );
  }
  return result;
}

} // namespace

Json::Value mirrorStatsJson( const std::string &mirrorDirectory )
{
  Json::Value stats;
  const Json::Value manifest = manifestSnapshot( mirrorDirectory );
  std::uint64_t entries = 0, bytes = 0, indexedAssets = 0;
  if ( manifest.isObject() )
  {
    for ( const auto &token : manifest.getMemberNames() )
    {
      // 11.0: the offline index is metadata, not a token's chunk map.
      if ( token == "index" )
      {
        const Json::Value &index = manifest["index"];
        indexedAssets = index.isObject() ? index.size() : 0;
        continue;
      }
      const Json::Value &chunks = manifest[token];
      if ( !chunks.isObject() )
        continue;
      entries += chunks.size();
      for ( const auto &key : chunks.getMemberNames() )
      {
        const Json::Value &chunk = chunks[key];
        if ( chunk.isObject() && chunk["bytes"].isUInt64() )
          bytes += chunk["bytes"].asUInt64();
      }
    }
  }
  stats["indexedAssets"] = static_cast<Json::UInt64>( indexedAssets );
  stats["entries"] = static_cast<Json::UInt64>( entries );
  stats["bytes"] = static_cast<Json::UInt64>( bytes );
  stats["directory"] = ResourceUri::parse( mirrorDirectory ).display();
  return stats;
}

namespace
{

/// The shared chunk-walk behind mirrorChunks (the FabricPlan overload
/// forwards here). Materializes each chunk's SOURCE window for its hinted
/// asset through the normal read path, atomically published.
MirrorReport mirrorChunksImpl( const VirtualCube &cube, const CubeChunkPlan &plan,
                               const MirrorOptions &options, const CancelToken &cancel )
{
  MirrorReport report;
  const std::string &mirrorDirectory = options.mirrorDirectory;
  if ( mirrorDirectory.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "mirror needs a directory" );
  VSIMkdir( mirrorDirectory.c_str(), 0755 );   // exists-or-exists-quietly
  VSIMkdir( ( mirrorDirectory + "/" + kMirrorChunkDir ).c_str(), 0755 );
  VSIStatBufL statBuffer;
  if ( VSIStatL( ( mirrorDirectory + "/" + kMirrorChunkDir ).c_str(), &statBuffer ) != 0 )
    throw GeoError( ErrorCode::IoError, "cannot create mirror directory: " + mirrorDirectory );

  MirrorWriterLock lock( mirrorDirectory );
  if ( !lock.held() )
    throw GeoError( ErrorCode::PermissionDenied,
                    "another writer holds this mirror directory (single-writer contract); a leftover 'writer.lock' from a crashed writer is broken automatically by pid liveness or age" );

  Json::Value manifest = readManifest( mirrorDirectory );
  if ( !manifest.isObject() )
    manifest = Json::Value( Json::objectValue );
  // A foreign-typed "index" member would make every later manifest[token]
  // write land on a non-object (jsoncpp's operator[] on an array is UB):
  // repair it to the empty object it must be.
  if ( !manifest["index"].isObject() )
    manifest["index"] = Json::Value( Json::objectValue );

  const std::uint64_t total = plan.chunkCountTotal();
  // maxBytes==0 means the DECLARED default bound: the chunk plan's own byte
  // estimate — never unbounded (mirror.h contract).
  const std::uint64_t budget = [ & ] {
    if ( options.maxBytes > 0 )
      return options.maxBytes;
    if ( total == 0 )
      return std::uint64_t { 0 };
    const std::vector<CubeChunkRequest> first = plan.materializeChunks( 0, 1 );
    return ( first.empty() ? 0 : first.front().estimatedBytes ) * total;
  }();
  std::uint64_t bytesWritten = 0;       ///< real file bytes (report truth)
  std::uint64_t logicalWritten = 0;     ///< declared chunk estimates (budget basis)
  std::size_t pendingManifestWrites = 0;
  // Identity tokens are cached per walk (a local token hashes ≤ 8 MiB —
  // once per asset, never once per chunk).
  std::map<std::string, std::string> tokenCache;
  // 11.0 (D-1008): per-chunk outcomes are retained up to a bounded window
  // and counted past it — a million-chunk pass reports counters, not a
  // million-element vector.
  const std::size_t kMaxRetainedOutcomes = 1024;
  auto pushOutcome = [ &report ]( MirrorChunkOutcome &&outcome ) {
    if ( report.chunks.size() < kMaxRetainedOutcomes )
      report.chunks.push_back( std::move( outcome ) );
    else
      ++report.outcomesDropped;
  };
  // The v2 manifest flush: throttled during the walk (every 32 mirrored
  // chunks), ALWAYS flushed at the end — a crashed pass still publishes
  // every chunk whose manifest write already happened, plus its own final
  // flush attempt (atomic publish keeps the file consistent throughout).
  auto flushManifest = [ & ]( Json::Value &manifest, bool force ) {
    if ( pendingManifestWrites == 0 && !force )
      return;
    if ( !force && pendingManifestWrites < 32 )
      return;
    atomic_fs::writeFileAtomic( mirrorDirectory + "/" + kMirrorManifest,
                                [ & ]( const std::string &staged ) {
      const std::string text = Json::writeString( Json::StreamWriterBuilder(), manifest );
      // The staged path is UTF-8 — open it as such, not through the
      // Windows ANSI code page (a non-ASCII mirror root would fail here).
      std::ofstream out( std::filesystem::u8path( staged ), std::ios::binary | std::ios::trunc );
      if ( !out )
        throw GeoError( ErrorCode::IoError, "mirror manifest: cannot create " + staged );
      out.write( text.data(), static_cast<std::streamsize>( text.size() ) );
      out.flush();
      if ( !out )
        throw GeoError( ErrorCode::IoError, "mirror manifest: write failed for " + staged );
    } );
    invalidateManifestSnapshot( mirrorDirectory );
    pendingManifestWrites = 0;
  };
  // Review R13 P2: the final flush must survive mid-walk exceptions — a
  // throw (unreadable source, write failure) would otherwise abandon up to
  // 31 pending entries. The guard publishes whatever was written; a flush
  // failure during unwinding is swallowed (the atomic publish keeps the
  // file consistent either way).
  struct FinalFlushGuard
  {
    std::function<void()> flush;
    ~FinalFlushGuard() { try { flush(); } catch ( ... ) {} }
  } finalFlushGuard{ [ & ] { flushManifest( manifest, /*force=*/true ); } };

  std::size_t chunkWindow = options.chunkWindow == 0 ? 64 : options.chunkWindow;
  // Asset hints resolve through one map built per walk (same shape as the
  // prefetch/query-planner byId maps) — a linear cube.assets() scan per
  // chunk is O(chunks × assets) and dwarfs the chunk work on wide plans.
  std::unordered_map<std::string, const VirtualCubeAssetIndexEntry *> assetsById;
  assetsById.reserve( cube.assetCount() );
  for ( const VirtualCubeAssetIndexEntry &entry : cube.assets() )
    assetsById.emplace( entry.record.id, &entry );

  for ( std::uint64_t begin = 0; begin < total; begin += chunkWindow )
  {
    if ( cancel.cancelled() )
      break;
    // Budget basis (11.0, unified): DECLARED chunk estimates — the same
    // unit the budget is declared in and the per-chunk gate below uses.
    // Real file bytes stay report-only (bytesWritten).
    if ( logicalWritten >= budget && budget > 0 )
    {
      report.budgetStopped = true;
      break;
    }
    const std::vector<CubeChunkRequest> requests =
      plan.materializeChunks( begin, std::min<std::size_t>( chunkWindow, 4096 ) );
    for ( const CubeChunkRequest &request : requests )
    {
      if ( cancel.cancelled() )
        break;

      MirrorChunkOutcome outcome;
      outcome.index = request.index;
      outcome.assetIdHint = request.assetIdHint;

      // Resolve the hinted asset (the FirstWins owner of this time step).
      const VirtualCubeAssetIndexEntry *asset = nullptr;
      {
        const auto found = assetsById.find( request.assetIdHint );
        if ( found != assetsById.end() )
          asset = found->second;
      }
      if ( asset == nullptr )
      {
        outcome.status = "failed";
        outcome.errorText = "chunk has no asset hint (empty time dim?)";
        ++report.failed;
        pushOutcome( std::move( outcome ) );
        continue;
      }
      std::string token = asset->identityToken;
      if ( token.empty() )
      {
        const auto cached = tokenCache.find( asset->record.id );
        if ( cached != tokenCache.end() )
          token = cached->second;
        else
        {
          // Ask the identity authority (fail-closed: unprovable stays
          // unmirrored — D-1010).
          token = fabricAssetIdentity( asset->record.path, AssetIdentityOptions{} ).token;
          tokenCache[asset->record.id] = token;
        }
        if ( token.empty() )
        {
          outcome.status = "skipped-unprovable-identity";
          ++report.skippedUnprovable;
          pushOutcome( std::move( outcome ) );
          continue;
        }
      }
      outcome.token = token;

      // The chunk's source window on its asset: map the chunk's grid extent
      // through the asset's own geotransform (same math as the read path).
      if ( !request.hasExtent )
      {
        outcome.status = "failed";
        outcome.errorText = "chunk carries no extent";
        ++report.failed;
        pushOutcome( std::move( outcome ) );
        continue;
      }
      // Per-chunk isolation (review R13 class): one unreadable asset or
      // write failure is a RECORDED chunk outcome, never an aborted pass —
      // the report and every remaining chunk survive (prefetch's pattern).
      try
      {
        RasterReader reader = RasterReader::open( fabricCachedPath( asset->record.path ) );
        const RasterMetadata &metadata = reader.metadata();
        if ( metadata.bands.empty() )
          throw GeoError( ErrorCode::InvalidMetadata,
                          "mirrored asset declares no readable band metadata" );
        // THE shared mapping rule (sign-safe, clamped) — no inline re-derivation.
        const VirtualCubeSourceWindow mapped = virtualCubeSourceWindow(
          metadata, request.minX, request.minY, request.maxX, request.maxY );
        if ( !mapped.ok )
        {
          outcome.status = "failed";
          outcome.errorText = "chunk extent misses the asset's raster";
          ++report.failed;
          pushOutcome( std::move( outcome ) );
          continue;
        }
        const RasterWindow sourceWindow = mapped.window;
        // 11.0 (D-1103): the chunk key's path component is the CREDENTIAL-
        // FREE index key (spelling-stable; a re-signed href of the same
        // object maps to the same chunk key). The offline index entry is
        // written/refreshed for every resolved asset so a later process can
        // replay without any network probe.
        const std::string indexKey = fabricMirrorIndexKey( asset->record.path );
        const std::string key =
          fabricChunkMirrorKey( token, indexKey, sourceWindow, "band1" );
        outcome.chunkKey = key;
        {
          Json::Value &assetIndex = manifest["index"][indexKey];
          if ( !assetIndex.isObject() )
            assetIndex = Json::Value( Json::objectValue );
          if ( !assetIndex["token"].isString() || assetIndex["token"].asString() != token )
          {
            assetIndex["token"] = token;
            assetIndex["assetId"] = asset->record.id;
            const std::array<double, 6> &gt = metadata.geotransform;
            assetIndex["grid"]["width"] = metadata.width;
            assetIndex["grid"]["height"] = metadata.height;
            Json::Value gtJson( Json::arrayValue );
            for ( int i = 0; i < 6; ++i )
              gtJson.append( gt[i] );
            assetIndex["grid"]["geotransform"] = gtJson;
            assetIndex["grid"]["epsg"] = metadata.crs.authid;
            assetIndex["writtenUtc"] = nowIso8601Utc();
            pendingManifestWrites++;
          }
        }

        // Already mirrored? A token-keyed hit is the proof — skip the fetch.
        // The in-memory manifest answers (the walk re-reads nothing; quadratic
        // manifest I/O would sink 10k-chunk passes).
        std::string existing;
        {
          Json::Value &tokenEntry = manifest[token];
          if ( !tokenEntry.isObject() )
            tokenEntry = Json::Value( Json::objectValue );
          if ( tokenEntry[key].isObject() && tokenEntry[key]["file"].isString() )
          {
            const std::string candidateName = tokenEntry[key]["file"].asString();
            if ( !isSafeMirrorChunkFileName( candidateName ) )
              tokenEntry.removeMember( key );   // hostile join: force a rewrite
            else
            {
              const std::string candidate =
                mirrorDirectory + "/" + kMirrorChunkDir + "/" + candidateName;
              VSIStatBufL existingStat;
              if ( VSIStatL( candidate.c_str(), &existingStat ) == 0 )
                existing = candidate;
            }
          }
        }
        if ( !existing.empty() )
        {
          outcome.status = "already-present";
          outcome.file = existing;
          ++report.alreadyPresent;
          pushOutcome( std::move( outcome ) );
          continue;
        }

        // The budget counts DECLARED chunk estimates (logical bytes); the
        // report's bytesWritten stays the real file size (GeoTIFF containers
        // are larger than their payload — comparing file bytes to a logical
        // estimate would skip most of the pass).
        if ( logicalWritten >= budget && budget > 0 )
        {
          outcome.status = "skipped-budget";
          ++report.skippedBudget;
          pushOutcome( std::move( outcome ) );
          report.budgetStopped = true;
          continue;
        }

        // Read the source window through the normal path (range cache intact)
        // and publish it as a plain GTiff chunk.
        const std::vector<double> values =
          reader.readWindow( { 1 }, sourceWindow, options.maxChunkReadBytes );
        const std::string target = mirrorDirectory + "/" + kMirrorChunkDir + "/" + key + ".tif";
        std::uint64_t written = 0;
        atomic_fs::writeFileAtomic( target, [ & ]( const std::string &stagedPath ) {
          // The chunk carries the SOURCE band's NoData declaration (review
          // R13): offline replay's NoData semantics then match the online
          // read exactly — declared-NoData source pixels lose FirstWins the
          // same way they do online.
          RasterBandSpec chunkBand;
          chunkBand.dtype = metadata.bands[0].dtype;
          chunkBand.description = metadata.bands[0].description;
          chunkBand.hasNoData = metadata.bands[0].hasNoData;
          chunkBand.noDataValue = metadata.bands[0].noDataValue;
          chunkBand.noDataIsNaN = metadata.bands[0].noDataIsNaN;
          RasterWriter writer = RasterWriter::create(
            stagedPath, sourceWindow.width, sourceWindow.height, { chunkBand },
            { "GTiff", { "TILED=YES", "BLOCKXSIZE=64", "BLOCKYSIZE=64" }, true } );
          // Real georeferencing: the chunk sits at its source extent in the
          // asset's own grid (a plain pixel grid would make mirrored chunks
          // un-geolocatable to direct consumers).
          const std::array<double, 6> &gt = metadata.geotransform;
          writer.setGeotransform( { request.minX, gt[1], 0.0, request.maxY, 0.0, gt[5] } );
          writer.writeWindow( 1, { 0, 0, sourceWindow.width, sourceWindow.height }, values.data() );
          writer.finalize();
          std::error_code ec;
          const auto size = std::filesystem::file_size( stagedPath, ec );
          if ( ec )
            throw GeoError( ErrorCode::IoError,
                            "mirror: cannot size staged chunk " + stagedPath + ": " + ec.message() );
          written = static_cast<std::uint64_t>( size );
        } );

        // The chunk's entry is assembled in a local and attached to the
        // token's map in ONE move AFTER the payload proof: an in-place
        // partial write here used to leave a sha256-less entry behind when
        // the hash threw — replay would then (correctly) treat the chunk as
        // corrupt while the already-present check kept any later pass from
        // repairing it. Still O(1) per chunk — the quadratic cost was
        // copying the token's whole map per chunk, not building one entry.
        Json::Value chunk;
        chunk["file"] = key + ".tif";
        chunk["bytes"] = static_cast<Json::UInt64>( written );
        chunk["assetId"] = asset->record.id;
        chunk["window"] = [ & ] {
          Json::Value w( Json::arrayValue );
          w.append( sourceWindow.xOff );
          w.append( sourceWindow.yOff );
          w.append( sourceWindow.width );
          w.append( sourceWindow.height );
          return w;
        }();
        chunk["timeUtc"] = request.timeUtc;
        // 11.0 integrity checksum: replay can demand a payload proof before
        // serving (size checks are cheap; this is the strong form).
        chunk["sha256"] = fileSha256Hex( target );
        // 12.0 materialization stamp (additive): prune's age basis. Entries
        // written before 12.0 lack it and simply never age-expire.
        chunk["writtenUtc"] = nowIso8601Utc();
        Json::Value &entry = manifest[token];   // attach in place — never copy
        // the token's whole chunk map out and back in per chunk (that was
        // O(chunks²) of JSON node copies on one-asset mirrors).
        entry[key] = std::move( chunk );

        // Throttled flush (11.0): the manifest publishes after every 32nd
        // mirrored chunk and unconditionally at the end of the walk.
        pendingManifestWrites++;
        flushManifest( manifest, /*force=*/false );

        outcome.status = "mirrored";
        outcome.file = target;
        outcome.bytes = written;
        bytesWritten += written;
        logicalWritten += request.estimatedBytes;
        ++report.mirrored;
        report.bytesWritten += written;
        pushOutcome( std::move( outcome ) );
      }
      catch ( const GeoError &error )
      {
        outcome.status = "failed";
        outcome.errorText = std::string( error.what() ).substr( 0, 512 );
        ++report.failed;
        pushOutcome( std::move( outcome ) );
      }
    }
  }
  report.cancelled = cancel.cancelled();
  // Final flush is UNCONDITIONAL: a pass that ended on cancel/budget still
  // publishes every chunk it actually wrote (manifest entries without the
  // final flush would point at valid chunk files no reader can find).
  flushManifest( manifest, /*force=*/true );
  return report;
}

} // namespace

MirrorReport mirrorChunks( const FabricPlan &plan, const MirrorOptions &options,
                           const CancelToken &cancel )
{
  // Rebuild the cube from the plan's selected assets + declared grid — the
  // grid is explicit here, so no probe happens.
  const VirtualCube cube =
    VirtualCube::build( plan.selectedAssets(), plan.grid(), OverlapPolicy::FirstWins, {} );
  return mirrorChunksImpl( cube, plan.chunkPlan(), options, cancel );
}

MirrorReport mirrorChunks( const VirtualCube &cube, const CubeChunkPlan &plan,
                           const MirrorOptions &options, const CancelToken &cancel )
{
  return mirrorChunksImpl( cube, plan, options, cancel );
}

Json::Value MirrorRepairReport::toJson() const
{
  Json::Value json = before.toJson();
  json["removed_bad_entries"] = static_cast<Json::UInt64>( removedBadEntries );
  json["remirror"] = remirror.toJson();
  json["manifest_unreadable"] = manifestUnreadable;
  return json;
}

MirrorRepairReport repairMirror( const VirtualCube &cube, const CubeChunkPlan &plan,
                                 const MirrorOptions &options, const CancelToken &cancel )
{
  // Expected cost: ~2× the mirror's bytes hashed (the audit verify + the
  // locked re-verification before dropping) — maintenance-grade, not a
  // per-read path.
  MirrorRepairReport report;
  const std::string &mirrorDirectory = options.mirrorDirectory;
  if ( mirrorDirectory.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "mirror repair needs a directory" );
  const RepairCleanupResult cleanup = repairCleanup( mirrorDirectory );
  report.before = cleanup.verify;
  report.removedBadEntries = cleanup.removedBadEntries;
  report.manifestUnreadable = cleanup.refused;
  if ( cleanup.refused )
    return report;   // refuse: never re-materialize against an unreadable manifest
  report.remirror = mirrorChunksImpl( cube, plan, options, cancel );
  return report;
}

MirrorRepairReport repairMirror( const FabricPlan &plan, const MirrorOptions &options,
                                 const CancelToken &cancel )
{
  const VirtualCube cube =
    VirtualCube::build( plan.selectedAssets(), plan.grid(), OverlapPolicy::FirstWins, {} );
  return repairMirror( cube, plan.chunkPlan(), options, cancel );
}

Json::Value MirrorPruneReport::toJson() const
{
  Json::Value json;
  json["manifest_unreadable"] = manifestUnreadable;
  json["orphan_files_removed"] = static_cast<Json::UInt64>( orphanFilesRemoved );
  json["orphan_files_refused"] = static_cast<Json::UInt64>( orphanFilesRefused );
  json["orphan_files_failed"] = static_cast<Json::UInt64>( orphanFilesFailed );
  json["dead_entries_removed"] = static_cast<Json::UInt64>( deadEntriesRemoved );
  json["expired_entries_removed"] = static_cast<Json::UInt64>( expiredEntriesRemoved );
  json["quota_entries_removed"] = static_cast<Json::UInt64>( quotaEntriesRemoved );
  json["kept_entries"] = static_cast<Json::UInt64>( keptEntries );
  json["bytes_removed"] = static_cast<Json::UInt64>( bytesRemoved );
  return json;
}

MirrorPruneReport pruneMirror( const std::string &mirrorDirectory,
                               const MirrorPruneOptions &options )
{
  MirrorPruneReport report;
  MirrorWriterLock lock( mirrorDirectory );
  if ( !lock.held() )
    throw GeoError( ErrorCode::PermissionDenied,
                    "another writer holds this mirror directory (single-writer contract); a leftover 'writer.lock' from a crashed writer is broken automatically by pid liveness or age" );
  // Read the manifest UNDER the lock (P0 review fix): a pre-lock read races
  // a materialization pass publishing new entries while we wait for the
  // lock — this pass would delete their fresh chunk files as "orphans" and
  // publish the stale manifest back over them (silent data loss).
  Json::Value manifest = readManifest( mirrorDirectory );
  if ( !manifest.isObject() )
  {
    // readManifest answers NULL for an UNPARSEABLE/oversized manifest: a
    // refusal, because garbage collection against a manifest it failed to
    // parse would delete payloads it never saw. (A MISSING manifest parses
    // to the empty object — an empty mirror, where every chunk file is
    // orphan and the pass below cleans it.)
    report.manifestUnreadable = true;
    return report;
  }

  const auto nowNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::system_clock::now().time_since_epoch() ).count();

  // One entry record per kept/broken chunk — bounded by the manifest.
  struct EntryRef
  {
    std::string token;
    std::string key;
    std::uint64_t bytes = 0;
    std::int64_t writtenNanos = 0;   // < 0 when unstamped (never ages)
  };
  std::vector<EntryRef> kept;

  for ( const std::string &token : manifest.getMemberNames() )
  {
    if ( token == "index" )
      continue;
    Json::Value &chunks = manifest[token];
    if ( !chunks.isObject() )
      continue;
    std::vector<std::string> dropKeys;
    for ( const std::string &chunkKey : chunks.getMemberNames() )
    {
      const Json::Value &chunk = chunks[chunkKey];
      if ( !chunk.isObject() || !chunk["file"].isString() )
        continue;   // wrong-typed slots belong to repair, not prune
      const std::string file = chunk["file"].asString();
      if ( !isSafeMirrorChunkFileName( file ) )
        continue;
      const std::string fullPath = mirrorDirectory + "/" + kMirrorChunkDir + "/" + file;
      VSIStatBufL statBuffer;
      const bool exists = VSIStatL( fullPath.c_str(), &statBuffer ) == 0;
      if ( !exists )
      {
        ++report.deadEntriesRemoved;
        dropKeys.push_back( chunkKey );
        continue;
      }
      const std::uint64_t entryBytes = static_cast<std::uint64_t>( statBuffer.st_size );
      // The materialization stamp is ALWAYS parsed (quota eviction needs the
      // ordering too); only the expiry DECISION depends on maxAgeSeconds.
      // Unstamped or unparseable entries never age and are un-evictable by
      // quota (absence is not evidence of staleness).
      std::int64_t writtenNanos = -1;
      if ( chunk["writtenUtc"].isString() )
      {
        const InstantParse parsed = parseIso8601Instant( chunk["writtenUtc"].asString() );
        if ( parsed.ok )
        {
          writtenNanos = parsed.epochNanos;
          if ( options.maxAgeSeconds > 0 )
          {
            const std::uint64_t ageSeconds = ( nowNanos > parsed.epochNanos )
              ? static_cast<std::uint64_t>( nowNanos - parsed.epochNanos ) / 1000000000ull
              : 0;
            if ( ageSeconds > options.maxAgeSeconds )
            {
              atomic_fs::removeFileQuiet( fullPath );
              ++report.expiredEntriesRemoved;
              report.bytesRemoved += entryBytes;
              dropKeys.push_back( chunkKey );
              continue;
            }
          }
        }
      }
      EntryRef ref;
      ref.token = token;
      ref.key = chunkKey;
      ref.bytes = entryBytes;
      ref.writtenNanos = writtenNanos;
      kept.push_back( ref );
    }
    for ( const std::string &key : dropKeys )
      chunks.removeMember( key );
  }

  // Quota: evict the OLDEST-stamped kept entries until the total fits.
  if ( options.maxBytes > 0 )
  {
    std::uint64_t total = 0;
    for ( const EntryRef &ref : kept )
      total += ref.bytes;
    std::vector<EntryRef *> evictable;
    for ( EntryRef &ref : kept )
      if ( ref.writtenNanos >= 0 )
        evictable.push_back( &ref );
    std::sort( evictable.begin(), evictable.end(),
               []( const EntryRef *a, const EntryRef *b ) {
                 return a->writtenNanos < b->writtenNanos;
               } );
    for ( EntryRef *ref : evictable )
    {
      if ( total <= options.maxBytes )
        break;
      Json::Value &chunks = manifest[ref->token];
      if ( !chunks.isObject() || !chunks.isMember( ref->key ) )
        continue;   // already dropped this pass
      atomic_fs::removeFileQuiet( mirrorDirectory + "/" + kMirrorChunkDir + "/" +
                                  chunks[ref->key]["file"].asString() );
      chunks.removeMember( ref->key );
      total -= ref->bytes;
      report.bytesRemoved += ref->bytes;
      ++report.quotaEntriesRemoved;
    }
  }

  for ( const std::string &token : manifest.getMemberNames() )
  {
    if ( token == "index" )
      continue;
    const Json::Value &chunks = manifest[token];
    if ( chunks.isObject() )
      report.keptEntries += chunks.size();
  }

  // Orphan scan AFTER entry pruning: dropped entries' files are unlinked,
  // so anything left in chunks/ that no kept entry names is garbage.
  std::set<std::string> referenced;
  for ( const std::string &token : manifest.getMemberNames() )
  {
    if ( token == "index" )
      continue;
    const Json::Value &chunks = manifest[token];
    if ( !chunks.isObject() )
      continue;
    for ( const std::string &chunkKey : chunks.getMemberNames() )
    {
      const Json::Value &chunk = chunks[chunkKey];
      if ( chunk.isObject() && chunk["file"].isString() &&
           isSafeMirrorChunkFileName( chunk["file"].asString() ) )
        referenced.insert( chunk["file"].asString() );
    }
  }
  const std::string chunkDir = mirrorDirectory + "/" + kMirrorChunkDir;
  std::error_code ec;
  for ( std::filesystem::directory_iterator it( std::filesystem::u8path( chunkDir ), ec ), end;
        !ec && it != end; it.increment( ec ) )
  {
    // 13.0: classify by symlink_status — a symlink/reparse point is never
    // deleted through, and an unstatable entry is left alone (fail closed,
    // both counted).
    std::error_code statusEc;
    const std::filesystem::file_status status = it->symlink_status( statusEc );
    if ( statusEc )
    {
      ++report.orphanFilesFailed;
      continue;
    }
    if ( std::filesystem::is_symlink( status ) )
    {
      ++report.orphanFilesRefused;
      continue;
    }
    if ( !std::filesystem::is_regular_file( status ) )
      continue;
    // 13.0: the name must convert to the manifest's UTF-8 domain to prove
    // it unreferenced — an unconvertible name (Windows invalid UTF-16
    // boundary) is a refusal, never a deletion. Non-ASCII names that DO
    // convert are deleted through atomic_fs's UTF-8 path discipline, which
    // re-encodes to the exact native spelling — the path never travels
    // through the active code page.
    std::string name;
    if ( !utf8FileName( *it, name ) )
    {
      ++report.orphanFilesRefused;
      continue;
    }
    if ( referenced.count( name ) > 0 )
      continue;
    std::error_code sizeEc;
    const auto size = std::filesystem::file_size( it->path(), sizeEc );
    if ( atomic_fs::removeFileQuiet( chunkDir + "/" + name ) )
    {
      if ( !sizeEc )
        report.bytesRemoved += static_cast<std::uint64_t>( size );
      ++report.orphanFilesRemoved;
    }
    else
    {
      // Locked / unremovable: fail closed — counted, file preserved.
      ++report.orphanFilesFailed;
    }
  }
  // A mid-scan increment error truncates the sweep: the tail held at
  // least one uninspected entry — counted as a failed action rather than
  // a silently incomplete pass.
  if ( ec )
    ++report.orphanFilesFailed;

  // Publish only when this pass actually changed the MANIFEST (P2
  // review: an unconditional rewrite would churn mtime and invalidate
  // the snapshot cache on a no-op prune — and orphan-file removal does
  // not alter the manifest at all).
  const bool changed = report.deadEntriesRemoved > 0 || report.expiredEntriesRemoved > 0 ||
                       report.quotaEntriesRemoved > 0;
  if ( changed )
  {
    atomic_fs::writeFileAtomic( mirrorDirectory + "/" + kMirrorManifest,
                                [ & ]( const std::string &staged ) {
      const std::string text = Json::writeString( Json::StreamWriterBuilder(), manifest );
      // The staged path is UTF-8 — open it as such, not through the
      // Windows ANSI code page (a non-ASCII mirror root would fail here).
      std::ofstream out( std::filesystem::u8path( staged ), std::ios::binary | std::ios::trunc );
      if ( !out )
        throw GeoError( ErrorCode::IoError, "mirror manifest: cannot create " + staged );
      out.write( text.data(), static_cast<std::streamsize>( text.size() ) );
      out.flush();
      if ( !out )
        throw GeoError( ErrorCode::IoError, "mirror manifest: write failed for " + staged );
    } );
    invalidateManifestSnapshot( mirrorDirectory );
  }
  return report;
}

} // namespace sicnu::geo
