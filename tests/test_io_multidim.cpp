/***************************************************************************
  tests/test_io_multidim.cpp — Phase 8: multidimensional contract suite.
  Driver-gated on netCDF; laziness, named slices, no flatten-to-bands.
 ***************************************************************************/

#include "geospatial/multidim/multidim_view.h"
#include "geospatial/gdal_guard.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <gdal.h>
#include <netcdf.h>

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace
{
std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_multidim" / name;
  std::error_code ec;
  fs::remove_all( dir, ec ); // idempotent suites: start from a clean scratch
  fs::create_directories( dir );
  return dir.string();
}

bool netCdfAvailable()
{
  sicnu::geo::ensureGdalRegistered();
  static const bool available = GDALGetDriverByName( "netCDF" ) != nullptr;
  return available;
}

/// 2×3×4 cube (time, y, x) with deterministic values; returns the path.
std::string writeCube( const std::string &dir, const std::string &name )
{
  // Authored with the netCDF C library directly: the suite exercises the
  // READ/slice side of the foundation contract.
  const std::string path = ( fs::path( dir ) / name ).string();
  int ncid = -1;
  REQUIRE( nc_create( path.c_str(), NC_CLOBBER, &ncid ) == NC_NOERR );
  int timeId = -1, yId = -1, xId = -1;
  REQUIRE( nc_def_dim( ncid, "time", 2, &timeId ) == NC_NOERR );
  REQUIRE( nc_def_dim( ncid, "y", 3, &yId ) == NC_NOERR );
  REQUIRE( nc_def_dim( ncid, "x", 4, &xId ) == NC_NOERR );
  int dimIds[3] = { timeId, yId, xId };
  int varId = -1;
  REQUIRE( nc_def_var( ncid, "sst", NC_FLOAT, 3, dimIds, &varId ) == NC_NOERR );
  // CF-style time coordinate variable with known values (hours offsets).
  int timeVarId = -1;
  REQUIRE( nc_def_var( ncid, "time", NC_DOUBLE, 1, &timeId, &timeVarId ) == NC_NOERR );
  const char *units = "hours since 2026-01-01 00:00:00";
  REQUIRE( nc_put_att_text( ncid, timeVarId, "units", std::strlen( units ), units ) == NC_NOERR );
  REQUIRE( nc_enddef( ncid ) == NC_NOERR );
  float values[2 * 3 * 4];
  for ( std::size_t i = 0; i < 24; ++i )
    values[i] = static_cast<float>( i );
  REQUIRE( nc_put_var_float( ncid, varId, values ) == NC_NOERR );
  const double timeValues[2] = { 101.0, 202.5 };
  REQUIRE( nc_put_var_double( ncid, timeVarId, timeValues ) == NC_NOERR );
  REQUIRE( nc_close( ncid ) == NC_NOERR );
  return path;
}

/// 2x3x4 cube WITH a declared _FillValue missing-value sentinel (netCDF
/// fills unwritten data): the last 6 cells stay unwritten.
std::string writeCubeWithMissing( const std::string &dir, const std::string &name )
{
  const std::string path = ( fs::path( dir ) / name ).string();
  int ncid = -1;
  REQUIRE( nc_create( path.c_str(), NC_CLOBBER, &ncid ) == NC_NOERR );
  int timeId = -1, yId = -1, xId = -1;
  REQUIRE( nc_def_dim( ncid, "time", 2, &timeId ) == NC_NOERR );
  REQUIRE( nc_def_dim( ncid, "y", 3, &yId ) == NC_NOERR );
  REQUIRE( nc_def_dim( ncid, "x", 4, &xId ) == NC_NOERR );
  int dimIds[3] = { timeId, yId, xId };
  int varId = -1;
  REQUIRE( nc_def_var( ncid, "lst", NC_FLOAT, 3, dimIds, &varId ) == NC_NOERR );
  float fillValue = -9999.0f;
  REQUIRE( nc_put_att_float( ncid, varId, "_FillValue", NC_FLOAT, 1, &fillValue ) == NC_NOERR );
  REQUIRE( nc_enddef( ncid ) == NC_NOERR );
  // Write only the first time step (12 cells); the second stays _FillValue.
  float values[12];
  for ( std::size_t i = 0; i < 12; ++i )
    values[i] = static_cast<float>( i + 1 );
  const std::size_t start[3] = { 0, 0, 0 };
  const std::size_t count[3] = { 1, 3, 4 };
  REQUIRE( nc_put_vara_float( ncid, varId, start, count, values ) == NC_NOERR );
  REQUIRE( nc_close( ncid ) == NC_NOERR );
  return path;
}
} // namespace

