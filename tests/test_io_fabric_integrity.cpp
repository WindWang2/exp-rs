/***************************************************************************
  tests/test_io_fabric_integrity.cpp — fabric trust-chain fail-closed suite
  (issues #1053 / #1054 / #1038 / #1056 geospatial items).

  Contracts locked here:
    * a crafted manifest "file" NEVER resolves outside the mirror root
      (../../, absolute, backslash, colon spellings);
    * format-2 manifests demand a sha256 proof per chunk (absence is a
      corrupt miss, a tampered payload never serves);
    * one bad chunk records its outcome and the walk continues — the
      MirrorReport survives with accurate counters;
    * wrong-typed external JSON degrades to typed skips/failures — never an
      escaping Json::LogicError;
    * band facts resolve by `.index`, never by vector position (#1054).
 ***************************************************************************/

#include "geospatial/fabric/catalog_service.h"
#include "geospatial/fabric/chunk_plan.h"
#include "geospatial/fabric/mirror.h"
#include "geospatial/fabric/virtual_cube.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/remote/remote_source_validator.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/util/sha256.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

using namespace sicnu::geo;

namespace
{

std::string scratchDir( const char *name )
{
  std::string path = ( std::filesystem::temp_directory_path() / "sicnu_fabric_tests" /
                       ( std::string( "integrity_" ) + name ) )
                       .string();
  std::filesystem::remove_all( path );
  std::filesystem::create_directories( path );
  return path;
}

VirtualCubeGrid cubeGrid()
{
  VirtualCubeGrid grid;
  grid.explicitGrid = true;
  grid.crs.valid = true;
  grid.crs.authid = "EPSG:4326";
  grid.scaleX = 1.0;
  grid.scaleY = 1.0;
  grid.minX = 0.0;
  grid.minY = 0.0;
  grid.maxX = 96.0;
  grid.maxY = 96.0;
  return grid;
}

/// 96×96 GTiff with the (i*7 % 251) ramp — same generator discipline as the
/// replay suite.
std::string writeRampScene( const std::string &path, int size = 96 )
{
  RasterWriter writer = RasterWriter::create(
    path, size, size, { RasterBandSpec {} },
    { "GTiff", { "TILED=YES", "BLOCKXSIZE=64", "BLOCKYSIZE=64" }, true } );
  writer.setGeotransform( { 0.0, 1.0, 0.0, static_cast<double>( size ), 0.0, -1.0 } );
  std::vector<double> raster( static_cast<std::size_t>( size ) * size );
  for ( std::size_t i = 0; i < raster.size(); ++i )
    raster[i] = static_cast<double>( ( i * 7 ) % 251 );
  writer.writeWindow( 1, { 0, 0, size, size }, raster.data() );
  writer.finalize();
  return path;
}

/// A DIFFERENT payload — what an escaped path would serve if traversal won.
std::string writeEvilScene( const std::string &path, int size = 96 )
{
  RasterWriter writer = RasterWriter::create(
    path, size, size, { RasterBandSpec {} },
    { "GTiff", { "TILED=YES", "BLOCKXSIZE=64", "BLOCKYSIZE=64" }, true } );
  writer.setGeotransform( { 0.0, 1.0, 0.0, static_cast<double>( size ), 0.0, -1.0 } );
  std::vector<double> raster( static_cast<std::size_t>( size ) * size, 666.0 );
  writer.writeWindow( 1, { 0, 0, size, size }, raster.data() );
  writer.finalize();
  return path;
}

AssetRecord sceneRecord( const std::string &id, const std::string &path, const std::string &instant )
{
  AssetRecord record;
  record.id = id;
  record.path = path;
  record.datetimeUtc = instant;
  record.hasBbox = true;
  record.minX = 0.0;
  record.minY = 0.0;
  record.maxX = 96.0;
  record.maxY = 96.0;
  record.mediaType = "image/tiff";
  record.roles = { "data" };
  return record;
}

std::string fileText( const std::string &path )
{
  std::ifstream in( path, std::ios::binary );
  std::string text( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
  return text;
}

void writeText( const std::string &path, const std::string &text )
{
  std::ofstream out( path, std::ios::binary | std::ios::trunc );
  out << text;
}

std::string fileSha256( const std::string &path )
{
  std::ifstream in( path, std::ios::binary );
  Sha256 hash;
  char buffer[4096];
  while ( in.read( buffer, sizeof( buffer ) ) || in.gcount() > 0 )
  {
    hash.update( buffer, static_cast<std::size_t>( in.gcount() ) );
    if ( !in )
      break;
  }
  return toHex( hash.finalize() );
}

Json::Value parseJsonText( const std::string &text )
{
  Json::Value parsed;
  Json::CharReaderBuilder builder;
  std::istringstream stream( text );
  std::string errors;
  REQUIRE( Json::parseFromStream( builder, stream, &parsed, &errors ) );
  return parsed;
}

/// Runs a real mirror pass over ONE local scene (4 chunks of 48×48) and
/// returns the mirror directory. Asserts the pass was clean.
std::string mirrorOneScene( const std::string &dir, const std::string &scenePath )
{
  const std::string mirrorDir = dir + "/mirror";
  const VirtualCube cube =
    VirtualCube::build( { sceneRecord( "scene-1", scenePath, "2026-01-01T00:00:00Z" ) }, cubeGrid(),
                        OverlapPolicy::FirstWins, {} );
  const CubeChunkPlan plan =
    CubeChunkPlan::forVirtualCube( cube, CubeChunkShape { 1, 48, 48, 1, {} } );
  MirrorOptions options;
  options.mirrorDirectory = mirrorDir;
  const MirrorReport report = mirrorChunks( cube, plan, options );
  REQUIRE( report.failed == 0 );
  REQUIRE( report.mirrored == 4 );
  return mirrorDir;
}

/// Rewrites the mirror manifest over a REAL pass's shape.
void rewriteManifest( const std::string &mirrorDir,
                      const std::function<void( Json::Value &manifest )> &transform )
{
  Json::Value manifest = parseJsonText( fileText( mirrorDir + "/manifest.json" ) );
  transform( manifest );
  Json::StreamWriterBuilder builder;
  writeText( mirrorDir + "/manifest.json", Json::writeString( builder, manifest ) );
}

/// Applies a transform to every chunk entry of the manifest.
void forEachChunk( Json::Value &manifest, const std::function<void( Json::Value &chunk )> &apply )
{
  for ( const std::string &token : manifest.getMemberNames() )
  {
    if ( !manifest[token].isObject() )
      continue;
    for ( const std::string &key : manifest[token].getMemberNames() )
    {
      Json::Value &chunk = manifest[token][key];
      if ( chunk.isObject() && chunk.isMember( "file" ) )
        apply( chunk );
    }
  }
}

} // namespace

TEST_CASE( "a crafted ../../ mirror file can never escape the mirror root",
           "[io][fabric][mirror][traversal][oracle1]" )
{
  const std::string dir = scratchDir( "traversal" );
  const std::string scene = writeRampScene( dir + "/scene.tif" );
  // The escape payload sits OUTSIDE the mirror (one level up).
  const std::string evil = writeEvilScene( dir + "/escaped.tif" );
  const std::string mirrorDir = mirrorOneScene( dir, scene );
  const RasterWindow window { 0, 0, 48, 48 };

  // A crafted manifest whose chunk file points at the escape payload —
  // bytes/sha256 declared HONESTLY for the escape file, so ONLY the path
  // guard can refuse it.
  rewriteManifest( mirrorDir, [ & ]( Json::Value &manifest ) {
    forEachChunk( manifest, [ & ]( Json::Value &chunk ) {
      chunk["file"] = "../escaped.tif";
      chunk["bytes"] = static_cast<Json::UInt64>( std::filesystem::file_size( evil ) );
      chunk["sha256"] = fileSha256( evil );
    } );
  } );

  // The replay lookup refuses the entry (typed corrupt reason); it never
  // stats or serves the outside file.
  const MirrorArtifactHit hit = resolveMirrorArtifact( mirrorDir, scene, window, "band1" );
  CHECK( !hit.hit );
  CHECK( hit.file.empty() );
  REQUIRE( !hit.skippedCorrupt.empty() );
  CHECK( hit.skippedCorrupt.find( "escapes the mirror root" ) != std::string::npos );

  // Absolute and backslash spellings are equally foreign (the walk only
  // ever writes 32-hex-char basenames).
  rewriteManifest( mirrorDir, [ & ]( Json::Value &manifest ) {
    forEachChunk( manifest, [ & ]( Json::Value &chunk ) { chunk["file"] = "/etc/hostname"; } );
  } );
  const MirrorArtifactHit absolute = resolveMirrorArtifact( mirrorDir, scene, window, "band1" );
  CHECK( !absolute.hit );
  CHECK( absolute.skippedCorrupt.find( "escapes the mirror root" ) != std::string::npos );

  rewriteManifest( mirrorDir, [ & ]( Json::Value &manifest ) {
    forEachChunk( manifest, [ & ]( Json::Value &chunk ) { chunk["file"] = "..\\..\\evil.tif"; } );
  } );
  const MirrorArtifactHit backslashed = resolveMirrorArtifact( mirrorDir, scene, window, "band1" );
  CHECK( !backslashed.hit );
  CHECK( backslashed.skippedCorrupt.find( "escapes the mirror root" ) != std::string::npos );

  // The full replay path stays honest too: with the crafted manifest, the
  // window falls back to the ORIGIN (the escape payload's 666 ramp must
  // never appear in replayed values).
  rewriteManifest( mirrorDir, [ & ]( Json::Value &manifest ) {
    forEachChunk( manifest, [ & ]( Json::Value &chunk ) {
      chunk["file"] = "../escaped.tif";
      chunk["bytes"] = static_cast<Json::UInt64>( std::filesystem::file_size( evil ) );
      chunk["sha256"] = fileSha256( evil );
      chunk["probe"] = "replay-fallback";   // distinct bytes: defeats any snapshot caching
    } );
  } );
  const VirtualCube cube = VirtualCube::build( { sceneRecord( "scene-1", scene, "2026-01-01T00:00:00Z" ) },
                                               cubeGrid(), OverlapPolicy::FirstWins, {} );
  VirtualCubeReadOptions readOptions;
  readOptions.mirrorDirectory = mirrorDir;
  const VirtualCubeWindowResult replay = cube.readWindow( 0, 0, 48, 48, readOptions );
  REQUIRE( replay.values.size() == 48 * 48 );
  for ( const double value : replay.values )
    CHECK( value != 666.0 );
}

TEST_CASE( "format-2 mirror manifests demand sha256 proofs; tampered chunks never serve",
           "[io][fabric][mirror][integrity]" )
{
  const std::string dir = scratchDir( "proof" );
  const std::string scene = writeRampScene( dir + "/scene.tif" );
  const std::string mirrorDir = mirrorOneScene( dir, scene );
  const RasterWindow window { 0, 0, 48, 48 };

  // The 11.0 writer stamps format_version 2 and proofs every chunk.
  const Json::Value manifest = parseJsonText( fileText( mirrorDir + "/manifest.json" ) );
  REQUIRE( manifest["format_version"].isInt() );
  CHECK( manifest["format_version"].asInt() == 2 );
  const MirrorArtifactHit clean = resolveMirrorArtifact( mirrorDir, scene, window, "band1" );
  CHECK( clean.hit );

  // A format-2 entry WITHOUT the sha256 proof is a corrupt miss (fail
  // closed) even though the file itself is intact.
  rewriteManifest( mirrorDir, [ & ]( Json::Value &manifest ) {
    forEachChunk( manifest, [ & ]( Json::Value &chunk ) { chunk.removeMember( "sha256" ); } );
  } );
  const MirrorArtifactHit unproven = resolveMirrorArtifact( mirrorDir, scene, window, "band1" );
  CHECK( !unproven.hit );
  REQUIRE( !unproven.skippedCorrupt.empty() );
  CHECK( unproven.skippedCorrupt.find( "missing sha256 proof" ) != std::string::npos );

  // A LEGACY (format-1) manifest — no format_version marker, no sha256 —
  // keeps replaying (size-only tolerance for pre-11.0 mirrors).
  rewriteManifest( mirrorDir, [ & ]( Json::Value &manifest ) {
    manifest.removeMember( "format_version" );
    forEachChunk( manifest, [ & ]( Json::Value &chunk ) { chunk.removeMember( "sha256" ); } );
  } );
  const MirrorArtifactHit legacy = resolveMirrorArtifact( mirrorDir, scene, window, "band1" );
  CHECK( legacy.hit );
  CHECK( legacy.skippedCorrupt.empty() );   // a clean hit

  // The legacy tolerance is NOT a size blind spot: appending one byte to a
  // chunk breaks the declared `bytes` proof → corrupt miss.
  for ( auto &entry : std::filesystem::directory_iterator( mirrorDir + "/chunks" ) )
  {
    std::ofstream out( entry.path().string(), std::ios::binary | std::ios::app );
    out << 'X';
  }
  const MirrorArtifactHit tampered = resolveMirrorArtifact( mirrorDir, scene, window, "band1" );
  CHECK( !tampered.hit );
  CHECK( tampered.skippedCorrupt.find( "size mismatch" ) != std::string::npos );
}

TEST_CASE( "a legacy v1 mirror is upgraded in place: proofs are backfilled, not stranded",
           "[io][fabric][mirror][legacy-upgrade]" )
{
  const std::string dir = scratchDir( "legacy_upgrade" );
  const std::string scene = writeRampScene( dir + "/scene.tif" );
  const std::string mirrorDir = mirrorOneScene( dir, scene );
  const RasterWindow window { 0, 0, 48, 48 };

  // Rewind the manifest to a pre-11.0 v1 shape: no format_version, no
  // sha256 (bytes stay declared).
  rewriteManifest( mirrorDir, [ & ]( Json::Value &manifest ) {
    manifest.removeMember( "format_version" );
    forEachChunk( manifest, [ & ]( Json::Value &chunk ) { chunk.removeMember( "sha256" ); } );
  } );
  const MirrorArtifactHit legacy = resolveMirrorArtifact( mirrorDir, scene, window, "band1" );
  CHECK( legacy.hit );   // v1 tolerance

  // Re-touching the mirror with the v11 writer must UPGRADE the manifest
  // in place: every recorded chunk is hashed and proven once, so the
  // stricter v2 replay contract never strands the legacy chunks.
  const VirtualCube cube = VirtualCube::build( { sceneRecord( "scene-1", scene, "2026-01-01T00:00:00Z" ) },
                                               cubeGrid(), OverlapPolicy::FirstWins, {} );
  const CubeChunkPlan plan =
    CubeChunkPlan::forVirtualCube( cube, CubeChunkShape { 1, 48, 48, 1, {} } );
  MirrorOptions options;
  options.mirrorDirectory = mirrorDir;
  const MirrorReport report = mirrorChunks( cube, plan, options );
  CHECK( report.failed == 0 );
  CHECK( report.mirrored == 0 );
  CHECK( report.alreadyPresent == 4 );   // proven chunks are deduped, not re-fetched

  const Json::Value manifest = parseJsonText( fileText( mirrorDir + "/manifest.json" ) );
  CHECK( manifest["format_version"].asInt() == 2 );
  std::size_t provenChunks = 0;
  for ( const std::string &token : manifest.getMemberNames() )
  {
    if ( !manifest[token].isObject() )
      continue;
    for ( const std::string &key : manifest[token].getMemberNames() )
    {
      const Json::Value &chunk = manifest[token][key];
      if ( chunk.isObject() && chunk["sha256"].isString() && !chunk["sha256"].asString().empty() )
        ++provenChunks;
    }
  }
  CHECK( provenChunks == 4 );

  // …and the upgraded mirror still replays.
  const MirrorArtifactHit upgraded = resolveMirrorArtifact( mirrorDir, scene, window, "band1" );
  CHECK( upgraded.hit );
}

TEST_CASE( "one bad chunk fails alone: the walk continues and the report survives",
           "[io][fabric][mirror][isolation][oracle2]" )
{
  const std::string dir = scratchDir( "isolation" );
  // Two time steps: one owned by a healthy scene, one by an asset that is
  // identity-provable (hashable bytes) but fails at RasterReader::open —
  // the mid-walk GeoError the per-chunk catch must isolate (#1053).
  const std::string good = writeRampScene( dir + "/good.tif" );
  const std::string notARaster = dir + "/bad.tif";
  writeText( notARaster, "definitely not a GeoTIFF" );

  const VirtualCube cube = VirtualCube::build(
    { sceneRecord( "good", good, "2026-01-01T00:00:00Z" ),
      sceneRecord( "bad", notARaster, "2026-02-01T00:00:00Z" ) },
    cubeGrid(), OverlapPolicy::FirstWins, {} );
  REQUIRE( cube.assetCount() == 2 );
  const CubeChunkPlan plan =
    CubeChunkPlan::forVirtualCube( cube, CubeChunkShape { 1, 48, 48, 1, {} } );
  REQUIRE( plan.chunkCountTotal() == 8 );   // 2 time steps × 2×2 spatial

  MirrorOptions options;
  options.mirrorDirectory = dir + "/mirror";
  const MirrorReport report = mirrorChunks( cube, plan, options );   // must NOT throw

  CHECK( report.mirrored == 4 );      // the healthy time step
  CHECK( report.failed == 4 );        // the unreadable time step — isolated
  CHECK( !report.budgetStopped );
  CHECK( !report.cancelled );
  std::size_t failedOutcomes = 0;
  std::size_t mirroredOutcomes = 0;
  for ( const MirrorChunkOutcome &outcome : report.chunks )
  {
    if ( outcome.status == "failed" )
    {
      ++failedOutcomes;
      CHECK( !outcome.errorText.empty() );   // every failure is described
    }
    if ( outcome.status == "mirrored" )
      ++mirroredOutcomes;
  }
  CHECK( failedOutcomes == 4 );
  CHECK( mirroredOutcomes == 4 );

  // The walk still published what it wrote (a readable manifest exists).
  const Json::Value manifest = parseJsonText( fileText( dir + "/mirror/manifest.json" ) );
  CHECK( manifest.isObject() );
  CHECK( manifest["format_version"].asInt() == 2 );
}

TEST_CASE( "hostile mirror manifests degrade to typed skips, never Json::LogicError",
           "[io][fabric][mirror][json][oracle4]" )
{
  const std::string dir = scratchDir( "hostile_manifest" );
  const std::string scene = writeRampScene( dir + "/scene.tif" );
  const std::string mirrorDir = mirrorOneScene( dir, scene );
  const RasterWindow window { 0, 0, 48, 48 };

  // Non-string `file`, non-numeric bytes/sha256: typed corrupt skips.
  rewriteManifest( mirrorDir, [ & ]( Json::Value &manifest ) {
    forEachChunk( manifest, [ & ]( Json::Value &chunk ) {
      chunk["file"] = Json::Value( Json::objectValue );
      chunk["bytes"] = "huge";
      chunk["sha256"] = 42;
    } );
  } );
  const MirrorArtifactHit hit = resolveMirrorArtifact( mirrorDir, scene, window, "band1" );
  CHECK( !hit.hit );
  CHECK( hit.skippedCorrupt.find( "non-string file" ) != std::string::npos );

  // Hostile stats + index shapes answer with honest zeros, no throw.
  rewriteManifest( mirrorDir, [ & ]( Json::Value &manifest ) {
    Json::Value crafted( Json::objectValue );
    crafted["format_version"] = 2;
    Json::Value entry( Json::objectValue );
    Json::Value chunk( Json::objectValue );
    chunk["file"] = "whatever.tif";
    chunk["bytes"] = Json::Value( Json::arrayValue );
    entry["chunkA"] = chunk;
    crafted["token-x"] = entry;
    Json::Value grid( Json::objectValue );
    grid["width"] = "wide";
    grid["height"] = 96;
    grid["geotransform"] = Json::Value( Json::arrayValue );
    for ( int i = 0; i < 6; ++i )
      grid["geotransform"].append( Json::Value( Json::objectValue ) );
    Json::Value indexEntry( Json::objectValue );
    indexEntry["token"] = "token-x";
    indexEntry["grid"] = grid;
    Json::Value index( Json::objectValue );
    index[fabricMirrorIndexKey( scene )] = indexEntry;
    crafted["index"] = index;
    manifest = crafted;
  } );
  Json::Value stats;
  CHECK_NOTHROW( stats = mirrorStatsJson( mirrorDir ) );
  CHECK( stats["entries"].asUInt64() == 1 );   // the hostile chunk still counts…
  CHECK( stats["bytes"].asUInt64() == 0 );     // …but contributes no invented bytes

  MirrorIndexAssetFacts facts;
  CHECK_NOTHROW( lookupMirrorAsset( mirrorDir, scene, facts ) );
  CHECK( facts.found );
  CHECK( !facts.hasGrid );   // a wrong-typed grid records no facts, no throw

  // An over-cap manifest is corrupt for readers and REFUSED for writers
  // (never silently replaced).
  writeText( mirrorDir + "/manifest.json", std::string( 17ull * 1024ull * 1024ull, ' ' ) );
  CHECK( resolveMirrorHit( mirrorDir, "token", "key" ).empty() );
  const VirtualCube cube = VirtualCube::build( { sceneRecord( "scene-1", scene, "2026-01-01T00:00:00Z" ) },
                                               cubeGrid(), OverlapPolicy::FirstWins, {} );
  const CubeChunkPlan plan =
    CubeChunkPlan::forVirtualCube( cube, CubeChunkShape { 1, 48, 48, 1, {} } );
  MirrorOptions options;
  options.mirrorDirectory = mirrorDir;
  bool typedRefusal = false;
  try
  {
    mirrorChunks( cube, plan, options );
  }
  catch ( const GeoError &error )
  {
    typedRefusal = error.code() == ErrorCode::InvalidMetadata;
  }
  CHECK( typedRefusal );
}

TEST_CASE( "hostile grid JSON fails typed and band facts resolve by .index, never position",
           "[io][fabric][cube][bands][oracle3]" )
{
  // VirtualCubeGrid::fromJson: foreign-typed fields are GeoErrors.
  CHECK_THROWS_AS( VirtualCubeGrid::fromJson( parseJsonText( R"({"crs":{},"scaleX":1,"scaleY":1})" ) ),
                   GeoError );
  CHECK_THROWS_AS( VirtualCubeGrid::fromJson( parseJsonText(
                     R"({"crs":"EPSG:4326","scaleX":"x","scaleY":1,"extent":[0,0,1,1]})" ) ),
                   GeoError );
  CHECK_THROWS_AS( VirtualCubeGrid::fromJson( parseJsonText( R"([1,2])" ) ), GeoError );

  // #1054: bands is INDEX-keyed and can carry holes (null interior band
  // handles are skipped by inspectRaster); findBandInfo must answer by
  // .index — `bands[2]` as a POSITION would be out of bounds for this shape.
  const RasterMetadata meta = RasterMetadata::fromJson( parseJsonText( R"({
    "bands": [
      { "index": 1, "dtype": "Byte" },
      { "index": 3, "dtype": "UInt16" }
    ]
  })" ) );
  REQUIRE( meta.bands.size() == 2 );
  CHECK( meta.bands[0].index == 1 );
  CHECK( meta.bands[1].index == 3 );

  const BandInfo *band3 = findBandInfo( meta, 3 );
  REQUIRE( band3 != nullptr );
  CHECK( band3->index == 3 );
  CHECK( band3->dtype == "UInt16" );
  CHECK( findBandInfo( meta, 1 ) != nullptr );
  CHECK( findBandInfo( meta, 2 ) == nullptr );   // the hole: typed absence
  CHECK( findBandInfo( meta, 4 ) == nullptr );
  CHECK( findBandInfo( meta, 0 ) == nullptr );

  // Wrong-typed band members degrade to defaults (no throw).
  const RasterMetadata hostile = RasterMetadata::fromJson( parseJsonText( R"({
    "width": "wide",
    "bands": [ { "index": "one", "dtype": {} } ]
  })" ) );
  CHECK( hostile.width == 0 );
  REQUIRE( hostile.bands.size() == 1 );
  CHECK( hostile.bands[0].index == 0 );
}

TEST_CASE( "the local catalog walk skips hostile documents without aborting",
           "[io][fabric][catalog][json]" )
{
  const std::string dir = scratchDir( "catalog" );

  // A valid item the walk MUST still find.
  writeText( dir + "/item1.json", R"({
    "type": "Feature", "id": "item-1",
    "properties": { "datetime": "2026-01-01T00:00:00Z" },
    "assets": { "data": { "href": "data.tif", "roles": ["data"] } }
  })" );
  // Hostile shapes: a wrong-typed "type", and a catalog with hostile links.
  writeText( dir + "/hostile_type.json", R"({ "type": {}, "assets": {} })" );
  writeText( dir + "/catalog.json", R"({
    "type": "Catalog",
    "links": [
      { "rel": "item", "href": "item1.json" },
      { "rel": "item", "href": "hostile_type.json" },
      { "rel": {}, "href": "never.json" },
      { "rel": "item", "href": {} },
      "not-an-object"
    ]
  })" );

  const CatalogService service = openCatalogService( dir, CatalogServiceOptions {} );
  CatalogService::SearchAllResult result;
  CHECK_NOTHROW( result = service.searchAll( CatalogQuery {} ) );
  REQUIRE( result.records.size() == 1 );   // the healthy item survived
  CHECK( result.records.front().id == "item-1" );
}

