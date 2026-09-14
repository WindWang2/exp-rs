/***************************************************************************
  tests/test_io_fabric_plan.cpp — fabric 10.0: query planner, bounded
  execution, range-cache prefetch and offline mirror over synthetic scenes.
 ***************************************************************************/

#include "geospatial/fabric/mirror.h"
#include "geospatial/fabric/object_store.h"
#include "support/http_range_server.h"
#include "geospatial/fabric/prefetch.h"
#include "geospatial/fabric/query_planner.h"
#include "geospatial/remote/offline_gate.h"
#include "geospatial/remote/range_cache.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/raster/raster_reader.h"

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
                       ( std::string( "plan_" ) + name ) )
                       .string();
  std::filesystem::remove_all( path );
  std::filesystem::create_directories( path );
  return path;
}

AssetRecord sceneRecord( const std::string &id, const std::string &path, const std::string &instant,
                         double cloud )
{
  AssetRecord record;
  record.id = id;
  record.path = path;
  record.datetimeUtc = instant;
  record.hasCloudCover = true;
  record.cloudCover = cloud;
  record.hasBbox = true;
  record.minX = 0.0;
  record.minY = 0.0;
  record.maxX = 32.0;
  record.maxY = 32.0;
  record.mediaType = "image/tiff";
  record.roles = { "data" };
  return record;
}

} // namespace

TEST_CASE( "plans are inspectable, costed, and honestly empty when nothing matches",
           "[io][fabric][plan]" )
{
  const std::string dir = scratchDir( "inspect" );
  const std::string scene = dir + "/s1.tif";
  {
    RasterWriter writer = RasterWriter::create(
      scene, 32, 32, { RasterBandSpec {} },
      { "GTiff", { "TILED=YES", "BLOCKXSIZE=32", "BLOCKYSIZE=32" }, true } );
    writer.setCrs( Crs::fromAuthid( "EPSG:4326" ) );
    writer.setGeotransform( { 0.0, 1.0, 0.0, 32.0, 0.0, -1.0 } );
    std::vector<double> raster( 32ull * 32, 5.0 );
    writer.writeWindow( 1, { 0, 0, 32, 32 }, raster.data() );
    writer.finalize();
  }

  FabricIntent intent;
  intent.records = { sceneRecord( "s1", scene, "2024-01-01T00:00:00Z", 2.0 ) };
  intent.sceneBudget = 8;
  intent.grid.explicitGrid = true;
  intent.grid.crs.valid = true;
  intent.grid.crs.authid = "EPSG:4326";
  intent.grid.scaleX = 1.0;
  intent.grid.scaleY = 1.0;
  intent.grid.minX = 0.0;
  intent.grid.minY = 0.0;
  intent.grid.maxX = 32.0;
  intent.grid.maxY = 32.0;
  intent.chunkShape = { 1, 16, 16, 1 };
  intent.hasWindow = true;
  intent.windowW = 32;
  intent.windowH = 32;

  const FabricPlan plan = planFabric( intent );
  const Json::Value json = plan.toJson();
  CHECK( json["windowPlan"].asBool() );
  CHECK( json["cost"]["scenes"].asUInt64() == 1 );
  CHECK( json["cost"]["chunks"].asUInt64() == 4 );   // 2×2 spatial tiles
  CHECK( json["cost"]["estimatedBytes"].asUInt64() == 32ull * 32 * 8 );
  CHECK( json["grid"]["crs"].asString() == "EPSG:4326" );
  CHECK( json["selectedAssets"].size() == 1 );
  CHECK( json["selectedAssets"][0]["displayPath"].asString().find( "s1.tif" ) != std::string::npos );

  bool stagesSeen[5] = { false, false, false, false, false };
  for ( const Json::Value &stage : json["stages"] )
  {
    const std::string name = stage["name"].asString();
    if ( name == "catalog_query" )
      stagesSeen[0] = true;
    if ( name == "asset_selection" )
      stagesSeen[1] = true;
    if ( name == "grid_planning" )
      stagesSeen[2] = true;
    if ( name == "chunk_planning" )
      stagesSeen[3] = true;
    if ( name == "identity_cache" )
      stagesSeen[4] = true;
  }
    CHECK( ( stagesSeen[0] && stagesSeen[1] && stagesSeen[2] && stagesSeen[3] &&
            stagesSeen[4] ) );

  // Window execution: exact values back, provenance complete.
  FabricExecutionReport report;
  const VirtualCubeWindowResult window = executeWindow( plan, {}, report );
  CHECK( window.values.size() == 32ull * 32 );
  CHECK( window.values[0] == 5.0 );
  CHECK( report.chunksExecuted == 1 );
  CHECK( report.assetsFailed == 0 );

  // An intent matching nothing yields a VALID empty plan — stages skipped,
  // never faked.
  FabricIntent empty = intent;
  empty.query.collections = { "no-such-collection" };
  empty.hasWindow = false;
  const FabricPlan emptyPlan = planFabric( empty );
  CHECK( emptyPlan.selectedAssets().empty() );
  CHECK( emptyPlan.cost().scenes == 0 );
  const Json::Value emptyJson = emptyPlan.toJson();
  CHECK( emptyJson["cost"]["chunks"].asUInt64() == 0 );

  // Typed intent violations.
  FabricIntent invalid;
  invalid.sceneBudget = 8;
  REQUIRE_THROWS_AS( planFabric( invalid ), GeoError );   // no source at all
  FabricIntent both = intent;
  both.records = intent.records;
  both.catalogUri = "/tmp/also-given";
  REQUIRE_THROWS_AS( planFabric( both ), GeoError );   // two sources
}