TEST_CASE( "multidim listing is lazy and variable-aware", "[io][multidim]" )
{
  if ( !netCdfAvailable() )
  {
    WARN( "netCDF driver not present — multidim suite self-skipped" );
    return;
  }
  const std::string path = writeCube( scratch( "lazy" ), "cube.nc" );

  sicnu::geo::MultidimView view = sicnu::geo::MultidimView::open( path );
  const sicnu::geo::MultidimMetadata &meta = view.metadata();
  CHECK( meta.driver == "netCDF" );
  REQUIRE( meta.variables.size() >= 1 );
  bool found = false;
  for ( const sicnu::geo::VariableInfo &variable : meta.variables )
  {
    if ( variable.name == "sst" )
    {
      found = true;
      REQUIRE( variable.dimensionNames.size() == 3 );
      CHECK( variable.dimensionNames[0] == "time" );
      CHECK( variable.dimensionNames[2] == "x" );
    }
  }
  CHECK( found );

  // Dimensions carry their declared sizes.
  std::size_t timeSize = 0;
  for ( const sicnu::geo::DimensionInfo &dim : meta.dimensions )
    if ( dim.name == "time" )
      timeSize = static_cast<std::size_t>( dim.size );
  CHECK( timeSize == 2 );
}

TEST_CASE( "time and level slices return the right 2D grid", "[io][multidim][slice]" )
{
  if ( !netCdfAvailable() )
    return;
  const std::string path = writeCube( scratch( "slice" ), "cube.nc" );

  // Reopen probe: full-extent read on a fresh read-only handle.
  {
    sicnu::geo::QuietCplErrors quiet;
    GDALDatasetH handle = GDALOpenEx( path.c_str(), GDAL_OF_READONLY | GDAL_OF_MULTIDIM_RASTER, nullptr, nullptr, nullptr );
    REQUIRE( handle );
    GDALGroupH root = GDALDatasetGetRootGroup( handle );
    REQUIRE( root );
    GDALMDArrayH array = GDALGroupOpenMDArray( root, "sst", nullptr );
    REQUIRE( array );
    std::vector<float> check( 24, -1.0f );
    const GUInt64 start[3] = { 0, 0, 0 };
    const std::size_t count[3] = { 2, 3, 4 };
    const GInt64 step[3] = { 1, 1, 1 };
    const GPtrDiff_t stride[3] = { 12, 4, 1 };
    GDALExtendedDataTypeH f32 = GDALExtendedDataTypeCreate( GDT_Float32 );
    REQUIRE( GDALMDArrayRead( array, start, count, step, stride, f32, check.data(), nullptr, 0 ) );
    GDALExtendedDataTypeRelease( f32 );
    CHECK( check[0] == 0.0f );
    CHECK( check[23] == 23.0f );
    GDALMDArrayRelease( array );
    GDALGroupRelease( root );
    GDALClose( handle );
  }

  sicnu::geo::MultidimView view = sicnu::geo::MultidimView::open( path );

  const sicnu::geo::MultidimGrid slice0 = view.readTemporalOrLevelSlice( "sst", "time", 0 );
  REQUIRE( slice0.rows == 3 );
  REQUIRE( slice0.cols == 4 );
  CHECK( slice0.values[0] == 0.0 );
  CHECK( slice0.values[11] == 11.0 );

  const sicnu::geo::MultidimGrid slice1 = view.readTemporalOrLevelSlice( "sst", "time", 1 );
  CHECK( slice1.values[0] == 12.0 );
  CHECK( slice1.values[11] == 23.0 );

  // Named-slice form is equivalent to the convenience form.
  const sicnu::geo::MultidimGrid named = view.readSlice( "sst", { { "time", 1 } } );
  REQUIRE( named.rows == 3 );
  REQUIRE( named.cols == 4 );
  CHECK( named.values[0] == 12.0 );
  CHECK( named.values[11] == 23.0 );

  // Slicing a spatial dim (leaving one free dim) violates the two-free-dims
  // contract and is refused — no flatten-to-1D.
  CHECK_THROWS_AS( view.readSlice( "sst", { { "time", 1 }, { "y", 2 } } ), sicnu::geo::GeoError );
}

