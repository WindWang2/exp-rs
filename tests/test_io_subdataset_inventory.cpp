/***************************************************************************
  tests/test_io_subdataset_inventory.cpp — bounded, redacted subdataset
  inventory + canonical projection. The netCDF path runs only when the
  netCDF driver AND the authoring library exist (repo driver-gate
  convention); the negative contracts run everywhere.
 ***************************************************************************/

#include "geospatial/io/subdataset_inventory.h"
#include "geospatial/common.h"

#include <catch2/catch_test_macros.hpp>

#include <gdal.h>
#include <gdal_priv.h>

#include <netcdf.h>

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using namespace sicnu::geo;
using namespace sicnu::geo::io;

namespace
{

void ensureGdal()
{
  static std::once_flag once;
  std::call_once( once, [] { GDALAllRegister(); } );
}

std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_subdatasets" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

bool netCdfAvailable()
{
  ensureGdal();
  return GDALGetDriverByName( "netCDF" ) != nullptr;
}

} // namespace

TEST_CASE( "inventory refuses sources without a SUBDATASETS domain", "[io][subdatasets][negative]" )
{
  const std::string dir = scratch( "plain" );
  ensureGdal();
  const std::string path = ( fs::path( dir ) / "plain.tif" ).string();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver );
  GDALDatasetH dataset = GDALCreate( driver, path.c_str(), 4, 4, 1, GDT_Byte, nullptr );
  REQUIRE( dataset );
  GDALClose( dataset );

  try
  {
    inventorySubdatasets( path );
    FAIL( "plain rasters have no subdatasets" );
  }
  catch ( const GeoError &error )
  {
    CHECK( error.code() == ErrorCode::InvalidArgument );
  }

  CHECK_THROWS_AS( inventorySubdatasets( dir + "/确实不存在.tif" ), GeoError );
}

TEST_CASE( "inspectSubdataset is a trust boundary, not a second inspector", "[io][subdatasets][negative]" )
{
  const std::string dir = scratch( "selector" );
  // A plain path is NOT a subdataset selector: the projection refuses it so
  // the two surfaces never blur.
  try
  {
    inspectSubdataset( dir + "/plain.tif" );
    FAIL( "plain paths are not subdataset selectors" );
  }
  catch ( const GeoError &error )
  {
    CHECK( error.code() == ErrorCode::InvalidArgument );
  }
  CHECK_THROWS_AS( inspectSubdataset( "" ), GeoError );
}

TEST_CASE( "netCDF multi-variable inventory lists, classifies and projects",
           "[io][subdatasets][netcdf]" )
{
  if ( !netCdfAvailable() )
  {
    WARN( "netCDF driver not available; inventory proof skipped (profile stays honest)" );
    return;
  }

  const std::string dir = scratch( "nc" );
  const std::string path = ( fs::path( dir ) / "cube.nc" ).string();
  // Author a two-variable netCDF with the netCDF C library: GDAL exposes one
  // subdataset per 2D variable when the file holds more than one.
  int ncid = -1;
  REQUIRE( nc_create( path.c_str(), NC_CLOBBER, &ncid ) == NC_NOERR );
  int yId = -1, xId = -1;
  REQUIRE( nc_def_dim( ncid, "y", 3, &yId ) == NC_NOERR );
  REQUIRE( nc_def_dim( ncid, "x", 4, &xId ) == NC_NOERR );
  int dims[2] = { yId, xId };
  int sstId = -1, tempId = -1;
  REQUIRE( nc_def_var( ncid, "sst", NC_FLOAT, 2, dims, &sstId ) == NC_NOERR );
  REQUIRE( nc_def_var( ncid, "temp", NC_FLOAT, 2, dims, &tempId ) == NC_NOERR );
  REQUIRE( nc_enddef( ncid ) == NC_NOERR );
  std::vector<float> cells( 12, 1.0f );
  REQUIRE( nc_put_var_float( ncid, sstId, cells.data() ) == NC_NOERR );
  REQUIRE( nc_put_var_float( ncid, tempId, cells.data() ) == NC_NOERR );
  REQUIRE( nc_close( ncid ) == NC_NOERR );

  const SubdatasetInventory inventory = inventorySubdatasets( path );
  INFO( inventory.toJson().toStyledString() );
  CHECK_FALSE( inventory.truncated );
  REQUIRE( inventory.entries.size() >= 2 );
  for ( const SubdatasetEntry &entry : inventory.entries )
  {
    CHECK( entry.kind == std::string( "subdataset" ) );
    CHECK( entry.display.find( "cube.nc" ) != std::string::npos );
    CHECK_FALSE( entry.name.empty() );
  }

  // Projection: the selected entry opens through the canonical model.
  const RasterMetadata meta = inspectSubdataset( inventory.entries.front().name );
  CHECK( meta.width == 4 );
  CHECK( meta.height == 3 );
}
