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
#include "geospatial/remote/range_cache.h"
#include "geospatial/raster/raster_writer.h"
#include "support/http_range_server.h"

#include <catch2/catch_test_macros.hpp>

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