TEST_CASE( "partial slices and unknown names are structured errors — no flatten", "[io][multidim][contract]" )
{
  if ( !netCdfAvailable() )
    return;
  const std::string path = writeCube( scratch( "contract" ), "cube.nc" );
  sicnu::geo::MultidimView view = sicnu::geo::MultidimView::open( path );

  // Missing a slice for a non-spatial dimension: the contract refuses instead
  // of silently flattening the remaining axis into extra rows/columns.
  CHECK_THROWS_AS( view.readSlice( "sst", {} ), sicnu::geo::GeoError );

  // Unknown variable / dimension names.
  CHECK_THROWS_AS( view.readTemporalOrLevelSlice( "nope", "time", 0 ), sicnu::geo::GeoError );
  CHECK_THROWS_AS( view.readSlice( "sst", { { "not_a_dim", 0 } } ), sicnu::geo::GeoError );

  // Out-of-range slice index.
  CHECK_THROWS_AS( view.readTemporalOrLevelSlice( "sst", "time", 9 ), sicnu::geo::GeoError );
}

TEST_CASE( "multidim view on non-multidim data fails as Unsupported", "[io][multidim][contract]" )
{
  const std::string dir = scratch( "plain" );
  const std::string plain = ( fs::path( dir ) / "plain.tif" ).string();
  sicnu::geo::ensureGdalRegistered();
  sicnu::geo::QuietCplErrors quiet;
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver );
  GDALDatasetH dataset = GDALCreate( driver, plain.c_str(), 4, 4, 1, GDT_Byte, nullptr );
  REQUIRE( dataset );
  GDALClose( dataset );

  try
  {
    sicnu::geo::MultidimView view = sicnu::geo::MultidimView::open( plain );
    // A plain TIFF has no root group arrays; listing an empty store is fine,
    // but slicing anything must fail.
    CHECK_THROWS_AS( view.readTemporalOrLevelSlice( "sst", "time", 0 ), sicnu::geo::GeoError );
  }
  catch ( const sicnu::geo::GeoError &error )
  {
    CHECK( ( error.code() == sicnu::geo::ErrorCode::Unsupported
             || error.code() == sicnu::geo::ErrorCode::OpenFailed ) );
  }
}

TEST_CASE( "coordinate axes are captured and resolve value-based slices",
           "[io][multidim][coords]" )
{
  if ( !netCdfAvailable() )
  {
    WARN( "netCDF driver not present — multidim suite self-skipped" );
    return;
  }
  const std::string path = writeCube( scratch( "coords" ), "axis.nc" );
  auto view = sicnu::geo::MultidimView::open( path );
  REQUIRE( view.isOpen() );

  const sicnu::geo::DimensionInfo *timeAxis = nullptr;
  for ( const auto &dim : view.metadata().dimensions )
    if ( dim.name == "time" )
      timeAxis = &dim;
  REQUIRE( timeAxis != nullptr );
  REQUIRE( timeAxis->hasValues );
  REQUIRE_FALSE( timeAxis->valuesBounded );
  REQUIRE( timeAxis->values.size() == 2 );
  CHECK( timeAxis->values[0] == Approx( 101.0 ) );
  CHECK( timeAxis->values[1] == Approx( 202.5 ) );

  // Exact match resolves the second time step.
  const auto exact = view.resolveCoordinateIndex( "time", 202.5, sicnu::geo::CoordinateMatch::Exact );
  CHECK( exact.index == 1 );
  CHECK( exact.exact );

  // Nearest picks the closer step and honors the tolerance bound.
  const auto nearest = view.resolveCoordinateIndex( "time", 210.0, sicnu::geo::CoordinateMatch::Nearest, 15.0 );
  CHECK( nearest.index == 1 );
  CHECK( nearest.distance == Approx( 7.5 ) );
  CHECK_THROWS_AS( view.resolveCoordinateIndex( "time", 400.0, sicnu::geo::CoordinateMatch::Nearest, 15.0 ),
                   sicnu::geo::GeoError );
  CHECK_THROWS_AS( view.resolveCoordinateIndex( "time", 210.0, sicnu::geo::CoordinateMatch::Exact ),
                   sicnu::geo::GeoError );
  CHECK_THROWS_AS( view.resolveCoordinateIndex( "nope", 1.0, sicnu::geo::CoordinateMatch::Exact ),
                   sicnu::geo::GeoError );

  // A value-based slice reads the SAME grid as the index-based one.
  const sicnu::geo::MultidimGrid byValue =
    view.readSliceByCoordinateValues( "sst", { { "time", 202.5 } }, sicnu::geo::CoordinateMatch::Exact );
  REQUIRE( byValue.rows == 3 );
  REQUIRE( byValue.cols == 4 );
  CHECK( byValue.values[0] == Approx( 12.0 ) );
}

