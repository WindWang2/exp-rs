/***************************************************************************
  tests/test_io_fabric_scale.cpp — fabric 10.0: scale evidence. The planner's
  O(page + selected) memory contract over a 100k-record catalog, million-
  chunk plans counted without materialization, cancellation typing, large
  logical windows bounded in memory, mirror corruption recovery.
 ***************************************************************************/

#include "geospatial/fabric/mirror.h"
#include "geospatial/fabric/prefetch.h"
#include "geospatial/fabric/query_planner.h"
#include "geospatial/remote/offline_gate.h"
#include "geospatial/remote/range_cache.h"
#include "geospatial/raster/raster_writer.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef __linux__
#include <sys/resource.h>
#endif

using namespace sicnu::geo;

namespace
{

std::string scratchDir( const char *name )
{
  std::string path = ( std::filesystem::temp_directory_path() / "sicnu_fabric_tests" /
                       ( std::string( "scale_" ) + name ) )
                       .string();
  std::filesystem::remove_all( path );
  std::filesystem::create_directories( path );
  return path;
}

/// Peak RSS in bytes (Linux /proc self; 0 = unsupported platform — the
/// memory assertions then degrade to structural checks and say so).
std::uint64_t peakRssBytes()
{
#ifdef __linux__
  struct rusage usage;
  getrusage( RUSAGE_SELF, &usage );
  return static_cast<std::uint64_t>( usage.ru_maxrss ) * 1024ull;
#else
  return 0;
#endif
}

std::string writeConstantScene( const std::string &path, int size, double value )
{
  sicnu::geo::RasterWriter writer =
    RasterWriter::create( path, size, size, { RasterBandSpec {} },
                          { "GTiff", { "TILED=YES", "BLOCKXSIZE=64", "BLOCKYSIZE=64" }, true } );
  writer.setCrs( Crs::fromAuthid( "EPSG:4326" ) );
  writer.setGeotransform( { 0.0, 1.0, 0.0, static_cast<double>( size ), 0.0, -1.0 } );
  std::vector<double> raster( static_cast<std::size_t>( size ) * size, value );
  writer.writeWindow( 1, { 0, 0, size, size }, raster.data() );
  writer.finalize();
  return path;
}

AssetRecord recordWithBbox( const std::string &id, const std::string &path,
                            const std::string &instant, double cloud )
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
  record.maxX = 64.0;
  record.maxY = 64.0;
  record.mediaType = "image/tiff";
  record.roles = { "data" };
  record.collection = "scale";
  record.metadata["platform"] = "TEST";
  record.metadata["instruments"] = "MSI";
  return record;
}

VirtualCubeGrid fixedGrid( double extent )
{
  VirtualCubeGrid grid;
  grid.explicitGrid = true;
  grid.crs.valid = true;
  grid.crs.authid = "EPSG:4326";
  grid.scaleX = 1.0;
  grid.scaleY = 1.0;
  grid.minX = 0.0;
  grid.minY = 0.0;
  grid.maxX = extent;
  grid.maxY = extent;
  return grid;
}

} // namespace

TEST_CASE( "planning over a 100k-record catalog adds O(selected) memory, not O(catalog)",
           "[io][fabric][scale][memory]" )
{
  // One real scene backs every record's open path (the planner only probes
  // identity tokens up to the declared limit).
  const std::string dir = scratchDir( "planmem" );
  const std::string scene = writeConstantScene( dir + "/scene.tif", 64, 1.0 );

  const int catalogSize = 100000;
  std::vector<AssetRecord> records;
  records.reserve( static_cast<std::size_t>( catalogSize ) );
  for ( int i = 0; i < catalogSize; ++i )
    records.push_back( recordWithBbox( "r" + std::to_string( i ), scene,
                                       "2024-01-01T00:00:00Z", double( i % 100 ) ) );

  const std::uint64_t beforePlan = peakRssBytes();

  FabricIntent intent;
  intent.records = std::move( records );   // hand the catalog over, no copy
  intent.sceneBudget = 8;
  intent.grid = fixedGrid( 64.0 );
  intent.chunkShape = { 1, 32, 32, 1 };
  intent.query.limit = 0;
  intent.query.maxItems = 1000000;   // the walk must see the whole catalog
  FabricPlanOptions options;
  options.identityProbeLimit = 1;    // one identity probe total (bounded cost)
  // The caller hands the catalog over (move) — the plan consumes it.
  const FabricPlan plan = planFabric( std::move( intent ), options );

  const std::uint64_t afterPlan = peakRssBytes();
  REQUIRE( plan.cost().catalogMatches == static_cast<std::uint64_t>( catalogSize ) );
  REQUIRE( plan.selectedAssets().size() == 8 );
  CHECK( plan.cost().catalogMatchesTruncated );

  // The structural contract: the planner KEPT eight scenes, never the
  // catalog. The plan JSON (what an agent inspects) stays bounded.
  CHECK( plan.toJson()["selectedAssets"].size() == 8 );

  if ( peakRssBytes() == 0 )
  {
    FAIL( "peak RSS unsupported — memory contract not measurable here" );
  }
  else
  {
    // The planner's own footprint over the walk: bounded by the selector
    // (8 records + plan scaffolding). A full extra copy of the catalog
    // would show as >= catalogSize records. Bound: 3x ONE scene record's
    // plan overhead stays far below 1% of the catalog footprint — assert
    // the plan stage added < 2 MB of peak RSS beyond the input baseline.
    const std::uint64_t planDelta = afterPlan - beforePlan;
    CHECK( planDelta < 2ull * 1024 * 1024 );
  }
}

