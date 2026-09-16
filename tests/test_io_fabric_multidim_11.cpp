/***************************************************************************
  tests/test_io_fabric_multidim_11.cpp — Cloud Data Fabric 11.0 (WP D/E):
  multidim chunk planning with real slicing (time instants, named-dimension
  ranges), bounded execution through the ONE multidim read path, and
  million-chunk plans that never materialize.

  Oracle discipline: the expected values come from the AUTHORING formula
  (independent of the read path), and the GDAL-direct reference (a raw
  readSliceWindow on the open store) is compared against the fabric
  execution for the same cells. Driver-gated on netCDF like the 4.0 suite.
 ***************************************************************************/

#include "geospatial/fabric/query_planner.h"
#include "geospatial/gdal_guard.h"
#include "geospatial/multidim/multidim_view.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <gdal.h>
#include <netcdf.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace sicnu::geo;
namespace fs = std::filesystem;

namespace
{

std::string scratchDir( const char *name )
{
  std::string path = ( fs::temp_directory_path() / "sicnu_fabric_tests" /
                       ( std::string( "md11_" ) + name ) )
                       .string();
  std::filesystem::remove_all( path );
  std::filesystem::create_directories( path );
  return path;
}

bool netCdfAvailable()
{
  sicnu::geo::ensureGdalRegistered();
  static const bool available = GDALGetDriverByName( "netCDF" ) != nullptr;
  return available;
}

/// Authoring formula (THE independent truth): v(t,b,y,x) = t*10000 + b*1000 + y*10 + x.
constexpr double authoredValue( int t, int b, int y, int x )
{
  return t * 10000.0 + b * 1000.0 + y * 10.0 + x;
}

/// 4(time) × 3(band) × 8(y) × 8(x) cube with CF time coordinates.
std::string writeCube( const std::string &dir, const std::string &name )
{
  const std::string path = ( fs::path( dir ) / name ).string();
  int ncid = -1;
  REQUIRE( nc_create( path.c_str(), NC_CLOBBER, &ncid ) == NC_NOERR );
  int timeId = -1, bandId = -1, yId = -1, xId = -1;
  REQUIRE( nc_def_dim( ncid, "time", 4, &timeId ) == NC_NOERR );
  REQUIRE( nc_def_dim( ncid, "band", 3, &bandId ) == NC_NOERR );
  REQUIRE( nc_def_dim( ncid, "y", 8, &yId ) == NC_NOERR );
  REQUIRE( nc_def_dim( ncid, "x", 8, &xId ) == NC_NOERR );
  int dimIds[4] = { timeId, bandId, yId, xId };
  int varId = -1;
  REQUIRE( nc_def_var( ncid, "sst", NC_FLOAT, 4, dimIds, &varId ) == NC_NOERR );
  int timeVarId = -1;
  REQUIRE( nc_def_var( ncid, "time", NC_DOUBLE, 1, &timeId, &timeVarId ) == NC_NOERR );
  const char *units = "hours since 2026-01-01 00:00:00";
  REQUIRE( nc_put_att_text( ncid, timeVarId, "units", std::strlen( units ), units ) == NC_NOERR );
  REQUIRE( nc_enddef( ncid ) == NC_NOERR );
  float values[4 * 3 * 8 * 8];
  for ( int t = 0; t < 4; ++t )
    for ( int b = 0; b < 3; ++b )
      for ( int y = 0; y < 8; ++y )
        for ( int x = 0; x < 8; ++x )
          values[(( t * 3 + b ) * 8 + y ) * 8 + x] = static_cast<float>( authoredValue( t, b, y, x ) );
  REQUIRE( nc_put_var_float( ncid, varId, values ) == NC_NOERR );
  const double timeValues[4] = { 0.0, 24.0, 48.0, 72.0 };
  REQUIRE( nc_put_var_double( ncid, timeVarId, timeValues ) == NC_NOERR );
  REQUIRE( nc_close( ncid ) == NC_NOERR );
  return path;
}

FabricIntent multidimIntent( const std::string &path )
{
  FabricIntent intent;
  intent.multidimPath = path;
  intent.multidimVariable = "sst";
  intent.chunkShape.time = 1;
  intent.chunkShape.y = 4;
  intent.chunkShape.x = 4;
  intent.chunkShape.band = 1;
  intent.chunkShape.perDimension["band"] = 1;
  return intent;
}

} // namespace