TEST_CASE( "windowed slices read only the requested extent",
           "[io][multidim][window]" )
{
  if ( !netCdfAvailable() )
  {
    WARN( "netCDF driver not present — multidim suite self-skipped" );
    return;
  }
  const std::string path = writeCube( scratch( "window" ), "cube.nc" );
  auto view = sicnu::geo::MultidimView::open( path );
  REQUIRE( view.isOpen() );

  const sicnu::geo::MultidimGrid window =
    view.readSliceWindow( "sst", { { "time", 1 } }, 1, 1, 2, 3 );
  REQUIRE( window.rows == 2 );
  REQUIRE( window.cols == 3 );
  // Full cube value at (t=1, y, x) is 12 + y*4 + x; the window starts at (1,1).
  const double expected[6] = { 17, 18, 19, 21, 22, 23 };
  for ( std::size_t i = 0; i < 6; ++i )
    CHECK( window.values[i] == Approx( expected[i] ) );

  // Out-of-extent windows are typed errors, never clamped lies.
  CHECK_THROWS_AS( view.readSliceWindow( "sst", { { "time", 1 } }, 2, 0, 2, 4 ),
                   sicnu::geo::GeoError );
  CHECK_THROWS_AS( view.readSliceWindow( "sst", { { "time", 1 } }, 0, 0, 4, 4 ),
                   sicnu::geo::GeoError );
}

TEST_CASE( "missing values are counted against the declared _FillValue",
           "[io][multidim][missing]" )
{
  if ( !netCdfAvailable() )
  {
    WARN( "netCDF driver not present — multidim suite self-skipped" );
    return;
  }
  const std::string path = writeCubeWithMissing( scratch( "missing" ), "lst.nc" );
  auto view = sicnu::geo::MultidimView::open( path );
  REQUIRE( view.isOpen() );

  const sicnu::geo::MultidimGrid written = view.readSlice( "lst", { { "time", 0 } } );
  CHECK( written.missingCount == 0 );
  const sicnu::geo::MultidimGrid unwritten = view.readSlice( "lst", { { "time", 1 } } );
  REQUIRE( unwritten.values.size() == 12 );
  CHECK( unwritten.missingCount == 12 );
  // Values stay stored (sentinel preserved, no silent rewrite to 0/NaN).
  CHECK( unwritten.values[0] == Approx( -9999.0 ) );
}


// ---------------------------------------------------------------------------
// 8.0 — EO cube workflow: string datetime axes, instant selection, bounded
// window reads (Data Fabric track, pkg E). Driver-gated like the rest.
// ---------------------------------------------------------------------------

