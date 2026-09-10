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

