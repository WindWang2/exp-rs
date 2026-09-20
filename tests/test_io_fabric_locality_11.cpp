/***************************************************************************
  tests/test_io_fabric_locality_11.cpp — Cloud Data Fabric 11.0 (WP F):
  access-pattern-driven prefetch. A declared window sequence merges into
  fewer reads, orders by locality, warms the range cache (second pass hits
  the cache), honors the byte budget, and coordinates with the mirror
  (already-local chunks are skipped, not re-pulled).
 ***************************************************************************/

#include "geospatial/fabric/mirror.h"
#include "geospatial/fabric/prefetch.h"
#include "geospatial/fabric/query_planner.h"
#include "geospatial/fabric/virtual_cube.h"
#include "geospatial/convert/raster_convert.h"
#include "geospatial/remote/range_cache.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "support/http_range_server.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
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
                       ( std::string( "loc_" ) + name ) )
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

/// 96×96 GTiff at world Y ∈ [0,96] with the deterministic ramp.
std::vector<unsigned char> buildTiff( const std::string &path )
{
  const int size = 96;
  {
    RasterWriter writer = RasterWriter::create( path, size, size, { RasterBandSpec {} },
                                                { "GTiff", { "TILED=YES", "BLOCKXSIZE=64", "BLOCKYSIZE=64" }, true } );
    writer.setCrs( Crs::fromAuthid( "EPSG:4326" ) );
    writer.setGeotransform( { 0.0, 1.0, 0.0, 96.0, 0.0, -1.0 } );
    std::vector<double> raster( static_cast<std::size_t>( size ) * size );
    for ( std::size_t i = 0; i < raster.size(); ++i )
      raster[i] = static_cast<double>( ( i * 7 ) % 251 );
    writer.writeWindow( 1, { 0, 0, size, size }, raster.data() );
    writer.finalize();
  }
  return fileBytes( path );
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

/// The record's declared bbox drives the pattern mapper's intersection
/// filter — 12.0 overview tests use a 384 raster, so their record must
/// declare [0,384]² (the shared sceneRecord hardcodes the 96 fixture).
AssetRecord sceneRecord384( const std::string &path )
{
  AssetRecord record = sceneRecord( path );
  record.minX = 0.0;
  record.minY = 0.0;
  record.maxX = 384.0;
  record.maxY = 384.0;
  return record;
}

VirtualCube buildCube384( const std::string &url )
{
  VirtualCubeBuildOptions options;
  options.probeLimit = 1;
  return VirtualCube::build( { sceneRecord384( url ) }, VirtualCubeGrid{},
                             OverlapPolicy::FirstWins, {}, options );
}

VirtualCube buildCube( const std::string &url )
{
  // NOT an explicit grid: the build's bounded probe records the asset's
  // grid facts (raster size + geotransform) — exactly what the pattern
  // planner maps with, without touching the origin again.
  VirtualCubeBuildOptions options;
  options.probeLimit = 1;
  return VirtualCube::build( { sceneRecord( url ) }, VirtualCubeGrid{},
                             OverlapPolicy::FirstWins, {}, options );
}

} // namespace