namespace
{

/// EO-style cube (time, y, x) whose time axis is an ISO-8601 STRING
/// coordinate variable — the common "practical EO cube" shape that the
/// numeric-only capture could not select.
std::string writeCubeWithStringTime( const std::string &dir, const std::string &name )
{
  const std::string path = ( fs::path( dir ) / name ).string();
  int ncid = -1;
  // NC_STRING variables need the netCDF-4 (HDF5) container format; a libnetcdf
  // built without HDF5 support must SKIP the suite, not fail it.
  const int createStatus = nc_create( path.c_str(), NC_CLOBBER | NC_NETCDF4, &ncid );
  if ( createStatus != NC_NOERR )
  {
    WARN( "netCDF-4 (HDF5) container unavailable (nc_create status " << createStatus << ") — string-axis suite skipped" );
    return std::string();
  }
  REQUIRE( ncid != -1 );
  int timeId = -1, yId = -1, xId = -1;
  REQUIRE( nc_def_dim( ncid, "time", 3, &timeId ) == NC_NOERR );
  REQUIRE( nc_def_dim( ncid, "y", 4, &yId ) == NC_NOERR );
  REQUIRE( nc_def_dim( ncid, "x", 5, &xId ) == NC_NOERR );
  int dimIds[3] = { timeId, yId, xId };
  int varId = -1;
  REQUIRE( nc_def_var( ncid, "lst", NC_FLOAT, 3, dimIds, &varId ) == NC_NOERR );
  // String datetime axis: label[i] = base + i days (one label declared in a
  // non-UTC offset to prove instant-based selection).
  int timeVarId = -1;
  REQUIRE( nc_def_var( ncid, "time", NC_STRING, 1, &timeId, &timeVarId ) == NC_NOERR );
  REQUIRE( nc_enddef( ncid ) == NC_NOERR );
  const char *labels[3] = {
    "2026-07-01T00:00:00Z",
    "2026-07-02T02:00:00+02:00", // same instant as 2026-07-02T00:00:00Z
    "2026-07-03T00:00:00Z",
  };
  for ( std::size_t i = 0; i < 3; ++i )
  {
    const std::size_t index[1] = { i };
    REQUIRE( nc_put_var1_string( ncid, timeVarId, index, &labels[i] ) == NC_NOERR );
  }
  float values[3 * 4 * 5];
  for ( std::size_t i = 0; i < 3 * 4 * 5; ++i )
    values[i] = static_cast<float>( i );
  REQUIRE( nc_put_var_float( ncid, varId, values ) == NC_NOERR );
  REQUIRE( nc_close( ncid ) == NC_NOERR );
  return path;
}

} // namespace

TEST_CASE( "string datetime axes are captured and resolve by label or instant",
           "[io][multidim][string-axis][utc8]" )
{
  if ( !netCdfAvailable() )
  {
    WARN( "netCDF driver not present — string-axis suite skipped" );
    return;
  }
  const std::string dir = scratch( "string_time" );
  const std::string path = writeCubeWithStringTime( dir, "eo_cube.nc" );
  if ( path.empty() )
    return;

  sicnu::geo::MultidimView view = sicnu::geo::MultidimView::open( path );
  REQUIRE( view.isOpen() );
  const sicnu::geo::DimensionInfo *timeAxis = nullptr;
  for ( const sicnu::geo::DimensionInfo &dim : view.metadata().dimensions )
    if ( dim.name == "time" )
      timeAxis = &dim;
  REQUIRE( timeAxis != nullptr );
  CHECK( timeAxis->hasStringValues );
  REQUIRE( timeAxis->stringValues.size() == 3 );
  CHECK( timeAxis->stringValues[0] == "2026-07-01T00:00:00Z" );
  // JSON round-trip keeps the string axis (symmetric serialization).
  const sicnu::geo::MultidimMetadata reparsed =
    sicnu::geo::MultidimMetadata::fromJson( view.metadata().toJson() );
  REQUIRE( reparsed.dimensions.size() == view.metadata().dimensions.size() );
  bool stringAxisRoundTripped = false;
  for ( const sicnu::geo::DimensionInfo &dim : reparsed.dimensions )
    if ( dim.name == "time" )
      stringAxisRoundTripped = dim.hasStringValues && dim.stringValues.size() == 3;
  CHECK( stringAxisRoundTripped );

  // Exact label match.
  const sicnu::geo::CoordinateSliceMatch exact =
    view.resolveCoordinateIndexByString( "time", "2026-07-03T00:00:00Z" );
  CHECK( exact.exact );
  CHECK( exact.index == 2 );

  // Mixed-offset label resolves by INSTANT (verbatim match is impossible).
  const sicnu::geo::CoordinateSliceMatch byInstant =
    view.resolveCoordinateIndexByString( "time", "2026-07-02T00:00:00Z" );
  CHECK( byInstant.index == 1 );
  CHECK( byInstant.distance == Approx( 0.0 ) );

  // Unknown label is a typed miss, never a guess.
  CHECK_THROWS_AS( view.resolveCoordinateIndexByString( "time", "2030-01-01T00:00:00Z" ),
                   sicnu::geo::GeoError );
}