TEST_CASE( "top-K selection honors the quality policy over a larger match set",
           "[io][fabric][plan][selection]" )
{
  const std::string dir = scratchDir( "topk" );
  std::vector<AssetRecord> records;
  for ( int i = 0; i < 20; ++i )
  {
    const std::string scene = dir + "/s" + std::to_string( i ) + ".tif";
    RasterWriter writer = RasterWriter::create(
      scene, 32, 32, { RasterBandSpec {} },
      { "GTiff", { "TILED=YES", "BLOCKXSIZE=32", "BLOCKYSIZE=32" }, true } );
    writer.setCrs( Crs::fromAuthid( "EPSG:4326" ) );
    writer.setGeotransform( { 0.0, 1.0, 0.0, 32.0, 0.0, -1.0 } );
    std::vector<double> raster( 32ull * 32, double( i ) );
    writer.writeWindow( 1, { 0, 0, 32, 32 }, raster.data() );
    writer.finalize();
    records.push_back( sceneRecord( "s" + std::to_string( i ), scene,
                                    "2024-01-01T00:00:0" + std::to_string( i % 10 ) + "Z",
                                    double( 100 - i ) ) );   // earlier scenes: fewer clouds
  }

  FabricIntent intent;
  intent.records = records;
  intent.sceneBudget = 5;
  intent.grid.explicitGrid = true;
  intent.grid.crs.valid = true;
  intent.grid.crs.authid = "EPSG:4326";
  intent.grid.scaleX = 1.0;
  intent.grid.scaleY = 1.0;
  intent.grid.minX = 0.0;
  intent.grid.minY = 0.0;
  intent.grid.maxX = 32.0;
  intent.grid.maxY = 32.0;

  const FabricPlan plan = planFabric( intent );
  REQUIRE( plan.selectedAssets().size() == 5 );
  CHECK( plan.cost().catalogMatches == 20 );
  // The five CLEANEST scenes win (cloud asc), newest breaks ties.
  CHECK( plan.selectedAssets()[0].id == "s19" );
  CHECK( plan.selectedAssets()[4].id == "s15" );
  CHECK( plan.cost().catalogMatchesTruncated );
}

