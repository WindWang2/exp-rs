/***************************************************************************
  tests/test_r33_t03_prefetch.cpp — R33-T03 WP02/WP03: bounded prefetch
  stepping and honest accounting. A chunkWindow larger than the 4096-request
  materialize bound used to advance `begin` by the FULL window and silently
  skip every chunk in (4096, chunkWindow); outcomes past the retained
  1024-entry window were counted but never reported; cancellation first
  observed at a window boundary reported nothing.
 ***************************************************************************/

#include "geospatial/fabric/prefetch.h"
#include "geospatial/fabric/query_planner.h"
#include "geospatial/remote/range_cache.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace sicnu::geo;

namespace
{

std::string scratchDir( const char *name )
{
  std::string path = ( std::filesystem::temp_directory_path() / "sicnu_fabric_tests" /
                       ( std::string( "r33t03_" ) + name ) )
                       .string();
  std::filesystem::remove_all( path );
  std::filesystem::create_directories( path );
  return path;
}

/// A 65×65 scene on a matching explicit grid: with 1×1 chunks that is 4225
/// logical chunks — past the 4096-request materialize bound, so a window
/// larger than the bound has room to skip the middle.
FabricPlan chunkyPlan( const std::string &dir, const CubeChunkShape &shape,
                       std::uint64_t expectedTotal )
{
  const std::string scene = dir + "/scene.tif";
  {
    RasterWriter writer = RasterWriter::create(
      scene, 65, 65, { RasterBandSpec {} },
      { "GTiff", { "TILED=YES", "BLOCKXSIZE=16", "BLOCKYSIZE=16" }, true } );
    writer.setCrs( Crs::fromAuthid( "EPSG:4326" ) );
    writer.setGeotransform( { 0.0, 1.0, 0.0, 65.0, 0.0, -1.0 } );
    std::vector<double> raster( 65ull * 65 );
    for ( std::size_t i = 0; i < raster.size(); ++i )
      raster[i] = static_cast<double>( i % 97 );
    writer.writeWindow( 1, { 0, 0, 65, 65 }, raster.data() );
    writer.finalize();
  }

  AssetRecord record;
  record.id = "scene";
  record.path = scene;
  record.datetimeUtc = "2024-01-01T00:00:00Z";
  record.hasCloudCover = true;
  record.cloudCover = 0.0;
  record.hasBbox = true;
  record.minX = 0.0;
  record.minY = 0.0;
  record.maxX = 65.0;
  record.maxY = 65.0;
  record.mediaType = "image/tiff";
  record.roles = { "data" };

  FabricIntent intent;
  intent.records = { record };
  intent.sceneBudget = 1;
  intent.grid.explicitGrid = true;
  intent.grid.crs.valid = true;
  intent.grid.crs.authid = "EPSG:4326";
  intent.grid.scaleX = 1.0;
  intent.grid.scaleY = 1.0;
  intent.grid.minX = 0.0;
  intent.grid.minY = 0.0;
  intent.grid.maxX = 65.0;
  intent.grid.maxY = 65.0;
  intent.chunkShape = shape;

  const FabricPlan plan = planFabric( intent );
  REQUIRE( plan.chunkPlan().chunkCountTotal() == expectedTotal );
  return plan;
}

/// Complete conservation: every chunk lands in exactly one bucket — a
/// recorded outcome (retained vector entry or dropped counter) OR the
/// skipped-cancelled remainder (which is counted, never recorded per chunk).
void requireFullAccounting( const PrefetchReport &report, std::uint64_t total,
                            const char *context )
{
    CAPTURE( context, report.warmed, report.cacheHits, report.mirrorHits,
             report.skippedBudget, report.skippedCancel, report.failed,
             report.outcomesDropped, report.chunks.size() );
    std::set<std::uint64_t> seenIndices;
    for ( const PrefetchChunkOutcome &outcome : report.chunks )
    {
        INFO( context << ": duplicate chunk index " << outcome.index );
        REQUIRE( seenIndices.insert( outcome.index ).second );
        REQUIRE( outcome.index < total );
    }
    const std::uint64_t recorded = report.warmed + report.cacheHits +
                                   report.mirrorHits + report.skippedBudget +
                                   report.failed;
    REQUIRE( recorded + report.skippedCancel == total );
    REQUIRE( static_cast<std::uint64_t>( report.chunks.size() ) +
               report.outcomesDropped == recorded );
}

} // namespace