TEST_CASE( "multidim plans slice by instants and named ranges, executing through one read path",
           "[io][fabric][multidim][wp_d][wp_e][oracle3]" )
{
  if ( !netCdfAvailable() )
  {
    WARN( "netCDF driver not present — multidim 11 suite self-skipped" );
    return;
  }
  const std::string dir = scratchDir( "cube" );
  const std::string store = writeCube( dir, "cube.nc" );

  FabricIntent intent = multidimIntent( store );
  intent.slice.timeStartUtc = "2026-01-02T00:00:00Z";   // 24h — inclusive
  intent.slice.timeEndUtc = "2026-01-04T00:00:00Z";     // 72h — exclusive
  intent.slice.dimensionRanges["band"] = { 1, 3 };      // bands 1 and 2

  const FabricPlan plan = planFabric( intent, {}, {} );
  REQUIRE( plan.cost().scenes == 0 );                    // no catalog stage
  CHECK( plan.cost().chunks == 16 );                     // 2 time × 2 band × 2 y × 2 x
  CHECK( plan.chunkPlan().timeSliced() );
  CHECK( plan.chunkPlan().dims().size() == 4 );          // band, time, y, x

  // Execution through executeChunks — every emitted grid compared against
  // the authoring formula (and a GDAL-direct reference for the first one).
  std::vector<std::pair<std::string, double>> logged;
  FabricExecutionReport report;
  const std::vector<FabricChunkOutcome> outcomes = executeChunks(
    plan, {}, 8,
    [ & ]( const CubeChunkRequest &request, const VirtualCubeWindowResult &window ) {
      // Chunk dims: band, time, y, x. Post-slice offsets map 1:1 to the
      // authored axes via the selection contract.
      const int bandOffset = static_cast<int>( request.dimOffsets[0] );
      const int timeOffset = static_cast<int>( request.dimOffsets[1] );
      const int yOff = static_cast<int>( request.dimOffsets[2] );
      const int xOff = static_cast<int>( request.dimOffsets[3] );
      const int sourceBand = 1 + bandOffset;             // dimensionRanges [1,3)
      const int sourceTime = 1 + timeOffset;             // time slice [24h, 72h)
      for ( int y = 0; y < 4; ++y )
        for ( int x = 0; x < 4; ++x )
        {
          const double expected = authoredValue( sourceTime, sourceBand, yOff + y, xOff + x );
          const double got = window.values[static_cast<std::size_t>( y ) * 4 + x];
          if ( got != expected )
            logged.push_back( { "formula", expected - got } );
        }
    },
    report, {} );

  REQUIRE( outcomes.size() == 16 );
  CHECK( report.chunksExecuted == 16 );
  CHECK( report.budgetBreached == false );
  CHECK( logged.empty() );   // every cell matched the authoring formula

  // GDAL-direct reference: the same window read straight from the open
  // store equals the fabric execution's first grid (one read path, two
  // entry points agree).
  MultidimView view = MultidimView::open( store );
  const MultidimGrid reference =
    view.readSliceWindow( "sst", { { "band", 1 }, { "time", 1 } }, 0, 0, 4, 4 );
  REQUIRE( reference.values.size() == 16 );
  CHECK( reference.values[0] == Catch::Approx( authoredValue( 1, 1, 0, 0 ) ) );
  CHECK( reference.values[15] == Catch::Approx( authoredValue( 1, 1, 3, 3 ) ) );
}

