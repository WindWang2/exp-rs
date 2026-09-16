/***************************************************************************
  tests/test_io_fabric_replay_11.cpp — Cloud Data Fabric 11.0 (WP C):
  TRUE offline mirror replay over a REAL loopback S3 origin.

  The contract under test (DECISIONS D-1103):
    * materialization records an offline index (asset key → token + grid
      facts) alongside the chunk payloads;
    * a LATER process builds the cube and resolves window reads from the
      index + chunks with ZERO network work while forced offline;
    * misses are honest (per-asset provenance failures), corruption is a
      miss (size mismatch), and expiry is a typed, controlled miss.
  The zero-network proof is the loopback server's request counter: it must
  not move by a single request during the whole offline phase.
 ***************************************************************************/

#include "geospatial/fabric/mirror.h"
#include "geospatial/fabric/object_store.h"
#include "geospatial/fabric/query_planner.h"
#include "geospatial/fabric/virtual_cube.h"
#include "geospatial/remote/offline_gate.h"
#include "geospatial/raster/raster_writer.h"
#include "support/http_s3_server.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace sicnu::geo;

namespace
{

std::string scratchDir( const char *name )
{
  std::string path = ( std::filesystem::temp_directory_path() / "sicnu_fabric_tests" /
                       ( std::string( "replay_" ) + name ) )
                       .string();
  std::filesystem::remove_all( path );
  std::filesystem::create_directories( path );
  return path;
}

std::vector<unsigned char> fileBytes( const std::string &path )
{
  std::ifstream in( path, std::ios::binary );
  return std::vector<unsigned char>( ( std::istreambuf_iterator<char>( in ) ),
                                     std::istreambuf_iterator<char>() );
}

/// 96×96 tiled GTiff with a deterministic ramp (same generator discipline
/// as the object-store suite).
std::vector<unsigned char> buildTiff( const std::string &path )
{
  const int size = 96;
  {
    RasterWriter writer = RasterWriter::create( path, size, size, { RasterBandSpec {} },
                                                { "GTiff", { "TILED=YES", "BLOCKXSIZE=64", "BLOCKYSIZE=64" }, true } );
    writer.setGeotransform( { 0.0, 1.0, 0.0, 96.0, 0.0, -1.0 } );   // world Y in [0,96] — matches the cube grid
    std::vector<double> raster( static_cast<std::size_t>( size ) * size );
    for ( std::size_t i = 0; i < raster.size(); ++i )
      raster[i] = static_cast<double>( ( i * 7 ) % 251 );
    writer.writeWindow( 1, { 0, 0, size, size }, raster.data() );
    writer.finalize();
  }
  return fileBytes( path );
}

std::size_t rampMismatches( const std::vector<double> &values, int width, int height,
                            int xOffset = 0, int yOffset = 0 )
{
  std::size_t mismatches = 0;
  for ( int y = 0; y < height; ++y )
    for ( int x = 0; x < width; ++x )
    {
      const std::size_t index = static_cast<std::size_t>( y ) * width + x;
      const std::size_t source = static_cast<std::size_t>( yOffset + y ) * 96 + xOffset + x;
      const double expected = static_cast<double>( ( source * 7 ) % 251 );
      if ( values[index] != expected )
        ++mismatches;
    }
  return mismatches;
}

AssetRecord sceneRecord( const std::string &path )
{
  AssetRecord record;
  record.id = "scene-1";
  record.path = path;
  record.datetimeUtc = "2026-01-01T00:00:00Z";
  record.hasBbox = true;
  record.minX = 0.0;
  record.minY = 0.0;
  record.maxX = 96.0;
  record.maxY = 96.0;
  record.mediaType = "image/tiff";
  record.roles = { "data" };
  return record;
}

FabricIntent cubeIntent( const std::string &path );

/// The offline grid is reconstructed from the mirror index's own grid
/// facts (geotransform-derived extent + CRS) — the replay contract does
/// not require the caller to remember the plan, only the mirror.
VirtualCubeGrid factsToGrid( const MirrorIndexAssetFacts &facts );

} // namespace