TEST_CASE( "EO cube workflow: instant selection, bounded window, dimension fidelity",
           "[io][multidim][eo-cube][utc8]" )
{
  if ( !netCdfAvailable() )
  {
    WARN( "netCDF driver not present — EO cube workflow skipped" );
    return;
  }
  const std::string dir = scratch( "eo_workflow" );
  const std::string path = writeCubeWithStringTime( dir, "eo_cube.nc" );
  if ( path.empty() )
    return;

  sicnu::geo::MultidimView view = sicnu::geo::MultidimView::open( path );
  REQUIRE( view.isOpen() );

  // 1) Select the acquisition "2026-07-02" through its string instant.
  const sicnu::geo::CoordinateSliceMatch t1 =
    view.resolveCoordinateIndexByString( "time", "2026-07-02T00:00:00Z" );
  REQUIRE( t1.index == 1 );

  // 2) Bounded window read at that instant (rows 1..3, cols 2..5): values
  //    must match the cube's (time=1, y, x) layout — dimension-order
  //    fidelity, never a reshuffled band.
  const std::vector<std::pair<std::string, std::int64_t>> dims = {
    { "time", t1.index },
  };
  sicnu::geo::MultidimGrid window =
    view.readSliceWindow( "lst", dims, 1, 2, 2, 3, /*maxCells=*/1ull * 1024 );
  REQUIRE( window.rows == 2 );
  REQUIRE( window.cols == 3 );
  const auto expected = [ & ] ( std::int64_t y, std::int64_t x ) {
    return static_cast<double>( 1 * 4 * 5 + y * 5 + x );
  };
  CHECK( window.values[0] == Approx( expected( 1, 2 ) ) );
  CHECK( window.values[1] == Approx( expected( 1, 3 ) ) );
  CHECK( window.values[2] == Approx( expected( 1, 4 ) ) );
  CHECK( window.values[3] == Approx( expected( 2, 2 ) ) );
  CHECK( window.values[5] == Approx( expected( 2, 4 ) ) );

  // 3) The maxCells bound refuses to materialize oversized grids.
  sicnu::geo::MultidimGrid rejected;
  CHECK_THROWS_AS( rejected = view.readSlice( "lst", dims, /*maxCells=*/4 ),
                   sicnu::geo::GeoError );

  // 4) Full slice at a resolved instant: the whole (y, x) plane arrives in
  //    time-major order (dimension-order fidelity over the free axes).
  sicnu::geo::MultidimGrid plane = view.readSlice( "lst", dims );
  REQUIRE( plane.rows == 4 );
  REQUIRE( plane.cols == 5 );
  CHECK( plane.values[0] == Approx( expected( 0, 0 ) ) );
  CHECK( plane.values[static_cast<std::size_t>( 4 ) * 5 - 1] == Approx( expected( 3, 4 ) ) );
}

// ---------------------------------------------------------------------------
// 9.0 M5 — logical cube descriptors: per-axis instants (CF-relative numeric
// units and string datetime labels), lazy metadata-only construction, and
// exact JSON round-trip symmetry.
// ---------------------------------------------------------------------------

#include "geospatial/multidim/multidim_cube.h"

