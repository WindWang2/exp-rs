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
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
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

Json::Value readManifest( const std::string &mirrorDirectory )
{
  const std::string path = mirrorDirectory + "/" + kMirrorManifest;
  VSIStatBufL statBuffer;
  if ( VSIStatL( path.c_str(), &statBuffer ) != 0 )
    return Json::Value( Json::objectValue );
  if ( static_cast<std::uintmax_t>( statBuffer.st_size ) > kMaxMirrorManifestBytes )
    return Json::Value();   // corrupt-by-contract (oversized) — caller counts the skip
  std::ifstream in( path, std::ios::binary );
  if ( !in )
    return Json::Value( Json::objectValue );
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
std::string fileSha256Hex( const std::string &path )
{
  std::ifstream in( path, std::ios::binary );
  if ( !in )
    return {};
  Sha256 hash;
  char buffer[64 * 1024];
  while ( in.read( buffer, sizeof( buffer ) ) || in.gcount() > 0 )
  {
    hash.update( buffer, static_cast<std::size_t>( in.gcount() ) );
    if ( !in )
      break;
  }
  return toHex( hash.finalize() );
}

/// Single-writer guard for one mirror directory (O_EXCL create; a crashed
/// writer's lock is broken by age — the pid is recorded, liveness is not
/// trusted, and the manifest itself is only ever replaced atomically).
class MirrorWriterLock
{
  public:
    explicit MirrorWriterLock( const std::string &mirrorDirectory )
        : mLockPath( mirrorDirectory + "/" + kMirrorLockFile )
    {
#ifdef _WIN32
      mHandle = CreateFileA( ( mirrorDirectory + "/" + kMirrorLockFile ).c_str(), GENERIC_WRITE, 0,
                             nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr );
      mHeld = mHandle != INVALID_HANDLE_VALUE;
#else
      mHandle = ::open( ( mirrorDirectory + "/" + kMirrorLockFile ).c_str(), O_WRONLY | O_CREAT | O_EXCL,
                        0644 );
      mHeld = mHandle >= 0;
#endif
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

  private:
#ifdef _WIN32
    void *mHandle = nullptr;
#else
    int mHandle = -1;
#endif
    bool mHeld = false;
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
                    "another writer holds this mirror directory (single-writer contract)" );

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
      std::ofstream out( staged, std::ios::binary );
      out << Json::writeString( Json::StreamWriterBuilder(), manifest );
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
      for ( const VirtualCubeAssetIndexEntry &entry : cube.assets() )
        if ( entry.record.id == request.assetIdHint )
        {
          asset = &entry;
          break;
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
          std::ifstream in( stagedPath, std::ios::binary );
          in.seekg( 0, std::ios::end );
          written = static_cast<std::uint64_t>( in.tellg() );
        } );

        Json::Value entry = manifest[token];
        entry[key]["file"] = key + ".tif";
        entry[key]["bytes"] = static_cast<Json::UInt64>( written );
        entry[key]["assetId"] = asset->record.id;
        entry[key]["window"] = [ & ] {
          Json::Value w( Json::arrayValue );
          w.append( sourceWindow.xOff );
          w.append( sourceWindow.yOff );
          w.append( sourceWindow.width );
          w.append( sourceWindow.height );
          return w;
        }();
        entry[key]["timeUtc"] = request.timeUtc;
        // 11.0 integrity checksum: replay can demand a payload proof before
        // serving (size checks are cheap; this is the strong form).
        entry[key]["sha256"] = fileSha256Hex( target );
        manifest[token] = entry;

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

} // namespace sicnu::geo