TEST_CASE( "prefetch steps every chunk when chunkWindow exceeds the materialize bound",
           "[io][fabric][prefetch][r33t03]" )
{
  const std::string dir = scratchDir( "step" );
  // 65×65 grid at 1×1 chunks = 4225 chunks. chunkWindow 8192 used to make
  // the walk visit [0,4096) and then jump to begin=8192 (past the total):
  // chunks 4096..4224 never received a terminal state.
  const FabricPlan plan = chunkyPlan( dir, { 1, 1, 1, 1 }, 4225 );

  RemoteRangeCache::install( {} );
  PrefetchOptions windowed;
  windowed.chunkWindow = 8192;
  const PrefetchReport walked = prefetchChunks( plan, windowed );
  RemoteRangeCache::uninstall();

  requireFullAccounting( walked, 4225, "window 8192" );
  CHECK( walked.failed == 0 );
  CHECK( walked.cacheHits == 4225 );   // local scene: nothing pulls origin bytes
  CHECK( walked.outcomesDropped == 4225 - 1024 );   // honest, was stuck at 0
}

TEST_CASE( "prefetch stepping stays exact for windows below the materialize bound",
           "[io][fabric][prefetch][r33t03]" )
{
  const std::string dir = scratchDir( "stepsmall" );
  const FabricPlan plan = chunkyPlan( dir, { 1, 1, 1, 1 }, 4225 );

  RemoteRangeCache::install( {} );
  PrefetchOptions windowed;
  windowed.chunkWindow = 1000;   // 5 batches: 1000×4 + 225
  const PrefetchReport walked = prefetchChunks( plan, windowed );
  RemoteRangeCache::uninstall();

  requireFullAccounting( walked, 4225, "window 1000" );
  CHECK( walked.failed == 0 );
  CHECK( walked.cacheHits == 4225 );
}

TEST_CASE( "prefetch cancel accounting conserves chunks wherever first observed",
           "[io][fabric][prefetch][r33t03]" )
{
  const std::string dir = scratchDir( "cancel" );
  // 25 chunks (5×5 spatial tiles), window 4 → 7 windows: cancellation
  // mid-walk and at window boundaries must both leave every chunk counted
  // exactly once — never double-counted, never silent.
  const FabricPlan plan = chunkyPlan( dir, { 1, 16, 16, 1 }, 25 );

  RemoteRangeCache::install( {} );

  SECTION( "cancelled before the walk starts" )
  {
    CancelToken preCancelled;
    preCancelled.cancel();
    const PrefetchReport report = prefetchChunks( plan, {}, preCancelled );
    requireFullAccounting( report, 25, "pre-cancelled" );
    CHECK( report.skippedCancel == 25 );
    CHECK( report.warmed + report.cacheHits == 0 );
  }

  SECTION( "cancelled mid-walk via an aliased predicate" )
  {
    CancelToken cancel;
    std::atomic<int> checks{ 0 };
    cancel.aliasPredicate( [ &checks ] {
      // Flip after 9 cancelled() checkpoints: the walk stops part-way and
      // the un-walked remainder must be reported exactly once.
      return checks.fetch_add( 1 ) >= 9;
    } );
    PrefetchOptions windowed;
    windowed.chunkWindow = 4;
    const PrefetchReport report = prefetchChunks( plan, windowed, cancel );
    requireFullAccounting( report, 25, "mid-walk" );
    CHECK( report.skippedCancel >= 1 );
    CHECK( report.skippedCancel <= 25 );
    CHECK( report.warmed + report.cacheHits >= 1 );
  }

  RemoteRangeCache::uninstall();
}