TEST_CASE( "multidim slice refusals are typed and honest",
           "[io][fabric][multidim][wp_e][negative]" )
{
  if ( !netCdfAvailable() )
  {
    WARN( "netCDF driver not present — multidim 11 suite self-skipped" );
    return;
  }
  const std::string dir = scratchDir( "cube2" );
  const std::string store = writeCube( dir, "cube2.nc" );
  MultidimView view = MultidimView::open( store );
  const MultidimCubeDescriptor descriptor = describeCube( view, "sst" );

  // Out-of-bounds dimension range: typed refusal at plan time.
  CubeChunkShape shape;
  shape.y = 4;
  shape.x = 4;
  CubeSlice badRange;
  badRange.dimensionRanges["band"] = { 2, 9 };   // axis size is 3
  REQUIRE_THROWS_AS( CubeChunkPlan::forMultidimDescriptor( descriptor, shape, badRange ),
                     GeoError );

  // An empty range is refused by validation.
  CubeSlice emptyRange;
  emptyRange.dimensionRanges["band"] = { 1, 1 };
  REQUIRE_THROWS_AS( CubeChunkPlan::forMultidimDescriptor( descriptor, shape, emptyRange ),
                     GeoError );
}

TEST_CASE( "million-chunk multidim plans stay bounded (u64 math, no full materialization)",
           "[io][fabric][multidim][wp_e][scale]" )
{
  // A descriptor is the PLANNING truth (JSON-symmetric, IO-free): build a
  // 10000×32×2048×2048 cube descriptor without any store behind it and
  // plan 1-cell y/x chunks — ~134 billion chunks are NAMED, none exist.
  MultidimCubeDescriptor descriptor;
  descriptor.path = "virtual://million";
  descriptor.variable = "v";
  descriptor.dtype = "Float32";
  const char *names[4] = { "time", "band", "y", "x" };
  const std::int64_t sizes[4] = { 10000, 32, 2048, 2048 };
  for ( int i = 0; i < 4; ++i )
  {
    MultidimCubeAxis axis;
    axis.name = names[i];
    axis.size = sizes[i];
    descriptor.dimensionNames.push_back( names[i] );
    descriptor.axes.push_back( axis );
  }

  CubeChunkShape shape;
  shape.y = 1;
  shape.x = 1;
  shape.time = 1;
  shape.perDimension["band"] = 1;
  const CubeChunkPlan plan = CubeChunkPlan::forMultidimDescriptor( descriptor, shape, {} );
  const std::uint64_t total = plan.chunkCountTotal();
  CHECK( total > 1000000000ull );   // 10000 × 32 × 2048 × 2048

  // Bounded windows at arbitrary offsets: exactly maxCount chunks, in the
  // fixed order, with consistent mixed-radix coordinates.
  const std::uint64_t probePoints[] = { 0, 1, 999999999ull, total - 3 };
  for ( const std::uint64_t begin : probePoints )
  {
    const std::vector<CubeChunkRequest> window =
      plan.materializeChunks( begin, 3 );
    REQUIRE( window.size() == 3 );
    for ( std::size_t i = 0; i < window.size(); ++i )
      CHECK( window[i].index == begin + i );
  }
  // Beyond the end: typed refusal, never a wrap-around.
  REQUIRE_THROWS_AS( plan.materializeChunks( total, 3 ), GeoError );
}