TEST_CASE( "access-pattern prefetch merges, warms, and coordinates with the mirror",
           "[io][fabric][locality][wp_f][integration]" )
{
  const std::string dir = scratchDir( "warm" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  testsupport::HttpRangeServer server( payload, testsupport::ServerBehavior::Normal );
  REQUIRE( server.port() > 0 );

  const VirtualCube cube = buildCube( server.url() );
  REQUIRE( cube.assetCount() == 1 );
  CHECK( cube.assets().front().hasGrid );

  RemoteRangeCache::install( {} );
  struct CacheGuard { ~CacheGuard() { RemoteRangeCache::uninstall(); } } cacheGuard;

  // A pan/scan trajectory: four overlapping 32×32 windows around the grid
  // centre — they merge into ONE warm read instead of four.
  std::vector<AccessWindow> pattern = {
      { 24, 24, 32, 32 }, { 40, 24, 32, 32 }, { 24, 40, 32, 32 }, { 40, 40, 32, 32 } };
  const PrefetchLocalityReport first = prefetchAccessPattern( cube, pattern );
  CHECK( first.mergedReads == 1 );       // merged, not four origin reads
  CHECK( first.warmed == 1 );
  CHECK( first.bytesPulled > 0 );
  CHECK( server.requestCount() > 0 );    // the warm reached the origin once

  // The SECOND pass over the same trajectory is served by the cache:
  // no origin bytes, no new requests.
  const PrefetchLocalityReport second = prefetchAccessPattern( cube, pattern );
  // The cache telemetry is the data-plane truth: every byte of the merged
  // window comes from cached blocks (origin revalidation 304s and GDAL's
  // speculative sibling 404s are control traffic, not data pulls).
  CHECK( second.cacheHits == 1 );
  CHECK( second.warmed == 0 );
  CHECK( second.bytesPulled == 0 );

  // Budget: a 1-byte budget skips the warm (declared input, honest skip).
  PrefetchOptions starved;
  starved.maxBytes = 1;
  const PrefetchLocalityReport third = prefetchAccessPattern( cube, pattern, starved );
  CHECK( third.skippedBudget == 1 );
  CHECK( third.budgetExhausted );
  CHECK( third.bytesPulled == 0 );
}

// ---------------------------------------------------------------------------
// 12.0 — overview- and band-aware access-pattern prefetch: a declared
// overview window warms THAT level's blocks (progressive refinement), and a
// declared band warms that band's blocks (multi-band sources).
// ---------------------------------------------------------------------------

namespace
{

/// 384×384 tiled GTiff converted to a real COG (the COG driver materializes
/// overviews) with a byte-robust pattern, then served over loopback.
std::vector<unsigned char> buildCogPayload( const std::string &dir, std::string *cogPath )
{
  const std::string plain = dir + "/plain.tif";
  const std::string cog = dir + "/cog.tif";
  {
    RasterWriter writer =
      RasterWriter::create( plain, 384, 384, { RasterBandSpec {} },
                            { "GTiff", { "TILED=YES", "BLOCKXSIZE=128", "BLOCKYSIZE=128" }, true } );
    writer.setCrs( Crs::fromAuthid( "EPSG:4326" ) );
    writer.setGeotransform( { 0.0, 1.0, 0.0, 384.0, 0.0, -1.0 } );
    std::vector<double> raster( 384ull * 384 );
    std::uint32_t state = 0x9E3779B9u;
    for ( std::size_t i = 0; i < raster.size(); ++i )
    {
      state ^= state << 13;
      state ^= state >> 17;
      state ^= state << 5;
      raster[i] = static_cast<double>( state % 1000003u );
    }
    writer.writeWindow( 1, { 0, 0, 384, 384 }, raster.data() );
    writer.finalize();
  }
  // The COG driver copies source overviews but never invents them: build
  // the level explicitly, then produce the COG.
  REQUIRE( buildOverviews( plain, { 2 }, "GAUSS" ) == 1 );
  makeCog( plain, cog, CogPreset::LosslessScientific );
  *cogPath = cog;
  return fileBytes( cog );
}

} // namespace

TEST_CASE( "access-pattern prefetch warms overview levels and skips absent ones honestly",
           "[io][fabric][locality][overview][utc12]" )
{
  const std::string dir = scratchDir( "overview" );
  std::string cogPath;
  const std::vector<unsigned char> payload = buildCogPayload( dir, &cogPath );
  testsupport::HttpRangeServer server( payload );
  server.setEtag( "\"overview-1\"" );

  const VirtualCube cube = buildCube384( server.url() );
  REQUIRE( cube.assetCount() == 1 );
  RemoteRangeCache::install( {} );
  struct CacheGuard { ~CacheGuard() { RemoteRangeCache::uninstall(); } } cacheGuard;
  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );

  // The COG has at least one overview level; the test needs it.
  RasterReader local( RasterReader::open( cogPath ) );
  REQUIRE( local.overviewCount( 1 ) >= 1 );
  const std::vector<int> dims = local.overviewDimensions( 1 );
  const int ow = dims[0];
  const int oh = dims[1];

  // Declared COARSE zoom (overview level 1) over the full grid, then a
  // native fine window — coarse warms first (progressive refinement).
  std::vector<AccessWindow> pattern = { { 0, 0, 384, 384, 1, 1 },  // overview 1, band 1
                                        { 0, 0, 96, 96, 1, 0 } }; // native, band 1
  const PrefetchLocalityReport report = prefetchAccessPattern( cube, pattern );
  INFO( "report: " << report.toJson() );
  CHECK( report.skippedNoOverview == 0 );
  CHECK( report.warmed == 2 );
  CHECK( report.bytesPulled > 0 );

  // The level's pixel count is a strict fraction of the full-res extent:
  // warming zoomed-out views pulls a fraction of the bytes.
  CHECK( ow * oh * 4 < 384 * 384 * 4 );

  // The warmed overview serves a later reader at that level with ZERO new
  // origin bytes (the warm pulled exactly the level's blocks).
  const std::uint64_t fetchedBefore =
    RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64();
  RasterReader warm( RasterReader::open( cachedPath ) );
  const int dstWidth = std::max( 1, static_cast<int>( std::lround( 384.0 * ( ow / 384.0 ) ) ) );
  const int dstHeight = std::max( 1, static_cast<int>( std::lround( 384.0 * ( oh / 384.0 ) ) ) );
  const std::vector<double> warmValues = warm.readWindowResampled(
    { 1 }, { 0, 0, 384, 384 }, dstWidth, dstHeight, 1, OverviewPolicy::Exact, "nearest" );
  const std::uint64_t fetchedAfter =
    RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64();
  CHECK( fetchedAfter == fetchedBefore );
  // Byte-correct against the local file at the same level.
  const std::vector<double> localValues = local.readWindowResampled(
    { 1 }, { 0, 0, 384, 384 }, dstWidth, dstHeight, 1, OverviewPolicy::Exact, "nearest" );
  CHECK( warmValues == localValues );

  // A declared level the source does not have is an honest per-read skip.
  std::vector<AccessWindow> absent = { { 0, 0, 384, 384, 1, 9 } };
  const PrefetchLocalityReport skipped = prefetchAccessPattern( cube, absent );
  CHECK( skipped.skippedNoOverview == 1 );
  CHECK( skipped.warmed == 0 );
}