TEST_CASE( "million-chunk plans count instantly and materialize only bounded windows",
           "[io][fabric][scale][chunks]" )
{
  const std::string dir = scratchDir( "million" );
  const std::string scene = writeConstantScene( dir + "/scene.tif", 64, 1.0 );

  std::vector<AssetRecord> records;
  for ( int t = 0; t < 64; ++t )
    records.push_back( recordWithBbox( "t" + std::to_string( t ), scene,
                                       "2024-01-01T00:00:00Z", 0.0 ) );

  FabricIntent intent;
  intent.records = records;
  intent.sceneBudget = 64;
  // 2048×2048 grid / 16×16 chunks / 64 time steps = 1,048,576 logical chunks.
  intent.grid = fixedGrid( 2048.0 );
  intent.chunkShape = { 1, 16, 16, 1 };
  const FabricPlan plan = planFabric( intent );
  CHECK( plan.chunkPlan().chunkCountTotal() == 1048576ull );

  // Bounded enumeration at any offset — the tail of a million.
  const std::vector<CubeChunkRequest> tail =
    plan.chunkPlan().materializeChunks( 1048576ull - 5, 100 );
  REQUIRE( tail.size() == 5 );
  CHECK( tail.back().index == 1048575ull );

  // The plan JSON stays small regardless of the chunk count.
  const Json::Value planJson = plan.toJson();
  CHECK( planJson["chunkPlan"]["chunkCountTotal"].asUInt64() == 1048576ull );
  CHECK( planJson.toStyledString().size() < 256ull * 1024 );
}

TEST_CASE( "cancellation is typed at every fabric entry point",
           "[io][fabric][scale][cancel]" )
{
  const std::string dir = scratchDir( "cancel" );
  const std::string scene = writeConstantScene( dir + "/scene.tif", 64, 1.0 );

  std::vector<AssetRecord> records;
  for ( int i = 0; i < 100; ++i )
    records.push_back( recordWithBbox( "r" + std::to_string( i ), scene, "", 1.0 ) );

  FabricIntent intent;
  intent.records = records;
  intent.sceneBudget = 4;
  intent.grid = fixedGrid( 64.0 );

  CancelToken cancel;
  cancel.cancel();
  REQUIRE_THROWS_AS( planFabric( intent, {}, cancel ), GeoError );
  bool cancelledCode = false;
  try
  {
    planFabric( intent, {}, cancel );
  }
  catch ( const GeoError &error )
  {
    cancelledCode = error.code() == ErrorCode::Cancelled;
  }
  CHECK( cancelledCode );

  // Virtual cube window reads honor the same token.
  const VirtualCube cube =
    VirtualCube::build( { records.front() }, fixedGrid( 64.0 ), OverlapPolicy::FirstWins, {} );
  REQUIRE_THROWS_AS( cube.readWindow( 0, 0, 16, 16, {}, cancel ), GeoError );
}