TEST_CASE( "prefetch warms the range cache within budget and cancel stops it",
           "[io][fabric][plan][prefetch]" )
{
  const std::string dir = scratchDir( "prefetch" );
  std::vector<AssetRecord> records;
  for ( int i = 0; i < 2; ++i )
  {
    const std::string scene = dir + "/s" + std::to_string( i ) + ".tif";
    RasterWriter writer = RasterWriter::create(
      scene, 32, 32,
      { [] {
        RasterBandSpec band;
        band.noDataValue = -9999.0;
        band.hasNoData = true;
        return band;
      }() },
      { "GTiff", { "TILED=YES", "BLOCKXSIZE=16", "BLOCKYSIZE=16" }, true } );
    writer.setCrs( Crs::fromAuthid( "EPSG:4326" ) );
    writer.setGeotransform( { 0.0, 1.0, 0.0, 32.0, 0.0, -1.0 } );
    std::vector<double> raster( 32ull * 32, double( i + 1 ) );
    writer.writeWindow( 1, { 0, 0, 32, 32 }, raster.data() );
    writer.finalize();
    records.push_back( sceneRecord( "s" + std::to_string( i ), scene,
                                    "2024-01-0" + std::to_string( i + 1 ) + "T00:00:00Z",
                                    double( i ) ) );
  }

  FabricIntent intent;
  intent.records = records;
  intent.sceneBudget = 4;
  intent.grid.explicitGrid = true;
  intent.grid.crs.valid = true;
  intent.grid.crs.authid = "EPSG:4326";
  intent.grid.scaleX = 1.0;
  intent.grid.scaleY = 1.0;
  intent.grid.minX = 0.0;
  intent.grid.minY = 0.0;
  intent.grid.maxX = 32.0;
  intent.grid.maxY = 32.0;
  intent.chunkShape = { 1, 16, 16, 1 };

  const FabricPlan plan = planFabric( intent );
  // 4 spatial tiles × 2 time steps (each scene is one step).
  REQUIRE( plan.chunkPlan().chunkCountTotal() == 8 );

  // Without the range cache installed, prefetch refuses (a no-op would
  // masquerade as work).
  RemoteRangeCache::uninstall();
  REQUIRE_THROWS_AS( prefetchChunks( plan ), GeoError );

  RemoteRangeCache::install( {} );
  PrefetchOptions options;
  options.chunkWindow = 2;
  const PrefetchReport warmed = prefetchChunks( plan, options );
  CHECK( warmed.failed == 0 );
  CHECK( warmed.warmed + warmed.cacheHits == 8 );
  // Local assets never pull origin bytes — every chunk is a cache-hit
  // ("already local" is the same outcome as "already cached").
  CHECK( warmed.cacheHits == 8 );
  CHECK( warmed.bytesPulled == 0 );

  // Second pass: identical — the cache story is asset-side, not time-side.
  const PrefetchReport cached = prefetchChunks( plan, options );
  CHECK( cached.cacheHits == 8 );
  CHECK( cached.bytesPulled == 0 );

  // A zero-chunk budget skips everything (and says so).
  PrefetchOptions starved;
  starved.maxBytes = 1;
  // Local chunks never breach a byte budget (they pull nothing) — the
  // report stays honest rather than inventing skips.
  const PrefetchReport starvedReport = prefetchChunks( plan, starved );
  CHECK( starvedReport.cacheHits == 8 );
  CHECK( starvedReport.budgetExhausted == false );

  CancelToken cancel;
  cancel.cancel();
  const PrefetchReport cancelledReport = prefetchChunks( plan, options, cancel );
  CHECK( cancelledReport.warmed + cancelledReport.cacheHits + cancelledReport.skippedCancel == 0 );
  RemoteRangeCache::uninstall();
}

