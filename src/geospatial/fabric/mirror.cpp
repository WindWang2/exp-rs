/***************************************************************************
  geospatial/fabric/mirror.cpp — explicit offline mirror.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/fabric/mirror.h"

#include "geospatial/fabric/object_store.h"
#include "geospatial/identity/asset_identity.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/util/resource_uri.h"
#include "geospatial/util/sha256.h"

#include <cpl_vsi.h>

#include <algorithm>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <thread>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace sicnu::geo
{

namespace
{

constexpr const char *kMirrorManifest = "manifest.json";
constexpr const char *kMirrorChunkDir = "chunks";
constexpr const char *kMirrorLockFile = "writer.lock";

Json::Value readManifest( const std::string &mirrorDirectory )
{
  const std::string path = mirrorDirectory + "/" + kMirrorManifest;
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
  const Json::Value manifest = readManifest( mirrorDirectory );
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
  if ( file.empty() )
  {
    if ( skippedCorrupt )
      *skippedCorrupt = "entry without file: token " + token.substr( 0, 8 );
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
  return fullPath;
}

Json::Value mirrorStatsJson( const std::string &mirrorDirectory )
{
  Json::Value stats;
  const Json::Value manifest = readManifest( mirrorDirectory );
  std::uint64_t entries = 0, bytes = 0;
  if ( manifest.isObject() )
  {
    for ( const auto &token : manifest.getMemberNames() )
    {
      const Json::Value &chunks = manifest[token];
      if ( !chunks.isObject() )
        continue;
      entries += chunks.size();
      for ( const auto &key : chunks.getMemberNames() )
        bytes += chunks[key]["bytes"].asUInt64();
    }
  }
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
  std::uint64_t manifestWrites = 0;
  // Identity tokens are cached per walk (a local token hashes ≤ 8 MiB —
  // once per asset, never once per chunk).
  std::map<std::string, std::string> tokenCache;

  std::size_t chunkWindow = options.chunkWindow == 0 ? 64 : options.chunkWindow;
  for ( std::uint64_t begin = 0; begin < total; begin += chunkWindow )
  {
    if ( cancel.cancelled() )
      break;
    if ( bytesWritten >= budget )
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
        report.chunks.push_back( std::move( outcome ) );
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
          token = assetIdentityToken( asset->record.path, AssetIdentityOptions{} ).token;
          tokenCache[asset->record.id] = token;
        }
        if ( token.empty() )
        {
          outcome.status = "skipped-unprovable-identity";
          ++report.skippedUnprovable;
          report.chunks.push_back( std::move( outcome ) );
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
        report.chunks.push_back( std::move( outcome ) );
        continue;
      }
      RasterReader reader = RasterReader::open( fabricCachedPath( asset->record.path ) );
      const RasterMetadata &metadata = reader.metadata();
      // THE shared mapping rule (sign-safe, clamped) — no inline re-derivation.
      const VirtualCubeSourceWindow mapped = virtualCubeSourceWindow(
        metadata, request.minX, request.minY, request.maxX, request.maxY );
      if ( !mapped.ok )
      {
        outcome.status = "failed";
        outcome.errorText = "chunk extent misses the asset's raster";
        ++report.failed;
        report.chunks.push_back( std::move( outcome ) );
        continue;
      }
      const RasterWindow sourceWindow = mapped.window;
      const std::string key =
        fabricChunkMirrorKey( token, asset->record.path, sourceWindow, "band1" );
      outcome.chunkKey = key;

      // Already mirrored? A token-keyed hit is the proof — skip the fetch.
      // The in-memory manifest answers (the walk re-reads nothing; quadratic
      // manifest I/O would sink 10k-chunk passes).
      std::string existing;
      {
        const Json::Value &tokenEntry = manifest[token];
        if ( tokenEntry.isObject() && tokenEntry[key].isObject() &&
             tokenEntry[key]["file"].isString() )
        {
          const std::string candidate =
            mirrorDirectory + "/" + kMirrorChunkDir + "/" + tokenEntry[key]["file"].asString();
          VSIStatBufL statBuffer;
          if ( VSIStatL( candidate.c_str(), &statBuffer ) == 0 )
            existing = candidate;
        }
      }
      if ( !existing.empty() )
      {
        outcome.status = "already-present";
        outcome.file = existing;
        ++report.alreadyPresent;
        report.chunks.push_back( std::move( outcome ) );
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
        report.chunks.push_back( std::move( outcome ) );
        report.budgetStopped = true;
        continue;
      }

      // Read the source window through the normal path (range cache intact)
      // and publish it as a plain GTiff chunk.
      const std::vector<double> values =
        reader.readWindow( { 1 }, sourceWindow, 256ull * 1024 * 1024 );
      const std::string target = mirrorDirectory + "/" + kMirrorChunkDir + "/" + key + ".tif";
      std::uint64_t written = 0;
      atomic_fs::writeFileAtomic( target, [ & ]( const std::string &stagedPath ) {
        RasterWriter writer = RasterWriter::create(
          stagedPath, sourceWindow.width, sourceWindow.height, { RasterBandSpec {} },
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
      manifest[token] = entry;

      atomic_fs::writeFileAtomic( mirrorDirectory + "/" + kMirrorManifest, [ & ]( const std::string &staged ) {
        std::ofstream out( staged, std::ios::binary );
        out << Json::writeString( Json::StreamWriterBuilder(), manifest );
      } );

      outcome.status = "mirrored";
      outcome.file = target;
      outcome.bytes = written;
      bytesWritten += written;
      logicalWritten += request.estimatedBytes;
      ++report.mirrored;
      report.bytesWritten += written;
      report.chunks.push_back( std::move( outcome ) );
    }
  }
  report.cancelled = cancel.cancelled();
  if ( manifestWrites % 32 != 0 )
  {
    atomic_fs::writeFileAtomic( mirrorDirectory + "/" + kMirrorManifest,
                         [ & ]( const std::string &staged ) {
      std::ofstream out( staged, std::ios::binary );
      out << Json::writeString( Json::StreamWriterBuilder(), manifest );
    } );
  }
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