TEST_CASE( "wrong-typed external JSON fails typed at the geospatial boundaries",
           "[io][fabric][json][oracle4]" )
{
  // RemoteSourceIdentity: a foreign-typed size_bytes is a typed refusal.
  CHECK_THROWS_AS( RemoteSourceIdentity::fromJson( parseJsonText(
                     R"({ "url": "https://x", "size_bytes": "big" })" ) ),
                   GeoError );
  // Wrong-typed optional strings degrade to absent (existing contract).
  RemoteSourceIdentity identity;
  CHECK_NOTHROW( identity = RemoteSourceIdentity::fromJson( parseJsonText( R"({ "url": 5 })" ) ) );
  CHECK( identity.url.empty() );
}

TEST_CASE( "atomic staging names keep their digit shape and mix in the pid",
           "[io][fabric][staging]" )
{
  const std::string dir = scratchDir( "staging" );
  const std::string target = dir + "/cube.tif";
  const std::string staged = atomic_fs::stagedPathFor( target );
  const std::string name = std::filesystem::path( staged ).filename().string();
  // "<stem>.<digits>.<digits>.tmp<ext>" — the shape the stage-ledger sweep
  // recognizes; the second digit group is the pid (#1056), not an unseeded
  // rand().
  const std::size_t tmp = name.find( ".tmp" );
  REQUIRE( tmp != std::string::npos );
  const std::string remainder = name.substr( tmp + 4 );
  CHECK( ( remainder.empty() || remainder.front() == '.' ) );
  const std::string body = name.substr( 0, tmp );
  const std::size_t lastDot = body.rfind( '.' );
  REQUIRE( lastDot != std::string::npos );
  const std::size_t firstDot = body.rfind( '.', lastDot - 1 );
  REQUIRE( firstDot != std::string::npos );
  const std::string counterGroup = body.substr( firstDot + 1, lastDot - firstDot - 1 );
  const std::string pidGroup = body.substr( lastDot + 1 );
  REQUIRE( !counterGroup.empty() );
  REQUIRE( !pidGroup.empty() );
  for ( const char c : counterGroup )
    CHECK( std::isdigit( static_cast<unsigned char>( c ) ) );
  for ( const char c : pidGroup )
    CHECK( std::isdigit( static_cast<unsigned char>( c ) ) );
  CHECK( name.substr( tmp ) == ".tmp.tif" );
  CHECK( atomic_fs::stagedPathFor( target ) != staged );   // names never repeat in-process
}