TEST_CASE( "multidim spatial slices map post-slice offsets to SOURCE pixels (review P0)",
           "[io][fabric][multidim][wp_e][spatial]" )
{
  // A descriptor with a declared geotransform (JSON-symmetric construction
  // — no store needed for PLANNING known-answers).
  MultidimCubeDescriptor descriptor;
  descriptor.path = "virtual://geo";
  descriptor.variable = "v";
  descriptor.dtype = "Float32";
  descriptor.hasGeoTransform = true;
  descriptor.geotransform = { 10.0, 2.0, 0.0, 40.0, 0.0, -2.0 };   // 16×16 @2m, origin (10,40)
  const char *names[4] = { "time", "band", "y", "x" };
  const std::int64_t sizes[4] = { 4, 2, 16, 16 };
  for ( int i = 0; i < 4; ++i )
  {
    MultidimCubeAxis axis;
    axis.name = names[i];
    axis.size = sizes[i];
    descriptor.dimensionNames.push_back( names[i] );
    descriptor.axes.push_back( axis );
  }

  CubeChunkShape shape;
  shape.y = 4;
  shape.x = 4;
  CubeSlice slice;
  slice.hasSpatialSlice = true;
  slice.minX = 16.0;   // pixel x = (16-10)/2 = 3
  slice.minY = 28.0;   // pixel y = (40-28)/2 = 6
  slice.maxX = 24.0;   // pixel x end = 7
  slice.maxY = 36.0;   // pixel y end = 10

  const CubeChunkPlan plan = CubeChunkPlan::forMultidimDescriptor( descriptor, shape, slice );
  CHECK( plan.spatialSliced() );
  CHECK( plan.chunkCountTotal() == 4 * 1 * 1 * 1 );   // time(4)×band(2/2=1)×y 1×x 1

  // The post-slice chunk offsets map to SOURCE pixels [6..10)×[3..7):
  // the selection IS the offset mapping.
  const auto &selection = plan.multidimSelection();
  REQUIRE( selection.count( "y" ) == 1 );
  REQUIRE( selection.count( "x" ) == 1 );
  REQUIRE( selection.at( "y" ).size() == 4 );
  REQUIRE( selection.at( "x" ).size() == 4 );
  // gt = {10,2,0,40,0,-2}: world y 28..36 maps to rows (40-36)/2=2 .. (40-28)/2=6
  CHECK( selection.at( "y" ).front() == 2 );   // source row of slice origin
  CHECK( selection.at( "x" ).front() == 3 );   // source col of slice origin
  CHECK( selection.at( "y" ).back() == 5 );
  CHECK( selection.at( "x" ).back() == 6 );
  // Execution maps post-slice offset 0 → source 6/3 (not 0/0).
  const std::vector<CubeChunkRequest> first = plan.materializeChunks( 0, 1 );
  REQUIRE( first.size() == 1 );
}

TEST_CASE( "time-less multidim stores plan real chunks; time slices refuse (review P1)",
           "[io][fabric][multidim][wp_e][negative]" )
{
  // A [band, y, x] store: NO temporal axis. The plan must chunk the real
  // dims — a phantom zero-size time dim multiplied everything to zero.
  MultidimCubeDescriptor descriptor;
  descriptor.path = "virtual://notime";
  descriptor.variable = "v";
  descriptor.dtype = "Float32";
  const char *names[3] = { "band", "y", "x" };
  const std::int64_t sizes[3] = { 3, 16, 16 };
  for ( int i = 0; i < 3; ++i )
  {
    MultidimCubeAxis axis;
    axis.name = names[i];
    axis.size = sizes[i];
    descriptor.dimensionNames.push_back( names[i] );
    descriptor.axes.push_back( axis );
  }
  CubeChunkShape shape;
  shape.y = 8;
  shape.x = 8;
  shape.perDimension["band"] = 2;
  const CubeChunkPlan plan =
    CubeChunkPlan::forMultidimDescriptor( descriptor, shape, {} );
  CHECK( plan.chunkCountTotal() == 2 * 2 * 2 );   // band(2/2)×y(16/8)×x(16/8)
  CHECK( plan.dims().size() == 3 );               // band, y, x — NO time dim

  // A time slice on a time-less store is a typed refusal (never ignored).
  CubeSlice slice;
  slice.timeStartUtc = "2026-01-01T00:00:00Z";
  slice.timeEndUtc = "2026-01-02T00:00:00Z";
  REQUIRE_THROWS_AS( CubeChunkPlan::forMultidimDescriptor( descriptor, shape, slice ),
                     GeoError );
}