TEST_CASE( "cube descriptors resolve CF-relative numeric time axes",
           "[io][multidim][cube][fabric9]" )
{
  if ( !netCdfAvailable() )
  {
    WARN( "netCDF driver unavailable — cube suite skipped" );
    return;
  }
  const std::string dir = scratch( "cube9" );
  const std::string path = writeCube( dir, "numeric.nc" );

  sicnu::geo::MultidimView view = sicnu::geo::MultidimView::open( path );
  const sicnu::geo::MultidimCubeDescriptor cube = sicnu::geo::describeCube( view, "sst" );
  REQUIRE( cube.variable == "sst" );
  REQUIRE( cube.dimensionNames.size() == 3 );
  CHECK( cube.dimensionNames[0] == "time" );

  const sicnu::geo::MultidimCubeAxis &time = cube.axes[0];
  REQUIRE( time.hasNumericValues );
  CHECK( time.numericValues.size() == 2 );
  // "hours since 2026-01-01 00:00:00": 101 h and 202.5 h resolve to UTC.
  REQUIRE( time.instantsResolved );
  REQUIRE( time.instantsUtc.size() == 2 );
  CHECK( time.instantsUtc[0] == "2026-01-05T05:00:00Z" );
  CHECK( time.instantsUtc[1] == "2026-01-09T10:30:00Z" );

  // The y/x axes carry no coordinate values: sizes only, no instants claim.
  CHECK_FALSE( cube.axes[1].instantsResolved );
  CHECK_FALSE( cube.axes[2].instantsResolved );

  // JSON symmetry: descriptor → JSON → descriptor → identical JSON.
  const Json::Value json = cube.toJson();
  const sicnu::geo::MultidimCubeDescriptor roundTrip =
    sicnu::geo::MultidimCubeDescriptor::fromJson( json );
  CHECK( roundTrip.toJson() == json );
}

TEST_CASE( "cube descriptors resolve string datetime axes to normalized instants",
           "[io][multidim][cube][fabric9]" )
{
  if ( !netCdfAvailable() )
  {
    WARN( "netCDF driver unavailable — cube suite skipped" );
    return;
  }
  const std::string dir = scratch( "cube9" );
  const std::string path = writeCubeWithStringTime( dir, "strings.nc" );
  if ( path.empty() )
  {
    WARN( "netCDF-4 container unavailable — string-axis cube suite skipped" );
    return;
  }

  sicnu::geo::MultidimView view = sicnu::geo::MultidimView::open( path );
  const sicnu::geo::MultidimCubeDescriptor cube = sicnu::geo::describeCube( view, "lst" );
  const sicnu::geo::MultidimCubeAxis &time = cube.axes[0];
  REQUIRE( time.hasStringLabels );
  CHECK( time.stringLabels.size() == 3 );
  REQUIRE( time.instantsResolved );
  REQUIRE( time.instantsUtc.size() == 3 );
  // Mixed offsets normalize: "+02:00" becomes Z; verbatim labels stay put.
  CHECK( time.instantsUtc[0] == "2026-07-01T00:00:00Z" );
  CHECK( time.instantsUtc[1] == "2026-07-02T00:00:00Z" );
  CHECK( time.stringLabels[1] == "2026-07-02T02:00:00+02:00" );
}

TEST_CASE( "cube descriptor JSON violations are typed errors",
           "[io][multidim][cube][fabric9]" )
{
  Json::Value broken( Json::objectValue );
  broken["variable"] = "sst";
  // no dimensions at all
  CHECK_THROWS_AS( sicnu::geo::MultidimCubeDescriptor::fromJson( broken ), sicnu::geo::GeoError );

  broken["dimension_names"] = Json::Value( Json::arrayValue );
  broken["dimension_names"].append( "time" );
  broken["axes"] = Json::Value( Json::arrayValue ); // ragged: 1 dim, 0 axes
  CHECK_THROWS_AS( sicnu::geo::MultidimCubeDescriptor::fromJson( broken ), sicnu::geo::GeoError );

  // Axis name mismatch against dimension order.
  Json::Value axis( Json::objectValue );
  axis["name"] = "y";
  axis["size"] = 3;
  broken["axes"].append( axis );
  CHECK_THROWS_AS( sicnu::geo::MultidimCubeDescriptor::fromJson( broken ), sicnu::geo::GeoError );

  // Unknown variables are typed too.
  if ( netCdfAvailable() )
  {
    const std::string dir = scratch( "cube9" );
    sicnu::geo::MultidimView view = sicnu::geo::MultidimView::open( writeCube( dir, "unknown.nc" ) );
    CHECK_THROWS_AS( sicnu::geo::describeCube( view, "nope" ), sicnu::geo::GeoError );
  }
}