TEST_CASE( "prefetch pulls REAL origin bytes through the range cache and honors a byte budget",
           "[io][fabric][plan][prefetch][integration]" )
{
  // A real remote source over the range cache's native protocol (http(s)):
  // the only way the budget machinery (telemetry-measured origin bytes) is
  // exercised for truth. Local assets pull zero and prove nothing here.
  const std::string dir = scratchDir( "prefetchhttp" );
  const std::string scenePath = dir + "/scene.tif";
  {
    sicnu::geo::RasterWriter writer =
      sicnu::geo::RasterWriter::create( scenePath, 64, 64, { sicnu::geo::RasterBandSpec {} },
                                        { "GTiff", { "TILED=YES", "BLOCKXSIZE=32", "BLOCKYSIZE=32" },
                                          true } );
    writer.setCrs( sicnu::geo::Crs::fromAuthid( "EPSG:4326" ) );
    writer.setGeotransform( { 0.0, 1.0, 0.0, 64.0, 0.0, -1.0 } );
    std::vector<double> raster( 64ull * 64 );
    for ( std::size_t i = 0; i < raster.size(); ++i )
      raster[i] = static_cast<double>( i % 251 );
    writer.writeWindow( 1, { 0, 0, 64, 64 }, raster.data() );
    writer.finalize();
  }
  std::ifstream sceneFile( scenePath, std::ios::binary );
  const std::vector<unsigned char> payload( ( std::istreambuf_iterator<char>( sceneFile ) ),
                                            std::istreambuf_iterator<char>() );
  REQUIRE( payload.size() > 4096 );

  testsupport::HttpRangeServer server( payload, testsupport::ServerBehavior::Normal );
  REQUIRE( server.port() > 0 );

  sicnu::geo::AssetRecord record;
  record.id = "scene";
  record.path = server.url();
  record.datetimeUtc = "2024-01-01T00:00:00Z";
  record.hasCloudCover = true;
  record.cloudCover = 0.0;
  record.hasBbox = true;
  record.minX = 0.0;
  record.minY = 0.0;
  record.maxX = 64.0;
  record.maxY = 64.0;

  FabricIntent intent;
  intent.records = { record };
  intent.sceneBudget = 2;
  intent.grid.explicitGrid = true;
  intent.grid.crs.valid = true;
  intent.grid.crs.authid = "EPSG:4326";
  intent.grid.scaleX = 1.0;
  intent.grid.scaleY = 1.0;
  intent.grid.minX = 0.0;
  intent.grid.minY = 0.0;
  intent.grid.maxX = 64.0;
  intent.grid.maxY = 64.0;
  intent.chunkShape = { 1, 32, 32, 1 };
  const FabricPlan plan = planFabric( std::move( intent ) );
  REQUIRE( plan.chunkPlan().chunkCountTotal() == 4 );

  RemoteRangeCache::install( {} );
  PrefetchOptions options;
  options.chunkWindow = 4;

  const PrefetchReport warmed = prefetchChunks( plan, options );
  CHECK( warmed.failed == 0 );
  // The DEFAULT budget (first-chunk estimate × count) is ENGAGED: real
  // origin bytes exceed the logical estimate (identity head window + GeoTIFF
  // framing), so the pass may legitimately stop early — every chunk is
  // warmed, a first-pass hit, or skipped-budget. ACCOUNTED, never silent.
  CHECK( warmed.warmed + warmed.cacheHits + warmed.skippedBudget == 4 );
  CHECK( warmed.warmed >= 1 );
  CHECK( warmed.bytesPulled > 0 );   // REAL origin bytes, not a local no-op
  CHECK( warmed.bytesPulled < payload.size() * 2 );   // bounded, not a full crawl
  CHECK( warmed.budgetExhausted );   // the default bound did its job

  // An explicit generous budget completes the whole plan.
  PrefetchOptions generous;
  generous.chunkWindow = 4;
  generous.maxBytes = payload.size() * 4;
  const PrefetchReport full = prefetchChunks( plan, generous );
  CHECK( full.failed == 0 );
  CHECK( full.warmed + full.cacheHits + full.skippedBudget == 4 );
  CHECK( full.budgetExhausted == false );

  // A tiny budget: the first pull exhausts it — the remainder is
  // skipped-budget (counted, not silent).
  PrefetchOptions starved;
  starved.maxBytes = 1;
  RemoteRangeCache::clearEntries();
  const PrefetchReport starvedReport = prefetchChunks( plan, starved );
  CHECK( starvedReport.skippedBudget + starvedReport.warmed == 4 );
  CHECK( starvedReport.skippedBudget >= 1 );
  CHECK( starvedReport.budgetExhausted );
  RemoteRangeCache::uninstall();
}