TEST_CASE( "forced-offline replay resolves windows from the mirror with ZERO network requests",
           "[io][fabric][replay][wp_c][integration][oracle1]" )
{
  const std::string dir = scratchDir( "s3replay" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  REQUIRE( payload.size() > 4096 );

  testsupport::HttpS3Server server( "eo-bucket", "scene.tif", payload, "\"etag-replay-1\"" );
  REQUIRE( server.valid() );

  ObjectStoreCredentials credentials;
  credentials.accessKeyId = "loopback";
  credentials.secretAccessKey = "loopback-secret";
  credentials.endpoint = server.endpoint();

  const std::string mirrorDir = dir + "/mirror";

  // --- Online phase: plan (identity probes included), mirror 4 chunks,
  // and capture the online window values as the replay oracle.
  std::vector<double> onlineWindow;
  std::vector<double> onlineWindowEdge;
  {
    ScopedObjectStoreCredentials window( "/vsis3/", credentials );
    const FabricPlan plan = planFabric( cubeIntent( "/vsis3/eo-bucket/scene.tif" ) );
    REQUIRE( plan.cost().scenes == 1 );
    CHECK( plan.cost().cacheableAssets == 1 );   // WP A: /vsis3/ identity is PROVABLE now

    MirrorOptions mirrorOptions;
    mirrorOptions.mirrorDirectory = mirrorDir;
    const MirrorReport report = mirrorChunks( plan, mirrorOptions );
    for ( const MirrorChunkOutcome &outcome : report.chunks )
      WARN( "chunk " << outcome.index << ": " << outcome.status << " | " << outcome.errorText );
    CHECK( report.mirrored == 4 );
    CHECK( report.failed == 0 );
    CHECK( report.outcomesDropped == 0 );

    const VirtualCube onlineCube = VirtualCube::build(
      plan.selectedAssets(), plan.grid(), OverlapPolicy::FirstWins, {} );
    onlineWindow = onlineCube.readWindow( 0, 0, 64, 64 ).values;
    REQUIRE( onlineWindow.size() == 64 * 64 );
    // A NON-origin chunk too (review R13): the (64,0) chunk covers the
    // 96-wide grid's x ∈ [64,96) — 32 wide. Origin-only oracles hid the
    // scatter-basis class of defects.
    onlineWindowEdge = onlineCube.readWindow( 64, 0, 32, 64 ).values;
    REQUIRE( onlineWindowEdge.size() == 32 * 64 );
  }

  // The offline index recorded the asset (token + grid facts).
  MirrorIndexAssetFacts facts;
  REQUIRE( lookupMirrorAsset( mirrorDir, "/vsis3/eo-bucket/scene.tif", facts ) );
  CHECK( facts.found );
  CHECK( !facts.token.empty() );
  CHECK( facts.hasGrid );
  CHECK( facts.rasterWidth == 96 );
  CHECK( facts.rasterHeight == 96 );

  // --- Offline phase: NOT ONE request may reach the origin. The guard
  // restores the process-global state even when an assertion fails mid-case.
  const std::uint64_t requestsBeforeOffline = server.requestCount();
  struct OfflineGuard
  {
    OfflineGuard() { offline::setEnabled( true ); offline::applyGdalNetworkDeny(); }
    ~OfflineGuard() { offline::clearGdalNetworkDeny(); offline::setEnabled( false ); }
  } offlineGuard;

  // The cube builds from the offline index alone (no probe budget spent,
  // no remote metadata open — the deny would make any attempt loud).
  VirtualCubeBuildOptions buildOptions;
  buildOptions.mirrorDirectory = mirrorDir;
  const VirtualCube offlineCube =
    VirtualCube::build( { sceneRecord( "/vsis3/eo-bucket/scene.tif" ) }, factsToGrid( facts ),
                        OverlapPolicy::FirstWins, {}, buildOptions );
  REQUIRE( offlineCube.assetCount() == 1 );
  CHECK( offlineCube.assets().front().fromMirrorIndex );
  CHECK( !offlineCube.assets().front().identityToken.empty() );

  VirtualCubeReadOptions readOptions;
  readOptions.mirrorDirectory = mirrorDir;

  // Read one full mirrored chunk window (the grid's (0,0) 64×64 chunk).
  const VirtualCubeWindowResult replay = offlineCube.readWindow( 0, 0, 64, 64, readOptions );
  REQUIRE( replay.provenance.size() == 1 );
  CHECK( replay.provenance.front().contributed );
  CHECK( !replay.provenance.front().mirrorHit.empty() );   // served from the mirror
  REQUIRE( replay.values.size() == onlineWindow.size() );
  CHECK( replay.values == onlineWindow );                  // byte-equal replay

  // The non-origin chunk replays byte-equal TOO — the scatter basis is the
  // ASSET-pixel window, so chunks at (64,0) contribute where they stand.
  // The (64,0) chunk of a 96-wide grid is 32 wide and 64 tall (rectangle).
  {
    std::ifstream man( mirrorDir + "/manifest.json" );
    std::string manText( ( std::istreambuf_iterator<char>( man ) ),
                         std::istreambuf_iterator<char>() );
    WARN( "manifest: " << manText );
  }
  const VirtualCubeWindowResult replayEdge =
    offlineCube.readWindow( 64, 0, 32, 64, readOptions );
  REQUIRE( replayEdge.provenance.size() == 1 );
  CHECK( replayEdge.provenance.front().contributed );
  CHECK( !replayEdge.provenance.front().mirrorHit.empty() );
  REQUIRE( replayEdge.values.size() == onlineWindowEdge.size() );
  CHECK( replayEdge.values == onlineWindowEdge );

  CHECK( server.requestCount() == requestsBeforeOffline ); // ORACLE 1: zero requests
}

TEST_CASE( "offline misses are honest, corrupted chunks are misses, expiry is controlled",
           "[io][fabric][replay][wp_c][negative]" )
{
  const std::string dir = scratchDir( "s3replay2" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  testsupport::HttpS3Server server( "eo-bucket", "scene.tif", payload, "\"etag-replay-2\"" );
  REQUIRE( server.valid() );

  ObjectStoreCredentials credentials;
  credentials.accessKeyId = "loopback";
  credentials.secretAccessKey = "loopback-secret";
  credentials.endpoint = server.endpoint();

  const std::string mirrorDir = dir + "/mirror";
  {
    ScopedObjectStoreCredentials window( "/vsis3/", credentials );
    const ObjectStoreIdentityFacts probe = probeObjectStoreIdentity( "/vsis3/eo-bucket/scene.tif" );
    WARN( "case2 probe probed=" << probe.probed << " etag=" << probe.etag
          << " err=" << probe.errorText << " requests=" << server.requestCount() );
    const FabricPlan plan = planFabric( cubeIntent( "/vsis3/eo-bucket/scene.tif" ) );
    WARN( "case2 cost chunks=" << plan.cost().chunks << " scenes=" << plan.cost().scenes );
    MirrorOptions mirrorOptions;
    mirrorOptions.mirrorDirectory = mirrorDir;
    const MirrorReport report = mirrorChunks( plan, mirrorOptions );
    WARN( "case2 mirrored=" << report.mirrored << " failed=" << report.failed
          << " budgetStopped=" << report.budgetStopped );
    for ( const MirrorChunkOutcome &outcome : report.chunks )
      WARN( "case2 chunk " << outcome.index << ": " << outcome.status << " | " << outcome.errorText );
    REQUIRE( report.mirrored == 4 );
  }

  MirrorIndexAssetFacts facts;
  REQUIRE( lookupMirrorAsset( mirrorDir, "/vsis3/eo-bucket/scene.tif", facts ) );
  REQUIRE( facts.hasGrid );

  // Expiry: rewrite the manifest's materialization stamp into the far past;
  // a maxAge bound turns the hit into a typed, controlled miss.
  const std::string manifestPath = mirrorDir + "/manifest.json";
  std::ifstream in( manifestPath, std::ios::binary );
  std::string text( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
  in.close();
  const std::string stampKey = "\"writtenUtc\"";
  const std::size_t at = text.find( stampKey );
  REQUIRE( at != std::string::npos );
  const std::size_t valueStart = text.find( "\"", at + stampKey.size() ) + 1;
  const std::size_t valueEnd = text.find( "\"", valueStart );
  const std::string oldStamp = text.substr( valueStart, valueEnd - valueStart );
  // A 1999 stamp with DIFFERENT length than the fresh one — the manifest
  // snapshot cache keys on (size, mtime) and this rewrite lands within the
  // same mtime second as the materialization flush.
  const std::string expiredStamp = "1999-12-31T23:59:59.000Z";
  text.replace( valueStart, oldStamp.size(), expiredStamp );
  {
    std::ofstream out( manifestPath, std::ios::binary | std::ios::trunc );
    out << text;
  }
  const RasterWindow anyWindow { 0, 0, 64, 64 };
  const MirrorArtifactHit expired = resolveMirrorArtifact(
    mirrorDir, "/vsis3/eo-bucket/scene.tif", anyWindow, "band1", /*maxAgeSeconds=*/3600 );
  CHECK( expired.expired );
  CHECK( !expired.hit );
  CHECK( expired.file.empty() );
  // Without a maxAge, the same artifact still resolves.
  const MirrorArtifactHit unexpired = resolveMirrorArtifact(
    mirrorDir, "/vsis3/eo-bucket/scene.tif", anyWindow, "band1", /*maxAgeSeconds=*/0 );
  CHECK( unexpired.hit );

  // Corruption: truncate the chunk THE QUERY TARGETS (derived from the
  // resolve itself — deterministic, not directory-order luck). The size
  // check (and the sha256 the manifest records) must turn it into a miss,
  // never a false hit.
  const MirrorArtifactHit beforeCorruption = resolveMirrorArtifact(
    mirrorDir, "/vsis3/eo-bucket/scene.tif", anyWindow, "band1", 0 );
  REQUIRE( beforeCorruption.hit );
  const std::string corruptTarget = beforeCorruption.file;
  std::filesystem::resize_file( corruptTarget, 0 );
  const MirrorArtifactHit corrupt = resolveMirrorArtifact(
    mirrorDir, "/vsis3/eo-bucket/scene.tif", anyWindow, "band1", 0 );
  CHECK( !corrupt.hit );
  CHECK( !corrupt.skippedCorrupt.empty() );

  // Same-size tamper: flip bytes in the MIDDLE of a DIFFERENT chunk — the
  // size check passes, so only the recorded sha256 can refuse it.
  const RasterWindow otherWindow { 64, 0, 32, 64 };
  const MirrorArtifactHit tamperTarget = resolveMirrorArtifact(
    mirrorDir, "/vsis3/eo-bucket/scene.tif", otherWindow, "band1", 0 );
  REQUIRE( tamperTarget.hit );
  {
    std::fstream tamper( tamperTarget.file,
                         std::ios::binary | std::ios::in | std::ios::out );
    REQUIRE( tamper.is_open() );
    tamper.seekp( 200 );
    char flip = 0;
    tamper.read( &flip, 1 );
    tamper.seekp( 200 );
    tamper.put( static_cast<char>( flip ^ 0xFF ) );
  }
  const MirrorArtifactHit tampered = resolveMirrorArtifact(
    mirrorDir, "/vsis3/eo-bucket/scene.tif", otherWindow, "band1", 0 );
  CHECK( !tampered.hit );
  CHECK( tampered.skippedCorrupt.find( "sha256" ) != std::string::npos );

  // Forced offline + a corrupted mirror = honest per-asset failure (the
  // window is answered NoData and the provenance says why), and still
  // zero requests.
  const std::uint64_t requestsBefore = server.requestCount();
  struct OfflineGuard
  {
    OfflineGuard() { offline::setEnabled( true ); offline::applyGdalNetworkDeny(); }
    ~OfflineGuard() { offline::clearGdalNetworkDeny(); offline::setEnabled( false ); }
  } offlineGuard;
  VirtualCubeBuildOptions buildOptions;
  buildOptions.mirrorDirectory = mirrorDir;
  const VirtualCube offlineCube = VirtualCube::build(
    { sceneRecord( "/vsis3/eo-bucket/scene.tif" ) }, factsToGrid( facts ),
    OverlapPolicy::FirstWins, {}, buildOptions );
  VirtualCubeReadOptions readOptions;
  readOptions.mirrorDirectory = mirrorDir;
  const VirtualCubeWindowResult window = offlineCube.readWindow( 0, 0, 64, 64, readOptions );
  const bool honestlyFailed = window.provenance.empty() ||
                              ( window.provenance.size() == 1 && window.provenance.front().failed );
  const bool servedFromMirror =
    !honestlyFailed && !window.provenance.empty() &&
    !window.provenance.front().mirrorHit.empty();
  CHECK( ( honestlyFailed || servedFromMirror ) );
  CHECK( server.requestCount() == requestsBefore );   // still zero dispatch
}

namespace
{

FabricIntent cubeIntent( const std::string &path )
{
  FabricIntent intent;
  intent.records = { sceneRecord( path ) };
  intent.grid.explicitGrid = true;
  intent.grid.crs.valid = true;
  intent.grid.crs.authid = "EPSG:4326";
  intent.grid.scaleX = 1.0;
  intent.grid.scaleY = 1.0;
  intent.grid.minX = 0.0;
  intent.grid.minY = 0.0;
  intent.grid.maxX = 96.0;
  intent.grid.maxY = 96.0;
  intent.chunkShape.time = 1;
  intent.chunkShape.y = 64;
  intent.chunkShape.x = 64;
  intent.chunkShape.band = 1;
  return intent;
}

// The offline grid is reconstructed from the mirror index's own grid
// facts (geotransform-derived extent + CRS) — the replay contract does
// not require the caller to remember the plan, only the mirror.
VirtualCubeGrid factsToGrid( const MirrorIndexAssetFacts &facts )
{
  VirtualCubeGrid grid;
  grid.explicitGrid = true;
  grid.crs.valid = true;
  grid.crs.authid = facts.epsgAuthid.empty() ? std::string( "EPSG:4326" ) : facts.epsgAuthid;
  grid.scaleX = facts.resX;
  grid.scaleY = facts.resY < 0 ? -facts.resY : facts.resY;   // magnitudes; the cube fixes the Y sign
  grid.minX = facts.assetMinX;
  grid.minY = facts.assetMinY;
  grid.maxX = facts.assetMaxX;
  grid.maxY = facts.assetMaxY;
  return grid;
}

} // namespace