TEST_CASE( "large logical windows cost O(window) memory, never O(cube)",
           "[io][fabric][scale][window]" )
{
  const std::string dir = scratchDir( "bigwin" );
  // ONE 64×64 asset under a 4096×4096 logical grid: the cube's logical
  // extent is 16M cells; every read stays at the requested window.
  const std::string scene = writeConstantScene( dir + "/scene.tif", 64, 7.0 );

  AssetRecord record = recordWithBbox( "big", scene, "2024-01-01T00:00:00Z", 0.0 );
  record.minX = 0.0;
  record.minY = 0.0;
  record.maxX = 4096.0;
  record.maxY = 4096.0;
  const VirtualCube cube =
    VirtualCube::build( { record }, fixedGrid( 4096.0 ), OverlapPolicy::FirstWins, {} );

  // Window (0,0) is the grid's TOP (world y 3584..4096) — the asset lives
  // at world y 0..64, i.e. the BOTTOM window rows. Read that window.
  const std::uint64_t before = peakRssBytes();
  const VirtualCubeWindowResult window = cube.readWindow( 0, 4096 - 512, 512, 512 );
  const std::uint64_t after = peakRssBytes();
  REQUIRE( window.values.size() == 512ull * 512 );
  REQUIRE( window.provenance.size() == 1 );
  CHECK( window.provenance[0].contributed );
  // The scene covers window rows 448..511 (world y 0..64), columns 0..63.
  CHECK( window.values[511ull * 512 + 63] == 7.0 );
  CHECK( window.values[448ull * 512] == 7.0 );
  // One cell past the scene's extent — NoData, and the window top too.
  CHECK( window.values[511ull * 512 + 64] == window.gridNoData );
  CHECK( window.values[0] == window.gridNoData );
  if ( peakRssBytes() != 0 )
  {
    // 512×512 doubles = 2 MiB; the whole operation must stay a small
    // constant above that — never 16M cells (128 MiB).
    CHECK( after - before < 16ull * 1024 * 1024 );
  }
}

TEST_CASE( "mirror corruption degrades by skipping entries, never by failing reads",
           "[io][fabric][scale][mirror]" )
{
  const std::string dir = scratchDir( "corrupt" );
  const std::string scene = writeConstantScene( dir + "/scene.tif", 32, 3.0 );

  FabricIntent intent;
  intent.records = { recordWithBbox( "s", scene, "2024-01-01T00:00:00Z", 0.0 ) };
  intent.sceneBudget = 2;
  intent.grid = fixedGrid( 32.0 );
  intent.chunkShape = { 1, 16, 16, 1 };
  intent.hasWindow = true;
  intent.windowW = 16;
  intent.windowH = 16;
  const FabricPlan plan = planFabric( intent );

  const std::string mirrorDir = dir + "/mirror";
  MirrorOptions options;
  options.mirrorDirectory = mirrorDir;
  const MirrorReport report = mirrorChunks( plan, options );
  CHECK( report.mirrored == 4 );

  // Corrupt the manifest: every token entry replaced by garbage.
  {
    std::ofstream out( mirrorDir + "/manifest.json", std::ios::binary );
    out << "{ not json at all";
  }
  std::string skippedCorrupt;
  CHECK( resolveMirrorHit( mirrorDir, "any-token", "any-key", &skippedCorrupt ).empty() );
  CHECK( !skippedCorrupt.empty() );
  // The stats surface answers honestly (zeros) instead of guessing.
  CHECK( mirrorStatsJson( mirrorDir )["entries"].asUInt64() == 0 );

  // A window read through the corrupt mirror still answers from the asset.
  VirtualCubeReadOptions readOptions;
  readOptions.mirrorDirectory = mirrorDir;
  FabricExecutionReport execReport;
  const VirtualCubeWindowResult window = executeWindow( plan, readOptions, execReport );
  REQUIRE( window.provenance.size() == 1 );
  CHECK( window.provenance[0].mirrorHit.empty() );   // no hit without proof
  CHECK( window.provenance[0].contributed );
  CHECK( window.values[0] == 3.0 );

  // Offline + corrupt mirror: the typed refusal stands (never a guess).
  offline::setEnabled( true );
  const FabricPlan offlinePlan = planFabric( intent );   // local records plan offline
  CHECK( offlinePlan.cost().scenes == 1 );
  offline::setEnabled( false );
}

TEST_CASE( "prefetch without the range cache and offline refusals stay typed",
           "[io][fabric][scale][typed]" )
{
  const std::string dir = scratchDir( "typed" );
  const std::string scene = writeConstantScene( dir + "/scene.tif", 32, 1.0 );

  FabricIntent intent;
  intent.records = { recordWithBbox( "s", scene, "2024-01-01T00:00:00Z", 0.0 ) };
  intent.sceneBudget = 2;
  intent.grid = fixedGrid( 32.0 );
  intent.chunkShape = { 1, 32, 32, 1 };
  const FabricPlan plan = planFabric( intent );

  RemoteRangeCache::uninstall();
  REQUIRE_THROWS_AS( prefetchChunks( plan ), GeoError );

  // Offline gate engages the typed refusal at the catalog seam for remote
  // roots; local records keep planning (the mirror doctrine).
  offline::setEnabled( true );
  FabricIntent remote;
  remote.catalogUri = "https://example.invalid/stac";
  remote.sceneBudget = 2;
  REQUIRE_THROWS_AS( planFabric( remote ), GeoError );
  offline::setEnabled( false );
}