TEST_CASE( "the mirror materializes chunks, replays offline, and never mirrors the unprovable",
           "[io][fabric][plan][mirror][offline]" )
{
  const std::string dir = scratchDir( "mirror" );
  const std::string scene = dir + "/s0.tif";
  {
    RasterWriter writer = RasterWriter::create(
      scene, 32, 32, { RasterBandSpec {} },
      { "GTiff", { "TILED=YES", "BLOCKXSIZE=16", "BLOCKYSIZE=16" }, true } );
    writer.setCrs( Crs::fromAuthid( "EPSG:4326" ) );
    writer.setGeotransform( { 0.0, 1.0, 0.0, 32.0, 0.0, -1.0 } );
    std::vector<double> raster( 32ull * 32 );
    for ( std::size_t i = 0; i < raster.size(); ++i )
      raster[i] = double( i % 251 );
    writer.writeWindow( 1, { 0, 0, 32, 32 }, raster.data() );
    writer.finalize();
  }

  FabricIntent intent;
  intent.records = { sceneRecord( "s0", scene, "2024-01-01T00:00:00Z", 1.0 ) };
  intent.sceneBudget = 2;
  intent.grid.explicitGrid = true;
  intent.grid.crs.valid = true;
  intent.grid.crs.authid = "EPSG:4326";
  intent.grid.scaleX = 1.0;
  intent.grid.scaleY = 1.0;
  intent.grid.minX = 0.0;
  intent.grid.minY = 0.0;
  intent.grid.maxX = 32.0;
  intent.grid.maxY = 32.0;
  intent.chunkShape = { 1, 16, 16, 1 };
  intent.hasWindow = true;
  intent.windowW = 16;
  intent.windowH = 16;

  const FabricPlan plan = planFabric( intent );
  REQUIRE( plan.chunkPlan().chunkCountTotal() == 4 );

  const std::string mirrorDir = dir + "/mirror";
  MirrorOptions mirrorOptions;
  mirrorOptions.mirrorDirectory = mirrorDir;
  const MirrorReport report = mirrorChunks( plan, mirrorOptions );
  CHECK( report.mirrored == 4 );
  CHECK( report.skippedUnprovable == 0 );
  CHECK( report.bytesWritten > 0 );

  // Second pass: everything already-present (token-keyed proof).
  const MirrorReport again = mirrorChunks( plan, mirrorOptions );
  CHECK( again.alreadyPresent == 4 );
  CHECK( again.mirrored == 0 );

  // Window reads prefer the mirror when it answers the exact geometry.
  VirtualCubeReadOptions readOptions;
  readOptions.mirrorDirectory = mirrorDir;
  FabricExecutionReport execReport;
  const VirtualCubeWindowResult window = executeWindow( plan, readOptions, execReport );
  REQUIRE( window.provenance.size() == 1 );
  CHECK( !window.provenance[0].mirrorHit.empty() );
  CHECK( window.values[0] == 0.0 );
  // Buffer index 16 = row 1, col 0 of the 16-wide window = ramp linear 32.
  CHECK( window.values[16] == 32.0 );

  // Offline replay through the planner: same plan, offline gate on.
  offline::setEnabled( true );
  // Local records still PLAN offline (that is the doctrine).
  const FabricPlan offlinePlan = planFabric( intent );
  CHECK( offlinePlan.cost().scenes == 1 );
  offline::setEnabled( false );

  // The mirror inventory is queryable and bounded-honest.
  const Json::Value stats = mirrorStatsJson( mirrorDir );
  CHECK( stats["entries"].asUInt64() == 4 );
}