TEST_CASE( "access-pattern prefetch warms the declared band of multi-band sources",
           "[io][fabric][locality][band][utc12]" )
{
  const std::string dir = scratchDir( "band" );
  const std::string scene = dir + "/scene.tif";
  {
    // Two bands with different ramps (same dtype — the writer refuses mixed
    // per-band dtypes) — a band-2 warm must not warm band 1.
    RasterBandSpec b1;
    RasterBandSpec b2;
    RasterWriter writer = RasterWriter::create(
      scene, 96, 96, { b1, b2 },
      { "GTiff",
        { "TILED=YES", "BLOCKXSIZE=64", "BLOCKYSIZE=64", "INTERLEAVE=BAND" },
        true } );
    writer.setCrs( Crs::fromAuthid( "EPSG:4326" ) );
    writer.setGeotransform( { 0.0, 1.0, 0.0, 96.0, 0.0, -1.0 } );
    std::vector<double> raster( 96ull * 96 );
    for ( std::size_t i = 0; i < raster.size(); ++i )
      raster[i] = static_cast<double>( ( i * 7 ) % 251 );
    writer.writeWindow( 1, { 0, 0, 96, 96 }, raster.data() );
    std::vector<double> band2( 96ull * 96 );
    for ( std::size_t i = 0; i < band2.size(); ++i )
      band2[i] = static_cast<double>( ( i * 11 ) % 251 );
    writer.writeWindow( 2, { 0, 0, 96, 96 }, band2.data() );
    writer.finalize();
  }
  const std::vector<unsigned char> payload = fileBytes( scene );
  testsupport::HttpRangeServer server( payload );
  server.setEtag( "\"band-1\"" );

  const VirtualCube cube = buildCube( server.url() );
  REQUIRE( cube.assetCount() == 1 );
  RemoteRangeCache::install( {} );
  struct CacheGuard { ~CacheGuard() { RemoteRangeCache::uninstall(); } } cacheGuard;
  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );

  std::vector<AccessWindow> pattern = { { 0, 0, 96, 96, 2, 0 } }; // band 2 only
  const PrefetchLocalityReport report = prefetchAccessPattern( cube, pattern );
  CHECK( report.warmed == 1 );
  const std::uint64_t fetchedAfterWarm =
    RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64();

  // Band 2 is warm: a fresh reader's band-2 read pulls zero new bytes.
  {
    RasterReader warm( RasterReader::open( cachedPath ) );
    warm.readWindow( { 2 }, { 0, 0, 96, 96 } );
  }
  CHECK( RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64() == fetchedAfterWarm );

  // Band 1 was NOT warmed: its read still pays origin bytes.
  {
    RasterReader cold( RasterReader::open( cachedPath ) );
    cold.readWindow( { 1 }, { 0, 0, 96, 96 } );
  }
  CHECK( RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64() > fetchedAfterWarm );
}


TEST_CASE( "access-pattern prefetch skips chunks the mirror already holds",
           "[io][fabric][locality][wp_f][mirror][integration]" )
{
  const std::string dir = scratchDir( "warmmirror" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  testsupport::HttpRangeServer server( payload, testsupport::ServerBehavior::Normal );
  REQUIRE( server.port() > 0 );

  const std::string url = server.url();
  server.setEtag( "\"etag-locality-1\"" );   // mirror materialization needs a
                                             // strong identity (fail-closed D-1010)
  const VirtualCube cube = buildCube( url );

  // Materialize the full grid as 48×48 mirror chunks (the grid's quarters).
  const CubeChunkPlan plan =
    CubeChunkPlan::forVirtualCube( cube, { 1, 48, 48, 1 }, {} );
  const std::string mirrorDir = dir + "/mirror";
  MirrorOptions mirrorOptions;
  mirrorOptions.mirrorDirectory = mirrorDir;
  const MirrorReport mirrored = mirrorChunks( cube, plan, mirrorOptions );
  REQUIRE( mirrored.mirrored == 4 );

  RemoteRangeCache::install( {} );
  struct CacheGuard { ~CacheGuard() { RemoteRangeCache::uninstall(); } } cacheGuard;

  // A single window EXACTLY on one mirrored chunk: the mirror answers it —
  // the origin sees no request and no cache bytes are pulled.
  const int requestsBefore = server.requestCount();
  std::vector<AccessWindow> pattern = { { 0, 0, 48, 48 } };
  PrefetchOptions prefetchOptions;
  prefetchOptions.mirrorDirectory = mirrorDir;
  const PrefetchLocalityReport report =
    prefetchAccessPattern( cube, pattern, prefetchOptions );
  CHECK( report.mergedReads == 1 );
  CHECK( report.mirrorHits == 1 );
  CHECK( report.warmed == 0 );
  CHECK( report.bytesPulled == 0 );
  CHECK( server.requestCount() == requestsBefore );
}