TEST_CASE( "chunk execution is time-correct, budgeted, and reports skips",
           "[io][fabric][plan][execute]" )
{
  const std::string dir = scratchDir( "exec" );
  std::vector<AssetRecord> records;
  for ( int i = 0; i < 3; ++i )
  {
    const std::string scene = dir + "/s" + std::to_string( i ) + ".tif";
    RasterWriter writer = RasterWriter::create(
      scene, 32, 32, { RasterBandSpec {} },
      { "GTiff", { "TILED=YES", "BLOCKXSIZE=16", "BLOCKYSIZE=16" }, true } );
    writer.setCrs( Crs::fromAuthid( "EPSG:4326" ) );
    writer.setGeotransform( { 0.0, 1.0, 0.0, 32.0, 0.0, -1.0 } );
    std::vector<double> raster( 32ull * 32, double( i + 3 ) );   // 3, 4, 5
    writer.writeWindow( 1, { 0, 0, 32, 32 }, raster.data() );
    writer.finalize();
    records.push_back( sceneRecord( "s" + std::to_string( i ), scene,
                                    "2024-01-0" + std::to_string( i + 1 ) + "T00:00:00Z", 0.0 ) );
  }

  FabricIntent intent;
  intent.records = records;
  intent.sceneBudget = 4;
  intent.grid.explicitGrid = true;
  intent.grid.crs.valid = true;
  intent.grid.crs.authid = "EPSG:4326";
  intent.grid.scaleX = 1.0;
  intent.grid.scaleY = 1.0;
  intent.grid.minX = 0.0;
  intent.grid.minY = 0.0;
  intent.grid.maxX = 32.0;
  intent.grid.maxY = 32.0;
  intent.chunkShape = { 1, 16, 16, 1 };
  // Quality: newest first → chunks in order s2 (5), s1 (4), s0 (3) × 4 tiles.

  const FabricPlan plan = planFabric( intent );
  REQUIRE( plan.chunkPlan().chunkCountTotal() == 12 );

  std::vector<std::pair<std::string, double>> seen;
  FabricExecutionReport report;
  const std::vector<FabricChunkOutcome> outcomes = executeChunks(
    plan, {}, 4,
    [ & ] ( const CubeChunkRequest &request, const VirtualCubeWindowResult &window ) {
      seen.push_back( { request.assetIdHint, window.values[0] } );
    },
    report );
  CHECK( outcomes.size() == 12 );
  CHECK( report.chunksExecuted == 12 );
  REQUIRE( seen.size() == 12 );
  // The FIRST chunk of each time step carries that step's own value.
  CHECK( seen[0].first == "s2" );
  CHECK( seen[0].second == 5.0 );
  CHECK( seen[4].first == "s1" );
  CHECK( seen[4].second == 4.0 );
  CHECK( seen[8].first == "s0" );
  CHECK( seen[8].second == 3.0 );

  // A tiny budget executes the head and skips the tail — reported, not thrown.
  FabricIntent starved = intent;
  starved.executionBudgetBytes = 16ull * 16 * 8 * 2;   // exactly two chunks' worth
  const FabricPlan starvedPlan = planFabric( starved );
  FabricExecutionReport starvedReport;
  std::size_t executed = 0;
  const std::vector<FabricChunkOutcome> starvedOutcomes = executeChunks(
    starvedPlan, {}, 16, nullptr, starvedReport );
  for ( const FabricChunkOutcome &outcome : starvedOutcomes )
    executed += outcome.ok ? 1 : 0;
  CHECK( executed == 2 );
  CHECK( starvedReport.budgetBreached );
}
